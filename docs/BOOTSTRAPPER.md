# The bootstrapper (daemon-boot)

**Operator directive, 2026-10-19 (dx9ware shape):** the app opens as a
bootstrapper that watches for Sober, gates a **Load** button on readiness, and
Load launches the overlay **locked to Sober** (follows the game window, never a
free-floating desktop overlay) **with streamproofing**. The bootstrapper is
also the future manager: payload updates, doctor, profiles — architect for
growth. This page is the contract and the growth plan.

## What ships (payload 013)

```
daemon-boot            the manager (this doc)
  watch Sober ─► ready ─► Load ─► spawn 'daemon --locked' ─► own its lifecycle
daemon                  the overlay (unchanged core, new --locked mode)
```

The window: status line + detail line, spinner while watching, Load (gated),
Unload, live log. Nothing else — deliberately. Every behavior lives in the
UI-free core so the future manager tabs have a spine to hang on.

## Architecture

| Layer | File(s) | Role |
|---|---|---|
| manager core | `boot/bootstrap_core.{h,cpp}` | readiness scan, overlay supervision (fork/execv + SIGTERM + waitpid). No UI. |
| GTK3 shim | `boot/gtk3_loader.{h,cpp}` | resolves the 29 `libgtk-3`/`glib` symbols we use via `dlopen` — no GTK headers, no link dependency. Absent GTK ⇒ `--console` still works, one-line error otherwise. |
| window | `boot/main_boot.cpp` | the screen; `--console`, `--check`, `--auto-load`, `--exit-after N`, `--force-load` (smoke). |
| overlay contract | `daemon/platform/main_linux.cpp` | `--locked` flag → `globals::g_locked_to_sober` |
| visibility | `daemon/platform/overlay_linux.cpp` | `ApplyVisibility()` — the only place that hides/shows the window |
| streamproof | `daemon/platform/streamproof_watcher.{h,cpp}` | /proc capturer scan (2 s throttle): obs, vlc, ffmpeg, wf-recorder, … + `globals::streamproof_apps` extras |
| switches | `daemon/globals.h` | `g_locked_to_sober`, `streamproof_enabled`, `streamproof_apps[256]` |

### Why dlopen GTK

Setup installs `gtk+3` (runtime), but nothing in the build hard-links a
toolkit. It also gives us a test seam: `boot/test/fake_gtk3.c` is a stub
`libgtk-3.so.0` that records every call — the whole UI flow is smoke-tested in
a sandbox with no GTK, no X11, no Sober (`boot/test/smoke.sh`, 7 checks).

### Locked + streamproof visibility (one function decides)

`ApplyVisibility()` in `overlay_linux.cpp` hides the window when either:

1. **locked and no game window** — `--locked` is never a desktop overlay;
   geometry follows Sober's EWMH rect (500 ms poll) and the window exists only
   while Sober's does. On unhide, geometry re-latches (Sober may have moved).
2. **streamproof and a capturer runs** — X11 has no per-window
   exclude-from-capture flag (Windows has `SetWindowDisplayAffinity`), so
   hiding from the frame is the honest mechanism. Window capture of Sober
   never includes us in the first place: we are a separate window. Capturers
   are matched by comm/cmdline tokens against the built-in list plus the
   operator's `;`-separated `streamproof_apps` extra. State shows on the
   debug page (`streamproof` / `lock` rows) and in `OverlayStatus()`.

Hidden means unmapped: no draw, events pumped at 20 Hz, hotkeys live.

### Readiness gates (in wall-clock order)

| Gate | Symptom when failing | Fix surfaced in the detail line |
|---|---|---|
| Sober process | "looking for Sober..." | launch Sober (flatpak or /opt/sober) |
| `libroblox.so` in its maps | BASE 0x0, "waiting for Roblox" forever | Flatpak-ns case — detail line explains |
| `yama ptrace_scope == 0` | cross-process reads fail (BASE 0x0) | `sudo sysctl kernel.yama.ptrace_scope=0` |
| DISPLAY/WAYLAND_DISPLAY | overlay cannot open a window | run inside a graphical session |
| overlay binary beside us | Load would exec nothing | run setup.sh |

`daemon-boot --check` prints the same scan as one line — setup.sh runs
it as the **doctor**.

### Lifecycle ownership

The bootstrapper owns the overlay process: Load forks `daemon --locked` in
its own session; Unload or exit sends SIGTERM (the overlay's detach path
restores client writes — we never SIGKILL by default); a dying overlay is
reaped via `waitpid(WNOHANG)`. Closing the bootstrap window stops the overlay.

## Smoke (run it after touching any of the above)

```
sh boot/test/smoke.sh --build    # builds both binaries + runs 7 checks
```

`--check` honesty · console watch · auto-load supervision · GTK label
plumbing · Load click → `--locked` spawn · Unload click → SIGTERM ·
no orphan overlay. `boot/test/fake_gtk3.c` must list the same 29 symbols as
`gtk3_loader.cpp` — if the loader grows, grow the stub.

## Roadmap (the manager this screen is the face of)

1. **Payload update** — reuse the frozen mirror contract (`mirror/state.json`
   live URLs, `mirror/<filename>` statuses, new file name per update): the
   bootstrapper fetches the archive (checksum from the status), stages it,
   and swaps binaries on restart. Function files stay frozen; only payloads
   change.
2. **Doctor** — grow `--check` into the UI page: yama, shlibs noise (known
   harmless on Void), Flatpak-ns detection, SDL/GL presence, XFixes presence,
   each with the one-line fix.
3. **Profiles** — save/load named globals sets; needs the full Linux config
   persistence (`config_linux.cpp` Save/Load are stubs — key=value files under
   `ExeDir()/config/` per the Windows shape).
4. **Log viewer / updates tab / tray** — the core already centralizes state
   (`bootcore::Status` + `Blog`); each is a page, not a rewrite.

## Non-goals (honesty)

- No Windows bootstrapper here — the Windows half is parse-checked only from
  this sandbox (no msbuild/cl).
- No Wayland-native click-through (still the documented stub) — the overlay
  speaks X11/XWayland.
- The GTK shim covers this screen only; growing it is the plan, not pretending
  it is a GTK app framework.