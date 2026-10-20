# roblox external

A usermode external for Roblox. Reads game memory from outside the process via
indirect NT syscalls (through a freshly mapped ntdll) and renders ESP, aimbot,
movement features, and more in its own standalone transparent DX11 + ImGui
overlay window.

**No injection.** The process is never touched — only read and written through
Windows kernel APIs.

---

## Features

| Feature | Status | Notes |
|---|---|---|
| ESP (boxes, names, health, distance, tool, skeleton, china hat) | ✅ Working | Canonical AABB projection |
| Chams | ✅ Working | |
| Aimbot + FOV circle | ✅ Working | Humanizer option (reaction delay + eased aim) |
| Noclip | ✅ Working | Always-on when enabled |
| Walkspeed | ✅ Working | Writes `Humanoid::Walkspeed` |
| Flight | ✅ Working | Velocity-driven, real dt |
| Infinite jump | ✅ Working | Edge-triggered impulses, mid-air chainable |
| Click teleport | ✅ Working | Ground-plane intersection, max range cap |
| FOV changer | ✅ Working | |
| Freecam | ✅ Working | Raw-input mouselook, pole-safe |
| Waypoints | ✅ Working | Pin / goto |
| Hitbox expander | ❓ Unverified | |
| Inventory checker | ✅ Working | Cursor-over-player check |
| Team check | ✅ Working | Teammates tinted in the team color; aimbot skips them |
| Wall check | ❓ Unverified | World-geometry raycast (auto-detected layout) + live player bodies — if walls don't block, run the debug page's world probe |
| Skybox changer | ❌ Broken | Stale offsets — reports via face write-back |
| Config save/load | ❓ Unverified | |
| Players (teleport-to / spectate) | ✅ Working | |
| Server hop / rejoin | ✅ Working | Public Roblox API, no token needed |

---

## Build

1. Open `roblox_external.slnx` in Visual Studio 2022+.
2. **VS 2022 users:** the project targets toolset `v145` (VS 2026). Downgrade to `v143`:
   ```powershell
   (Get-Content "roblox_external\roblox_external.vcxproj") -replace 'v145','v143' | Set-Content "roblox_external\roblox_external.vcxproj"
   ```
3. Set **Release | x64**, then **Build** (`Ctrl+Shift+B`).
4. Output: `x64\Release\glass-widget.exe`

### Shippable apps — one entrypoint

```
make exe        # Windows portable .exe  -> dist/windows/ (+ zip)
make appimage   # Linux .AppImage        -> dist/linux/
make verify     # gate both artifact layouts
```

These `make` targets are the **only** supported way to produce an artifact, and
`.github/workflows/build.yml` runs the exact same ones on every push, PR, `v*`
tag (→ GitHub Release), weekly, and on demand — lint → version → exe → AppImage,
each gated by `packaging/verify.sh`.

Each compiler is exercised **on the platform it ships for**, and each artifact
is executed, not just inspected:

| | where it is built | what is asserted |
|---|---|---|
| `.exe` | a real Windows runner (build number, VS version, MSVC toolsets and SDKs are printed into the run) | PE32+ · machine `x64` · GUI subsystem · imports `d3d11`/`dwmapi`/`winhttp`/`kernel32` · `offsets_fallback.json` byte-matches the repo copy · **then the exe is launched** and a negative NTSTATUS (missing DLL, illegal instruction, access violation) fails the job |
| `.AppImage` | a `fedora:44` container under a headless **Weston** Wayland session | full AppDir layout · type-2 AppImage magic · the payload is re-inspected after crossing the job boundary · **then the AppImage is run** and has to reach `AppRun`'s Wine check and seed its data dir |

The Windows script finds MSBuild itself and auto-downgrades the `v145` toolset
to whatever is installed (restoring the `.vcxproj` afterwards), so step 2 above
is only needed for a manual IDE build. The AppImage wraps the Windows binary in
a Wine launcher with its own prefix — there is no native Linux build yet — and
switches to a direct `exec` when given `--native`.
Details: [packaging/README.md](packaging/README.md).

> Run `make` from the repo root (or `make -C <repo>`): GNU make cannot carry a
> path containing a space through `$(abspath …)`, which is the normal shape of
> a Windows checkout.

> The exe links the **dynamic** CRT, so a target machine needs the Visual C++
> redistributable. `verify.sh` flags this on every build; switching the Release
> configs to `<RuntimeLibrary>MultiThreaded</RuntimeLibrary>` would make the
> drop genuinely self-contained.

### Requirements
- **Desktop development with C++** workload
- Windows 10/11 SDK
- C++20 (`stdcpp20`)

### Dependencies (vendored)
- **ImGui 1.91.1** — `roblox_external/imgui/`
- **Glass Obsidian** UI library — `glass-obsidian/` (compiled into the project via the `.vcxproj`)

### Linked libraries
`d3d11` · `dxgi` · `dwmapi` · `shell32` · `winhttp`

(`dxgi` is reached only through COM `QueryInterface`, so it is a link-time
dependency with no entry in the exe's import table.)

> ⚠️ Release builds with **AVX2**. Needs a ~2013+ CPU. Drop to SSE2 if you hit
> an illegal-instruction crash.

---

## Run

1. Start **Roblox** (windowed or borderless — overlays can't draw over exclusive fullscreen).
2. Run the exe **as Administrator** (required to open a handle to Roblox).
3. The menu opens automatically. Toggle with **HOME** (rebindable).

Roblox does **not** need to be running first — the overlay launches immediately,
attaches in the background, and re-attaches automatically if Roblox restarts.

**Exiting:** title-bar X, or right-click the **tray icon** → exit.

---

## Architecture

```
roblox_external/
  main.cpp           entry (WinMain), FeatureLoop, AttachLoop, render_ui()
  platform/          platform.h seam + platform_win.cpp (real impl);
                     platform_linux.cpp is the Linux starting point
  overlay.h/.cpp     transparent DX11 window, ImGui init, input, tray, taskbar
  menu.cpp           Win32 keybind capture, snapshot reads, config IO, debug tests
  ui/menu_ui.h/.cpp  full menu UI (Glass Obsidian widgets, platform-free)
  globals.h          all settings as inline globals + LogLine() + g_request_exit
  memory.h/.cpp      RPM/WPM wrappers, instance readers
  process.h/.cpp     FindRoblox(), GetRobloxWindow()
  game.h/.cpp        ReadDatamodel(), GetGameName(), GetPlaceId()
  cache.h/.cpp       background thread (16ms) publishing immutable snapshots
  offsets.h          all offsets for the target client version
  offsets_live.h/.cpp  runtime offset refresh from public service
  input_mouse.h      raw mouse delta queue (for freecam mouselook)
  features/          aimbot  camera  click_teleport  config  esp  flight
                     fov_changer  freecam  hitbox_expander  infinite_jump
                     inventory_checker  movement  noclip  players  servers
                     skybox_changer  walkspeed  waypoints

glass-obsidian/      UI library (ImGui public API only)
```

**Platform rule:** everything under `features/` plus `memory`, `process`,
`game`, `cache`, `offsets_live` and `main` compiles against
`platform/platform.h` alone — no `<Windows.h>` outside the Win32 layer
(`overlay`, `menu`, `media_tracker`, `config`, `servers_http`, `nt_syscalls`,
`platform_win`). A Linux port means implementing `platform_linux.cpp`, not
rewriting features. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) §10.

### Threads

| Thread | Job |
|---|---|
| main / render | `overlay::run()` — message pump, ImGui draw, click-through |
| cache | re-reads DataModel → Players → characters every 16ms |
| feature | `FeatureLoop()` — all `Run*` features every 1ms |
| attach | `AttachLoop()` — attach + auto-reconnect, 1s poll |
| input | `input_thread()` — GetAsyncKeyState polling, feeds ImGui |
| keyboard hook | WH_KEYBOARD_LL on its own thread (space press queue) |

### Data flow

```
g_base_address + VisualEngine::Pointer
  → VisualEngine::FakeDataModel
    → FakeDataModel::RealDataModel
      → services (by class name)
        → players → characters → parts
```

Rendering features → `render_ui()` (render thread).
Memory-only features → `FeatureLoop()` (feature thread).

---

## Documentation

- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — detailed technical notes, open problems, motion-block layout
- **[docs/CHANGELOG.md](docs/CHANGELOG.md)** — branch changelog
- **[docs/offsets_dump.txt](docs/offsets_dump.txt)** — raw offset reference

---

## License

No license specified. See `glass-obsidian/LICENSE` for the UI library's terms.
