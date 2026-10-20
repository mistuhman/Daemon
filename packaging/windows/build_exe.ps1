<#
.SYNOPSIS
    Builds glass-widget.exe and packages a portable Windows drop.

.DESCRIPTION
    Locates MSBuild with vswhere, optionally rewrites the platform toolset
    (v145 -> whatever the machine actually has, e.g. v143 on VS 2022), builds
    Release|x64 and stages:

        dist/windows/glass-widget/glass-widget.exe
        dist/windows/glass-widget/offsets_fallback.json
        dist/windows/glass-widget/README.txt
        dist/windows/glass-widget-<version>-x64.zip
        dist/windows/build-info.txt        (toolchain + sha256 provenance)
        dist/logs/msbuild.log              (detailed, for CI forensics)

    The .vcxproj is restored byte-for-byte afterwards, even on failure.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File packaging\windows\build_exe.ps1
    powershell -ExecutionPolicy Bypass -File packaging\windows\build_exe.ps1 -Toolset v143
    powershell -ExecutionPolicy Bypass -File packaging\windows\build_exe.ps1 -Configuration Debug -NoZip
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')] [string] $Configuration = 'Release',
    [ValidateSet('x64', 'x86')]       [string] $Platform      = 'x64',
    # 'auto' = detect the newest installed toolset and patch the project if
    # v145 is missing. Pass an explicit value (v143/v144/v145) to force one.
    [string] $Toolset = 'auto',
    [string] $Version = '',
    [switch] $NoZip,
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$Project  = Join-Path $RepoRoot 'roblox_external\roblox_external.vcxproj'
$DistDir  = Join-Path $RepoRoot 'dist\windows'
$LogDir   = Join-Path $RepoRoot 'dist\logs'
$StageDir = Join-Path $DistDir  'glass-widget'
$Backup   = "$Project.packaging.bak"

function Info($m) { Write-Host "[build_exe] $m" -ForegroundColor Cyan }
function Warn($m) { Write-Host "[build_exe] warning: $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host "[build_exe] ERROR: $m" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $Project)) { Die "project not found: $Project" }

# A previous run that was killed between patching and restoring leaves the
# project on a downgraded toolset. Put it back before doing anything else.
if (Test-Path $Backup) {
    Warn "found a stale $([IO.Path]::GetFileName($Backup)) - restoring the project from it"
    Move-Item $Backup $Project -Force
}

# ---------------------------------------------------------------- MSBuild ---
function Get-VsWhere {
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $base) { continue }
        $p = Join-Path $base 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path $p) { return $p }
    }
    return $null
}

# MSBuild and the toolset list have to come from the *same* Visual Studio
# install: picking msbuild from VS 2022 and then patching the project to a
# v145 that only a side-by-side VS 2026 has produces a baffling
# "toolset not found" at build time.
function Resolve-Toolchain {
    $result = [pscustomobject]@{ MSBuild = $null; VsRoot = $null; Toolsets = @() }

    $vswhere = Get-VsWhere
    if ($vswhere) {
        # newest install first, then every other one that has MSBuild
        $roots = @(& $vswhere -latest -prerelease -products * `
                       -requires Microsoft.Component.MSBuild -property installationPath) +
                 @(& $vswhere -prerelease -products * `
                       -requires Microsoft.Component.MSBuild -property installationPath)
        foreach ($root in ($roots | Where-Object { $_ } | Select-Object -Unique)) {
            $msb = Get-ChildItem -Path (Join-Path $root 'MSBuild') -Filter 'MSBuild.exe' `
                                 -Recurse -File -ErrorAction SilentlyContinue |
                   Where-Object { $_.FullName -match '\\Bin\\(amd64\\|arm64\\)?MSBuild\.exe$' } |
                   # prefer the 64-bit host: LTCG on this project is memory hungry
                   Sort-Object { if ($_.FullName -match '\\Bin\\(amd64|arm64)\\') { 0 } else { 1 } } |
                   Select-Object -First 1
            if (-not $msb) { continue }
            $result.MSBuild  = $msb.FullName
            $result.VsRoot   = $root
            $result.Toolsets = @(Get-ToolsetsIn $root)
            return $result
        }
    }

    # No vswhere (Build Tools on PATH, a Developer prompt, ...): take whatever
    # MSBuild is reachable and infer the toolsets from its install root.
    $cmd = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        $result.MSBuild = $cmd.Source
        $root = $cmd.Source
        for ($i = 0; $i -lt 6 -and $root; $i++) {
            $root = Split-Path $root -Parent
            if ($root -and (Test-Path (Join-Path $root 'VC\Tools\MSVC'))) {
                $result.VsRoot   = $root
                $result.Toolsets = @(Get-ToolsetsIn $root)
                break
            }
        }
    }
    return $result
}

function Get-ToolsetsIn([string] $root) {
    $msvc = Join-Path $root 'VC\Tools\MSVC'
    if (-not (Test-Path $msvc)) { return @() }
    $found = foreach ($d in Get-ChildItem $msvc -Directory -ErrorAction SilentlyContinue) {
        # 14.3x -> v143, 14.4x -> v144, 14.5x -> v145
        if ($d.Name -match '^14\.(\d)') { 'v14' + $Matches[1] }
    }
    return @($found | Sort-Object -Unique)
}

$chain = Resolve-Toolchain
if (-not $chain.MSBuild) {
    Die "MSBuild not found. Install the 'Desktop development with C++' workload, or run this from a Developer PowerShell."
}
$msbuild = $chain.MSBuild
Info "msbuild:  $msbuild"
if ($chain.VsRoot) { Info "vs root:  $($chain.VsRoot)" }

# ---------------------------------------------------------------- toolset ---
$projectText    = Get-Content $Project -Raw
$currentToolset = if ($projectText -match '<PlatformToolset>(v\d+)</PlatformToolset>') { $Matches[1] } else { 'v143' }

$targetToolset = $Toolset
if ($Toolset -eq 'auto') {
    $installed = @($chain.Toolsets)
    if ($installed.Count -eq 0) {
        Warn "could not enumerate installed toolsets; keeping $currentToolset"
        $targetToolset = $currentToolset
    } elseif ($installed -contains $currentToolset) {
        $targetToolset = $currentToolset
        Info "toolset:  $currentToolset (installed)"
    } else {
        $targetToolset = ($installed | Sort-Object -Descending | Select-Object -First 1)
        Info "toolset:  $currentToolset not installed (have: $($installed -join ', ')) -> $targetToolset"
    }
}

$patched = $false
if ($targetToolset -ne $currentToolset) {
    Info "patching toolset $currentToolset -> $targetToolset (temporary, restored after the build)"
    Copy-Item $Project $Backup -Force
    ($projectText -replace "<PlatformToolset>$currentToolset</PlatformToolset>", "<PlatformToolset>$targetToolset</PlatformToolset>") |
        Set-Content $Project -NoNewline
    $patched = $true
}

# ------------------------------------------------------------------ build ---
New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
$buildLog = Join-Path $LogDir 'msbuild.log'
$errLog   = Join-Path $LogDir 'msbuild-errors.txt'
$started  = (Get-Date).ToUniversalTime().AddSeconds(-5)

# Why an environment variable and not /p:SolutionDir=...\ :
# building the .vcxproj directly leaves $(SolutionDir) defaulting to the
# *project* directory, so the exe lands in roblox_external\x64\Release instead
# of the documented x64\Release. Passing the fix as a command-line property
# means passing a value that ends in a backslash, and PowerShell quotes such an
# argument as "...\" - the trailing \" is then eaten as an escaped quote by the
# native argument parser and MSBuild sees a mangled path. That only shows up on
# checkouts whose path contains a space, i.e. not on CI and always on
# "C:\Users\First Last\...". MSBuild reads environment variables as global
# properties, which has no quoting layer at all.
$prevSolutionDir = $env:SolutionDir
$env:SolutionDir = (Join-Path $RepoRoot '')   # guaranteed trailing separator

try {
    if ($Clean) {
        Info 'clean'
        & $msbuild $Project /t:Clean /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal /nologo
    }

    Info "building $Configuration|$Platform (SolutionDir=$env:SolutionDir)"
    & $msbuild $Project `
        /t:Build `
        /p:Configuration=$Configuration `
        /p:Platform=$Platform `
        /m `
        /nr:false `
        /nologo `
        /v:minimal `
        /clp:Summary `
        /fl  /flp:"LogFile=$buildLog;Verbosity=detailed" `
        /fl1 /flp1:"LogFile=$errLog;ErrorsOnly;Verbosity=diagnostic"
    $buildExit = $LASTEXITCODE
    if ($buildExit -ne 0) {
        # Re-surface the compiler errors where they are readable without
        # downloading the raw CI log (GitHub turns ::error:: into annotations).
        if (Test-Path $errLog) {
            Get-Content $errLog | Where-Object { $_ -match '\S' } | Select-Object -First 15 | ForEach-Object {
                $line = $_ -replace '::', ':'
                if ($env:GITHUB_ACTIONS) { Write-Host "::error title=msbuild::$line" } else { Write-Host $line -ForegroundColor Red }
            }
        }
        Die "msbuild failed (exit $buildExit) - full log: dist/logs/msbuild.log"
    }
}
finally {
    if ($patched -and (Test-Path $Backup)) {
        Move-Item $Backup $Project -Force
        Info 'restored original .vcxproj'
    }
    $env:SolutionDir = $prevSolutionDir
}

# ------------------------------------------------------------------ stage ---
# Default MSBuild OutDir is $(SolutionDir)$(Platform)\$(Configuration)\. We pin
# SolutionDir to the repo root, but still search the project-relative spot (a
# Visual Studio build, or an older invocation) and, as a last resort, the whole
# tree - a wrong guess here used to surface as "build succeeded, nothing to
# package", which is the least useful failure mode available.
$outDirName = if ($Platform -eq 'x64') { 'x64' } else { 'x86' }
$candidates = @(
    (Join-Path $RepoRoot "$outDirName\$Configuration\glass-widget.exe"),
    (Join-Path $RepoRoot "roblox_external\$outDirName\$Configuration\glass-widget.exe")
)
$exe = $candidates | Where-Object { Test-Path $_ } |
       Sort-Object { (Get-Item $_).LastWriteTimeUtc } -Descending |
       Select-Object -First 1

if (-not $exe) {
    Warn 'exe not in the expected output directories - searching the tree'
    $exe = Get-ChildItem -Path $RepoRoot -Filter 'glass-widget.exe' -Recurse -File -ErrorAction SilentlyContinue |
           Where-Object { $_.FullName -notlike "$StageDir*" -and $_.LastWriteTimeUtc -ge $started } |
           Sort-Object LastWriteTimeUtc -Descending |
           Select-Object -First 1 -ExpandProperty FullName
}
if (-not $exe) {
    Die ("build reported success but no glass-widget.exe was found. Looked in:`n  " + ($candidates -join "`n  ") + "`n  (and recursively under $RepoRoot)")
}
Info "binary:   $exe"

if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Copy-Item $exe $StageDir
Copy-Item (Join-Path $RepoRoot 'roblox_external\offsets_fallback.json') $StageDir
$pdb = [IO.Path]::ChangeExtension($exe, '.pdb')
if ($Configuration -eq 'Debug' -and (Test-Path $pdb)) { Copy-Item $pdb $StageDir }

if (-not $Version) {
    $Version = (& (Join-Path $PSScriptRoot '..\version.ps1') | Select-Object -First 1)
}

@"
glass-widget $Version ($Configuration|$Platform)

Run:
  1. Start Roblox (windowed or borderless - overlays cannot draw over
     exclusive fullscreen).
  2. Right-click glass-widget.exe -> Run as administrator.
  3. The menu opens automatically; toggle it with HOME.

Keep offsets_fallback.json next to the exe - it is the offline offset source
when the live offset service cannot be reached.

Exit: title-bar X, or right-click the tray icon -> exit.
"@ | Set-Content (Join-Path $StageDir 'README.txt')

Info "staged -> $StageDir"

# ----------------------------------------------------------- provenance ----
$staged = Get-Item (Join-Path $StageDir 'glass-widget.exe')
$sha    = (Get-FileHash $staged.FullName -Algorithm SHA256).Hash.ToLower()

# Evidence that the optimisation settings the README promises actually reached
# the compiler, pulled out of the detailed log so it survives in CI output.
$archFlag = '(not found in log)'
$stdFlag  = '(not found in log)'
if (Test-Path $buildLog) {
    $cl = Select-String -Path $buildLog -Pattern '/arch:(\w+)' -AllMatches -ErrorAction SilentlyContinue |
          Select-Object -First 1
    if ($cl) { $archFlag = $cl.Matches[0].Value }
    $st = Select-String -Path $buildLog -Pattern '/std:c\+\+\w+' -AllMatches -ErrorAction SilentlyContinue |
          Select-Object -First 1
    if ($st) { $stdFlag = $st.Matches[0].Value }
}

$info = @"
version       $Version
configuration $Configuration|$Platform
toolset       $targetToolset$(if ($patched) { " (project declares $currentToolset)" })
msbuild       $msbuild
vs root       $(if ($chain.VsRoot) { $chain.VsRoot } else { 'n/a' })
host          $([System.Environment]::OSVersion.VersionString) / $env:PROCESSOR_ARCHITECTURE
built (utc)   $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))
source exe    $exe
size          $($staged.Length) bytes
sha256        $sha
compile flags $archFlag $stdFlag
"@
$info | Set-Content (Join-Path $DistDir 'build-info.txt')
Write-Host $info

if (-not $NoZip) {
    $zip = Join-Path $DistDir "glass-widget-$Version-$Platform.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $StageDir '*') -DestinationPath $zip
    Info "zip -> $zip"
}

Info ("done: glass-widget.exe ({0:N2} MB)" -f ($staged.Length / 1MB))
