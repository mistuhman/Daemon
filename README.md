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
| ESP (boxes, names, health, distance, tool, skeleton, china hat) | ✅ Working | Canonical AABB projection; shared stroke width / glow / outline |
| ESP tracers | ❓ Unverified | Screen-anchor lines to the feet; origin, attach point, width, distance cap, colour |
| Chams | ✅ Working | |
| Aimbot + FOV circle | ✅ Working | Humanizer option (reaction delay + eased aim); the key is hold-to-aim or a latch, and the menu says "armed" while it is latched on |
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
| Dead check (aimbot) | ❓ Unverified | Skips players whose Humanoid reports 0 hp, and releases a sticky lock when the target dies. The predicate is unit-tested and the health read is the one the players list already uses; what has not been watched is the aimbot letting go of a body in a live server, so it ships off |
| Wall check | ❓ Unverified | World-geometry raycast (auto-detected layout) + live player bodies — if walls don't block, run the debug page's world probe |
| Skybox changer | ❓ Unverified | The offsets match the live client version now, so the old "every read is stale" failure is gone. Unproven is the pair of `RenderView` validity bytes it flips: no dump publishes them for this release, so the previous numbers are kept and the feature reports by reading the face ids back |
| Config save/load | ❓ Unverified | |
| Players (teleport-to / spectate) | ✅ Working | Blacklist steers the aimbot; hiding those names from ESP is a separate switch, off by default |
| Server hop / rejoin | ✅ Working | Public Roblox API, no token needed. A list older than three minutes is re-fetched instead of joined, because a dead job id leaves the client sitting on its join screen |
| Menu theming | ✅ Working | 9 presets (incl. two light), a polish switch over 10 material dials, layout/density/rail, per-group collapse — all persisted |

---

## Build

1. Open `roblox_external.slnx` in Visual Studio 2022+.
2. **VS 2022 users:** the project targets toolset `v145` (VS 2026). Downgrade to `v143`:
   ```powershell
   (Get-Content "roblox_external\roblox_external.vcxproj") -replace 'v145','v143' | Set-Content "roblox_external\roblox_external.vcxproj"
   ```
3. Set **Release | x64**, then **Build** (`Ctrl+Shift+B`).
4. Output: `x64\Release\external.exe`

### Requirements
- **Desktop development with C++** workload
- Windows 10/11 SDK
- C++20 (`stdcpp20`)

### Dependencies (vendored)
- **ImGui 1.91.1** — `roblox_external/imgui/`
- **Glass Obsidian** UI library — `glass-obsidian/` (compiled into the project via the `.vcxproj`)

### Linked libraries
`d3d11` · `dxgi` · `dwmapi` · `shell32` · `winhttp`

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

## Menu: themes, material, layout

The menu is built on the vendored **Glass Obsidian** library (`glass-obsidian/`, its own
README documents the widget layer). Its theme page is the settings surface, and the
rule it is built to is *live*: every control moves the palette or the metrics the same
frame, there is no Apply button, and no style value is cached in two places.

| Group | What it is |
|---|---|
| **base theme** | one of nine presets — Obsidian, Liquid, Ink (very high contrast), **Frost** and **Paper** (light), Ember, Aurora, Verdant, Crimson — plus a contrast knob (0..1, ink to the extreme of its polarity, surfaces flat, hairlines hard) and a glass tint with its own amount |
| **accent** | ten swatches or a picked colour, a secondary accent for the far end of every gradient (or derived automatically), and five rainbow modes: cycle, sweep, breathe, duotone, off |
| **glass** | transparency (0..100, text stays opaque), corner radius, window shadow, inner glow |
| **material** | **polish** (one switch for the whole layer: every strength below reads as 0, the values you dialled stay put, and dragging a dial turns polish back on), then ten strengths — sheen, edge weight, bloom, rim chroma, frost, specular sweep, aurora, track shimmer, hover lift, surface gradient — plus an animate switch and a speed multiplier. 0 turns a layer off entirely; `animate` off freezes the movement and keeps the polish |
| **layout** | menu width/height, min and max size, ui scale (re-bakes the font atlas), font and title size |
| **density** | grid columns (1..3), card height, card padding, gutter, row spacing, track thickness, control height, page padding |
| **navigation** | rail as labels / icons only / hidden, its width, row height, title-bar height, the pinned session card |
| **detail** | hero cards, one-line hints, collapsible groups, expand/collapse all |
| **reset** | theme and layout reset separately, because a colour experiment and a size experiment fail differently |

Two things about the shape of it, since they are load-bearing:

* **the rainbow never writes into a setting.** The animated accent is derived from the
  render clock in `BuildPalette()`, so `ui_accent_color` always holds what you picked:
  a config saved mid-cycle stores your colour, and turning the mode off restores itself.
* **a closed group submits no cards.** The collapse state is one `';'`-joined list of
  closed `"page/group"` ids (`ui_closed_groups`), not a bool per group — the default
  (everything open) is free, a renamed group costs nothing, and it round-trips.

Every one of those settings is a `ui_*` global in `globals.h`, written and read by
`features/config/config.cpp` with the rest of the config file, so a saved config carries
your theme. The look itself is composed once per frame by `BuildPalette()` /
`ApplyLayout()` in `ui/menu_ui.cpp` — the only place the settings become an
`obsidian::Palette`. The HUD float windows (fps counter, media badge) read the same
palette, which is why a light theme does not leave white text floating over the game.

## ESP: strokes, glow, tracers

The ESP used to build its lines by stacking 1px `AddRectFilled` calls — an outline one,
a colour one, another for the inner edge. That is why the ESP could not be made thicker:
the width was geometry, not a setting. The strokes now come from one vocabulary
(`EspWidth` / `StrokeRect` / `StrokeCorners`, in `features/esp/esp.cpp`) that every ESP
element uses, so three settings reach the whole overlay at once:

* **`esp_line_width`** — one weight for box, corners, skeleton, health bar and tracers.
  A 2px box with a 1px skeleton looks like three plugins; this is what stops that.
* **`esp_outline`** — the black backing, i.e. the reason ESP survives a white baseplate.
* **`esp_glow`** — a dim halo of the *same* colour under a stroke, which is how a line
  reads as lit rather than pasted on.

On top of that: corner-bracket **length** is a setting (`esp_corner_frac`, a fraction of
the box's shorter side so a crouched player keeps four matching brackets), the name
label gets a **plate** (solid or fading downward — a 1px outline is not enough over a
skybox), and **tracers** draw from a screen anchor to the target's *feet*, because the
bottom of the box is where a Roblox character actually stands. Tracers can take the
entity's team/whitelist tint instead of their own colour and can be capped by distance
without hiding the box, so they stay useful as a "who is around the corner" layer.

`build/menu_shots` renders the real `ui/menu_ui.cpp` headlessly (no GPU, no Windows) and
checks each of the nine themes for polarity and legibility; `glass-obsidian/preview/`
holds the resulting gallery.

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
  memory.h/.cpp      chunked read/write helpers (see Memory access), instance readers
  process.h/.cpp     FindRoblox(), GetRobloxWindow()
  game.h/.cpp        ReadDatamodel(), GetGameName(), GetPlaceId()
  cache.h/.cpp       background thread (16ms) publishing immutable snapshots
  offsets.h          offsets for one client version, merged from a dump by
                     glass-obsidian/tools/offsets_regen.py (never pasted - see docs)
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

### Memory access

There is no `ReadProcessMemory` or `WriteProcessMemory` in this binary — not as an
import, not as a call. Every read and write goes to `NtReadVirtualMemory` /
`NtWriteVirtualMemory`, invoked *by address* through `nt_syscalls.{h,cpp}`:

1. a fresh `ntdll.dll` is mapped from disk and its `.text` replaces the loaded copy
   (that removes the anti-cheat's hooks from the copy the syscalls are taken from);
2. syscall numbers are parsed out of the export table at runtime — nothing is
   hard-coded, so a client version bump cannot leave a stale `mov eax, N` behind;
3. `IndirectSyscall*` jumps to the `syscall; ret` **inside ntdll**, rather than
   executing a stub from our own allocation, which is the difference between an
   unhooked call and a call from a suspicious executable-memory region;
4. the handle is opened for the four rights actually used
   (`VM_READ | VM_WRITE | VM_OPERATION | QUERY_INFORMATION`), not `PROCESS_ALL_ACCESS`.

The wrappers used to be named `nt::ReadProcessMemory` / `nt::WriteProcessMemory`,
which described what they *replace* while implying the thing that matters most is
still happening. They are `nt::ReadVirtualMemory` / `nt::WriteVirtualMemory` now, and
`nt_syscalls.h` poisons the WinAPI names with a tripwire `#define` so an accidental
reintroduction fails to compile instead of quietly adding an import
(`ROBLOX_ALLOW_WINAPI_RW` opts a TU out; none needs to).

`memory.cpp` layers one more thing on top: a read of a range that crosses an unmapped
page fails *wholesale* at this level, so large reads are retried in shrinking chunks.

What this does **not** do, in case it matters to you: it is not kernel read/write, it
does not spoof the handle's creator, and `QUERY_INFORMATION` on another process is
itself enumerable. Anything stronger than this is a different class of tool.

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
