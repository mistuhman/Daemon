# mirror — compiler payload hosting

This directory holds self-contained compiler installers that ride entirely inside one file (base64 gzip tar) so a paste host expiring mid-install can never half-apply.

- `files/roblox-external-compiler-001.ps1` — PowerShell version (Windows)
- `files/roblox-external-compiler-001.txt` — POSIX sh version (Linux, git-bash)

Both are **sha-gated** and **fail-closed**: archive SHA verified before write, target dir must NOT exist, nothing outside the dir you pass is touched.

Workflow `.github/workflows/mirror-upload.yml` mirrors these files to off-repo paste hosts and posts a GitHub Commit Status per file with `target_url` = the paste URL. The status list is the source of truth for current URLs.

## The Windows system32 trap

Admin PowerShell starts in `C:\WINDOWS\system32`. A relative `OutFile` writes there and fails:

> iwr : Access to the path 'C:\WINDOWS\system32\roblox-external-compiler-001.ps1' is denied.

The fix is to always use an absolute path in TEMP for the download.

## Command

```
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; $f="$env:TEMP\roblox-external-compiler-001.ps1"; $d="C:\temp\extract"; if(Test-Path $d){Remove-Item -Recurse -Force $d}; iwr -Uri https://raw.githubusercontent.com/Glockerz/roblox-external/arena/1239e467-roblox-external/mirror/files/roblox-external-compiler-001.ps1 -OutFile $f; powershell -ExecutionPolicy Bypass -File $f $d
```

Success = `COMPILER001_DONE=YES` and `ARCHIVE_SHA256_GOT == ARCHIVE_SHA256_WANT`.

After extraction `compiler\windows\setup.bat` is the Windows setup.
