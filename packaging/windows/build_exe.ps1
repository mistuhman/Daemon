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
$StageDir = Join-Path $DistDir  'glass-widget'

function Info($m) { Write-Host "[build_exe] $m" -ForegroundColor Cyan }
function Die($m)  { Write-Host "[build_exe] ERROR: $m" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $Project)) { Die "project not found: $Project" }

# ---------------------------------------------------------------- MSBuild ---
function Find-MSBuild {
    $cmd = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        $vswhere = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe'
    }
    if (Test-Path $vswhere) {
        $p = & $vswhere -latest -prerelease -products * `
                -requires Microsoft.Component.MSBuild `
                -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if ($p) { return $p }
    }
    return $null
}

$msbuild = Find-MSBuild
if (-not $msbuild) {
    Die "MSBuild not found. Install the 'Desktop development with C++' workload, or run this from a Developer PowerShell."
}
Info "msbuild: $msbuild"

# ---------------------------------------------------------------- toolset ---
function Get-InstalledToolsets {
    $roots = @()
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { $vswhere = Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe' }
    if (Test-Path $vswhere) {
        $roots = & $vswhere -latest -prerelease -products * -property installationPath
    }
    $found = @()
    foreach ($r in $roots) {
        $msvc = Join-Path $r 'VC\Tools\MSVC'
        if (Test-Path $msvc) {
            foreach ($d in Get-ChildItem $msvc -Directory) {
                # 14.3x -> v143, 14.4x -> v144, 14.5x -> v145
                if ($d.Name -match '^14\.(\d)') { $found += ("v14" + $Matches[1]) }
            }
        }
    }
    return ($found | Sort-Object -Unique)
}

$projectText = Get-Content $Project -Raw
$currentToolset = if ($projectText -match '<PlatformToolset>(v\d+)</PlatformToolset>') { $Matches[1] } else { 'v143' }

$targetToolset = $Toolset
if ($Toolset -eq 'auto') {
    $installed = @(Get-InstalledToolsets)
    if ($installed.Count -eq 0) {
        Info "could not enumerate toolsets; keeping $currentToolset"
        $targetToolset = $currentToolset
    } elseif ($installed -contains $currentToolset) {
        $targetToolset = $currentToolset
    } else {
        $targetToolset = ($installed | Sort-Object -Descending | Select-Object -First 1)
        Info "$currentToolset not installed (have: $($installed -join ', '))"
    }
}

$patched = $false
$backup  = "$Project.packaging.bak"
if ($targetToolset -ne $currentToolset) {
    Info "patching toolset $currentToolset -> $targetToolset (temporary)"
    Copy-Item $Project $backup -Force
    ($projectText -replace "<PlatformToolset>$currentToolset</PlatformToolset>", "<PlatformToolset>$targetToolset</PlatformToolset>") |
        Set-Content $Project -NoNewline
    $patched = $true
}

# ------------------------------------------------------------------ build ---
try {
    if ($Clean) {
        Info 'clean'
        & $msbuild $Project /t:Clean /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal
    }

    Info "building $Configuration|$Platform"
    $logDir = Join-Path $RepoRoot 'dist\logs'
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $errLog = Join-Path $logDir 'msbuild-errors.txt'
    & $msbuild $Project `
        /t:Build `
        /p:Configuration=$Configuration `
        /p:Platform=$Platform `
        /m `
        /nologo `
        /v:minimal `
        /clp:Summary `
        /p:SolutionDir="$RepoRoot\" `
        /fl /flp:"LogFile=$logDir\msbuild.log;Verbosity=normal" `
        /fl1 /flp1:"LogFile=$errLog;ErrorsOnly;Verbosity=diagnostic"
    if ($LASTEXITCODE -ne 0) {
        # Re-surface the compiler errors where they are readable without
        # downloading the raw CI log (GitHub turns ::error:: into annotations).
        if (Test-Path $errLog) {
            Get-Content $errLog | Where-Object { $_ -match '\S' } | Select-Object -First 15 | ForEach-Object {
                $line = $_ -replace '::', ':'
                if ($env:GITHUB_ACTIONS) { Write-Host "::error title=msbuild::$line" } else { Write-Host $line -ForegroundColor Red }
            }
        }
        Die "msbuild failed (exit $LASTEXITCODE) - full log: dist/logs/msbuild.log"
    }
}
finally {
    if ($patched) {
        Move-Item $backup $Project -Force
        Info 'restored original .vcxproj'
    }
}

# ------------------------------------------------------------------ stage ---
# Default MSBuild OutDir is $(SolutionDir)$(Platform)\$(Configuration)\; we pin
# SolutionDir to the repo root, but still search the project-relative spot so a
# build produced by Visual Studio or an older invocation also packages.
$outDirName = if ($Platform -eq 'x64') { 'x64' } else { 'x86' }
$candidates = @(
    (Join-Path $RepoRoot "$outDirName\$Configuration\glass-widget.exe"),
    (Join-Path $RepoRoot "roblox_external\$outDirName\$Configuration\glass-widget.exe")
)
$exe = $candidates | Where-Object { Test-Path $_ } |
       Sort-Object { (Get-Item $_).LastWriteTimeUtc } -Descending |
       Select-Object -First 1
if (-not $exe) {
    Die ("build reported success but no glass-widget.exe was found. Looked in:`n  " + ($candidates -join "`n  "))
}
Info "binary: $exe"

if (Test-Path $StageDir) { Remove-Item $StageDir -Recurse -Force }
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Copy-Item $exe $StageDir
Copy-Item (Join-Path $RepoRoot 'roblox_external\offsets_fallback.json') $StageDir
$pdb = [IO.Path]::ChangeExtension($exe, '.pdb')
if ($Configuration -eq 'Debug' -and (Test-Path $pdb)) { Copy-Item $pdb $StageDir }

if (-not $Version) {
    $Version = (& (Join-Path $PSScriptRoot '..\version.ps1'))
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

if (-not $NoZip) {
    $zip = Join-Path $DistDir "glass-widget-$Version-$Platform.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $StageDir '*') -DestinationPath $zip
    Info "zip -> $zip"
}

$size = [math]::Round((Get-Item (Join-Path $StageDir 'glass-widget.exe')).Length / 1MB, 2)
Info "done: glass-widget.exe ($size MB)"
