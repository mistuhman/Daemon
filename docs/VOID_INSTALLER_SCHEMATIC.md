# Void Linux Installer — Design Schematic (P3)

> Plug-and-play installer for two Void boxes (glibc-X11 + musl-Wayland) — deterministic, no sudo surprises, re-runnable.

## Goal

One command after `xbps-fetch` gives a working `daemon`:

```sh
xbps-fetch -o /tmp/re https://paste.c-net.org/<id>
sh /tmp/re /tmp/extract
sh /tmp/extract/setup.sh        # already proven: BUILD_DONE=YES on Void-66 (g++14.2, SDL2 2.30, Mesa)
```

`setup.sh` must also leave a permanent entry: `~/.local/bin/daemon` + `~/.local/share/applications/daemon.desktop` so XFCE/Whisker and Ulauncher find it by `roblox` — already proven via `assets/install.sh`.

The schematic below makes that install reproducible across both Void variants.

## Layout on disk

```
/tmp/re                     courier payload (sha-gated, 2421KB, 160 files)
/tmp/re-extract/            unpacked tree (payload + setup.sh + assets/ + build/)
~/.local/bin/daemon -> /tmp/re-extract/build/external  *or* copied ELF
~/.local/share/applications/daemon.desktop  (StartupWMClass=daemon)
/tmp/re-extract/build.log    full build log (not tail -n 20)
~/.cache/daemon/offsets.json  cached version-gated offsets
./config/                   per-launch ESP/aim etc (ExeDir+config/)
```

Copy vs symlink choice: `install.sh` currently **copies** the ELF to `~/.local/bin/daemon` so `/tmp` can be cleaned. Keep copy.

## Dependencies matrix

| Host | Must have | xbps name | Check |
|---|---|---|---|
| void-a glibc X11 | compiler toolchain | `base-devel` (already on 66) | `g++ --version` 14.2 |
|  | SDL2+GL | `SDL2-devel` `mesa` `libglvnd-devel` `pkg-config` | `pkg-config --modversion sdl2` 2.30 |
|  | X11 extras (P3 transparent) | `libX11-devel` `libXfixes-devel` `libXtst-devel` | `pkg-config --exists xfixes` |
|  | audio (hitsound P4) | `pulseaudio` or `pipewire` (optional, mute fallback) | `paplay --version` |
|  | Sober | Flatpak `org.vinegarhq.Sober` (Vinegar) | `flatpak ps` shows Sober |
| void-b musl Wayland | same + musl compat | `gcompat` + `MUSL_STATIC=ON` or static libstdc++ | `ldd build/external` no `not found` |
|  | Wayland compositor | `sway` `wlroots` or XWayland | `echo $WAYLAND_DISPLAY` |

`setup.sh` logic (already in `assets/setup-linux.sh`):
- If `xbps-install` not runnable (no sudo / Permission denied on `/`), skip upgrade, still build.
- `pkg-config` + `sdl2-config` probe before cmake; if missing, log hint but continue headless.
- `cmake -B build` with `HAS_SDL_OVERLAY=1` only when SDL+GL found; else headless still proves scanner.
- On failure: print full error block from `build.log` plus `cmake --build build -j1` ordered line.

## X11 specifics (P3 next)

1. **WM_CLASS**: `SDL_setenv("SDL_VIDEO_X11_WMCLASS","daemon",1)` before `SDL_Init` — SDL 2.30 removed `SDL_HINT_VIDEO_X11_WMCLASS`, env var is the backend read.
2. **Always-on-top + borderless**: `SDL_WINDOW_BORDERLESS` + EWMH `_NET_WM_STATE_ABOVE` via `SDL_SetWindowAlwaysOnTop` (SDL 2.0.16+) or Xlib `XChangeProperty`.
3. **Transparency**: request 32-bit visual (GL RGBA8), `glClearColor(0,0,0,0)`, compositor must be on (XFCE compositor enabled by default; if off, fallback to opaque window — log it).
4. **Click-through**: XFixes input shape — when menu closed, `XFixesSetWindowShapeRegion(dpy, win, ShapeInput, 0,0, None)` (empty input shape = clicks pass to game); when menu open, `ShapeInput` = window rect. `libXfixes` is the only new dep.
5. **Game rect**: enumerate `XQueryTree(root)`, read `_NET_WM_PID` per child, match `sober::FindSoberPid()` or `platform::FindGameProcess()` pid, then `XGetWindowAttributes` for x/y/w/h. For Flatpak Sober, `nsenter -m -t <pid> xdotool search --pid` fallback. ESP's `WorldToScreen` already does client→desktop transform; on Linux it will use this rect.

## Flatpak awareness

Sober runs as `bwrap` children; host PID for `sober` is inside mount ns. `sober_scanner::FindSoberPid()` scores by: comm `sober|app_process|RobloxPlayerBeta`, `MapsHasLibRoblox` (libroblox.so r-xp), cgroup `org.vinegarhq.Sober` depth, `libbadcpu.so` bonus. `LibRobloxBase()` tries: host `/proc/pid/maps` r-xp libroblox.so, then `/proc/pid/root/proc/pid/maps` (mountns escape), then `nsenter -m -t pid cat /proc/self/maps`. Host `process_vm_readv` still works on outer pid (same uid) without nsenter.

Yama: `cat /proc/sys/kernel/yama/ptrace_scope` expected `1` (restricted) — same-uid `process_vm_readv` OK; `/proc/pid/mem` needs attach. Scanner logs exact scope.

## Installer steps (setup.sh deterministic)

```sh
#!/bin/sh
set -eu
echo "=== daemon Void setup (payload setup.sh) ==="
echo "[setup] yama ptrace_scope=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo ?)"
# 1. deps (best-effort)
for pkg in base-devel cmake SDL2-devel libX11-devel libXtst-devel libXfixes-devel mesa libglvnd-devel flatpak xbps-fetch curl; do
  if xbps-query -Rs $pkg >/dev/null 2>&1; then sudo xbps-install -y $pkg || echo "[setup] $pkg: already or skip"; fi || true
done
# 2. configure
cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | tee build.log
# 3. build
cmake --build build -j$(nproc) 2>&1 | tee -a build.log; test ${PIPESTATUS[0]} -eq 0
echo "BUILD_DONE=YES" | tee -a build.log
# 4. install entry
sh assets/install.sh  # copies build/external -> ~/.local/bin/daemon + .desktop
# 5. run hint
echo "Run: daemon  or  ~/.local/bin/daemon"
```

Idempotent: re-running `setup.sh` re-configures, rebuilds, reinstalls (no duplicate entries).

## P4+ backlog (not in installer)

- XTest/uinput synthetic mouse (aimbot/triggerbot) — needs `input` group or `CAP_SYS_ADMIN`; stubbed until X11 input proven.
- `config_linux.cpp` full persistence (currently stub writes stub cfg).
- `sober_sig_refresh.py` for TaskScheduler bump — not installer concern.

## Verification

- `xbps-fetch` + `sh /tmp/re /tmp/extract` sha gates already photographable (ARCHIVE_SHA256_GOT/WANT match).
- `BUILD_DONE=YES` + `external` 1.7M ELF + `SDL_CreateWindow` log line = build proof.
- Next receipt after P3 merge: `overlay_linux: window created — P3 menu wired` + screenshot of menu at 1440x900 (debug page).
