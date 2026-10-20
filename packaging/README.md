# packaging

The repo compiles **exactly two** shippable apps, and there is **exactly one**
way to produce either of them: the `make` targets at the repo root. CI
(`.github/workflows/build.yml`) runs those same targets, so a local build and a
green CI run are the same commands in the same order.

```
make version    # canonical version string (packaging/version.sh)
make exe        # Windows portable .exe  -> dist/windows/
make appimage   # Linux .AppImage        -> dist/linux/
make appdir     # stage the AppDir only (no appimagetool, no network)
make verify     # gate: assert the artifact layouts are complete
make all        # exe + appimage + verify
make clean      # rm -rf dist/
```

| Target | Script | Output |
|---|---|---|
| Windows portable `.exe` | `packaging/windows/build_exe.ps1` | `dist/windows/glass-widget/` + `dist/windows/glass-widget-<version>-x64.zip` |
| Linux `.AppImage` | `packaging/linux/build_appimage.sh` | `dist/linux/glass-widget-<version>-x86_64.AppImage` |

Everything lands in `dist/`, which is git-ignored.

## The workflow (always on)

`build.yml` fires on **every push, every PR, every `v*` tag, on demand, and
weekly**:

```
version ──┬─> windows-exe ─> linux-appimage ─> release (tags only)
  lint  ──┘     verify            verify
```

* **version** — one `packaging/version.sh` string for the whole run; both
  artifacts and the release name carry it, so nothing drifts between jobs.
* **lint** — `bash -n` + `shellcheck` on the shell side, PowerShell parser on
  the `.ps1` side. The compiler itself is under test, not just its output.
* **windows-exe** — `make exe` on `windows-latest`, then
  `packaging/verify.sh --windows`; uploads the drop and the zip
  (`if-no-files-found: error`).
* **linux-appimage** — downloads that exe, `make appimage`, then
  `packaging/verify.sh --linux`.
* **release** — on a `v*` tag only, attaches the `.AppImage` and the `.zip` to
  a generated GitHub Release.

Concurrency is keyed on the ref with `cancel-in-progress`, so the newest commit
on a branch is always the one with artifacts.

### Versioning

`packaging/version.sh` (and its mirror `packaging/version.ps1`) is the single
source of truth:

| situation | version |
|---|---|
| tag on HEAD | `1.2.3` |
| commits since tag | `1.2.3+4.g<sha>` |
| no tags | `0.0.0+<YYYYMMDD>.g<sha>` |
| uncommitted changes | `…​.dirty` suffix |

Override with `VERSION=…` (env or `make exe VERSION=…`).

### The gate — `packaging/verify.sh`

Exit code = number of failed checks; **no artifacts is also a failure**.
It checks the PE `MZ` header and size of the exe, that the shipped
`offsets_fallback.json` parses and byte-matches `roblox_external/`, the whole
AppDir layout, that `AppRun` is executable and parses as bash, that no
`@TEMPLATE@` token survived substitution, the ELF header of the `.AppImage`,
and runs `desktop-file-validate` when it is available.

---

## Windows — `.exe`

```
make exe                      # or, directly:
powershell -ExecutionPolicy Bypass -File packaging\windows\build_exe.ps1
```

* Finds MSBuild via `vswhere` (or PATH, or a Developer PowerShell).
* **Toolset auto-fallback:** the `.vcxproj` targets `v145` (VS 2026). On a
  VS 2022 box the script detects the installed MSVC toolsets, patches the
  project to the newest one it actually has (e.g. `v143`), builds, and then
  restores the original project file byte-for-byte — including on failure.
  Force one with `-Toolset v143`, or pin with `-Toolset v145`.
* Builds `Release|x64` by default (`-Configuration Debug`, `-Platform x86`,
  `-Clean`, `-NoZip`, `-Version <s>` are available).
* Stages the exe **plus `offsets_fallback.json`** — `offsets_live.cpp` looks
  for that file in the exe's directory as the offline offset source — and a
  `README.txt` with the run-as-administrator instructions.

## Linux — `.AppImage`

```bash
make appimage EXE=path/to/glass-widget.exe     # or, directly:
packaging/linux/build_appimage.sh --exe path/to/glass-widget.exe
```

There is **no native Linux binary yet**: `overlay.cpp`, `menu.cpp` and
`platform_win.cpp` are still Win32-only (see the README's *Platform rule*;
`platform/platform_linux.cpp` is the port's starting point). So the AppImage
ships the Windows build and launches it through **Wine** — the same way Roblox
itself runs on Linux, which is also what `platform_linux.cpp` assumes when it
scans `/proc/*/comm` for `RobloxPlayerBeta.exe`.

The generated `AppRun`:

* finds `wine`/`wine64` (override with `GLASS_WIDGET_WINE`);
* keeps its **own** 64-bit prefix at
  `$XDG_DATA_HOME/glass-widget/wineprefix` (override with
  `GLASS_WIDGET_WINEPREFIX`) so it never touches `~/.wine`, and bootstraps it
  on first run;
* runs from `$XDG_DATA_HOME/glass-widget` so `imgui.ini`, configs and the
  offset cache land somewhere writable instead of the read-only mount, and
  seeds `offsets_fallback.json` there;
* fails with an actionable message instead of a Wine stack trace when Wine is
  missing.

When a native ELF exists, nothing in the layout has to change:

```bash
packaging/linux/build_appimage.sh --native path/to/glass-widget
```

`AppRun` then `exec`s it directly (same data dir, same offsets seeding).

Flags: `--exe`, `--native`, `--version`, `--appdir-only` (stage the AppDir and
stop), `ARCH=aarch64` via env.

`appimagetool` is used if installed, otherwise downloaded once into
`$XDG_CACHE_HOME/glass-widget-packaging/`. It is invoked with
`--appimage-extract-and-run` so it also works on FUSE-less machines
(containers, CI). With no tool and no network the script still leaves a
complete, runnable `dist/linux/glass-widget.AppDir/AppRun` and exits `2`.

### Files

```
Makefile                         the only supported entrypoint
.github/workflows/build.yml      the same targets, on every push/PR/tag
packaging/
  version.sh / version.ps1       canonical version string (mirrors)
  verify.sh                      artifact gate (used by CI and `make verify`)
  assets/glass-widget.png        256×256 icon (AppImage + .desktop)
  linux/build_appimage.sh        AppDir assembly + appimagetool
  linux/AppRun.in                launcher template (@PAYLOAD_KIND@ = wine|native)
  linux/glass-widget.desktop.in  desktop entry template
  windows/build_exe.ps1          MSBuild + portable zip
```
