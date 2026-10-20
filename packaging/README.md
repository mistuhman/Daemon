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
version ──┬─> compile .exe ──────> compile .AppImage ──> release (tags only)
  lint  ──┘   Windows runner        Fedora + Wayland
              verify + smoke run    verify + smoke run
```

* **version** — one `packaging/version.sh` string for the whole run; both
  artifacts and the release name carry it, so nothing drifts between jobs.
* **lint** — `bash -n` + `shellcheck` on the shell side, PowerShell parser on
  the `.ps1` side, plus a self-test that feeds `pe_info.sh` a synthetic good
  and bad PE. The compiler itself is under test, not just its output.
* **compile .exe** — `make exe` on `windows-latest`. The job first prints the
  host's exact Windows build, Visual Studio version, installed MSVC toolsets
  and Windows SDKs, so the artifact's provenance is in the log. Then
  `packaging/verify.sh --windows`, then it **launches the exe on the runner**
  and fails on a negative NTSTATUS exit (`0xC0000135` missing DLL,
  `0xC000001D` AVX2 on too old a CPU, `0xC0000005` startup crash). Without a
  GPU or Roblox the overlay cannot come up, which is why a clean exit and a
  still-running process both count as a pass — a *crash* does not.
* **compile .AppImage** — runs inside a `fedora:44` container with a headless
  **Weston** compositor, i.e. a real Wayland session and no X server at all.
  It downloads the exe, `make appimage`, `packaging/verify.sh --linux`, and
  then **runs the AppImage it just built**: with Wine deliberately absent the
  expected outcome is `AppRun`'s actionable "Wine not found" message and exit
  1, which still proves the runtime unpacked, `AppRun` executed and the offset
  table reached `$XDG_DATA_HOME`. `workflow_dispatch` with `wine_smoke: true`
  additionally installs Wine and tries to launch the payload (informational,
  never gating).
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

Exit code = number of failed checks; **no artifacts is also a failure**, and a
check that cannot run prints a visible `skip` rather than quietly passing.

Windows drop:

* exe, `offsets_fallback.json`, `README.txt` and `build-info.txt` all present;
* the exe passes `packaging/pe_info.sh` — PE32+, machine `x64`, **GUI**
  subsystem, ≥ 64 KiB, and references to `d3d11.dll`, `dxgi.dll`,
  `dwmapi.dll` and `winhttp.dll` (the four libraries the project links);
* the shipped `offsets_fallback.json` parses as JSON **and** byte-matches
  `roblox_external/offsets_fallback.json`;
* a portable `.zip` exists and really is one.

Linux AppDir / AppImage:

* full AppDir layout, `AppRun` executable and valid bash, no surviving
  `@TEMPLATE@` tokens, `desktop-file-validate` when installed;
* the payload exe is re-inspected **after** crossing the job boundary, so a
  truncated artifact download fails here instead of shipping;
* the `.AppImage` is executable, has an ELF header, is ≥ 128 KiB and carries
  the type-2 magic bytes `41 49 02` at offset 8.

### `packaging/pe_info.sh`

A dependency-free PE/COFF reader: `od` and shell arithmetic only, so the same
code runs in Git Bash on the Windows runner and in the Fedora container. It
prints `format`, `machine`, `subsystem`, section count, entry point, image
size, DLL characteristics, link timestamp, `sha256` and every `*.dll` the
image references, and turns `--expect-machine` / `--expect-subsystem` /
`--expect-import` / `--min-size` into a non-zero exit.

```bash
packaging/pe_info.sh dist/windows/glass-widget/glass-widget.exe \
  --expect-machine x64 --expect-subsystem gui --expect-import d3d11.dll
```

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
* Writes `dist/windows/build-info.txt`: version, configuration, toolset used
  (and the one the project declares), MSBuild path, VS root, host OS, source
  exe path, size, `sha256`, and the `/arch:` and `/std:` flags recovered from
  the build log — so "which compiler produced this binary" is answerable from
  the artifact alone.

**Why `SolutionDir` is set through the environment.** Building the `.vcxproj`
directly leaves `$(SolutionDir)` defaulting to the *project* directory, so the
exe lands in `roblox_external\x64\Release` and packaging finds nothing. The
obvious fix — `/p:SolutionDir="$RepoRoot\"` — is a trap: the value has to end
in a backslash, PowerShell quotes any argument containing a space, and the
resulting `...\"` is consumed as an escaped quote by the native argument
parser. It works on CI (`D:\a\...`) and breaks on
`C:\Users\First Last\...`. MSBuild reads environment variables as global
properties, which has no quoting layer at all.

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
* fails with an actionable message (including the install command for Fedora,
  Debian and Arch) instead of a Wine stack trace when Wine is missing;
* treats the prefix as bootstrapped only when `system.reg` exists, so an
  interrupted first run does not leave a permanently empty prefix behind;
* warns when it is started in a Wayland session with no XWayland
  (`DISPLAY` unset), since Wine's own Wayland driver is still experimental —
  silence it with `GLASS_WIDGET_ALLOW_WAYLAND=1`.

When a native ELF exists, nothing in the layout has to change:

```bash
packaging/linux/build_appimage.sh --native path/to/glass-widget
```

`AppRun` then `exec`s it directly (same data dir, same offsets seeding).

Flags: `--exe`, `--native`, `--version`, `--appdir-only` (stage the AppDir and
stop), `ARCH=aarch64` via env.

The payload is checked for an `MZ` header and run through `pe_info.sh` before
anything is packaged, so a bad artifact download fails immediately rather than
producing a perfectly well-formed AppImage around garbage.

`appimagetool` is used if `$APPIMAGETOOL` points at one, else if it is
installed, otherwise downloaded once into
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
  pe_info.sh                     dependency-free PE/COFF inspector
  assets/glass-widget.png        256×256 icon (AppImage + .desktop)
  linux/build_appimage.sh        AppDir assembly + appimagetool
  linux/AppRun.in                launcher template (@PAYLOAD_KIND@ = wine|native)
  linux/glass-widget.desktop.in  desktop entry template
  windows/build_exe.ps1          MSBuild + portable zip + build-info.txt
```

### Paths with spaces

GNU make cannot carry a path containing a space through `$(abspath …)` or a
prerequisite list, and plenty of real Windows checkouts live under
`C:\Users\First Last\…`. The Makefile therefore uses **only relative paths**
and refuses to run from anywhere but the repo root:

```bash
make exe                       # from the repo root
make -C "/path with spaces/repo" exe
```
