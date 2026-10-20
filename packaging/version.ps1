# Canonical version string for every artifact this repo ships.
# PowerShell mirror of packaging/version.sh - keep the two in sync.
#
#   tag on HEAD        -> 1.2.3
#   commits since tag  -> 1.2.3+4.g<sha>
#   no tags at all     -> 0.0.0+<yyyyMMdd>.g<sha>
#   not a git checkout -> 0.0.0+<yyyyMMdd>
#
# Override with $env:VERSION.
$ErrorActionPreference = 'SilentlyContinue'

if ($env:VERSION) { $env:VERSION; exit 0 }

$root  = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd')

& git -C $root rev-parse --git-dir *> $null
if ($LASTEXITCODE -ne 0) { "0.0.0+$stamp"; exit 0 }

$sha = (& git -C $root rev-parse --short HEAD)
if (-not $sha) { $sha = 'unknown' }

& git -C $root diff --quiet HEAD -- *> $null
$dirty = if ($LASTEXITCODE -ne 0) { '.dirty' } else { '' }

$tag = (& git -C $root describe --tags --abbrev=0)
if ($tag) {
    $count = (& git -C $root rev-list "$tag..HEAD" --count)
    $ver   = $tag -replace '^v', ''
    if ($count -eq '0') { "$ver$dirty" } else { "$ver+$count.g$sha$dirty" }
} else {
    "0.0.0+$stamp.g$sha$dirty"
}
