# roblox external

A usermode external for Roblox. Reads game memory from outside the process via
indirect NT syscalls (through a freshly mapped ntdll) and renders ESP, aimbot,
movement features, and more in its own standalone transparent DX11 + ImGui
overlay window.

**No injection.** The process is never touched — only read and written through
Windows kernel APIs.

---

## Agent Operating Protocol

This repo carries the agent operating layer from
[Glockerz/Repo-Template](https://github.com/Glockerz/Repo-Template): doctrine, context,
plans, memory, and a capability-verified single-command context loader, all *in* the repo,
so any fresh agent session can cold-start, remember what happened, and prove itself
before doing work.

```bash
node agent/context.ts          # ONE typed passthrough — run before EVERY response
```

That one command replaces the legacy ~30-invocation bash ritual (`git status`, `git log`,
`git diff`, `ls -R`, `cat` every doc, tailing logs, counting tokens, checking the
environment, …). It parses the full context, reads short-term memory, checks the repo's
own invariants, and emits a problem-solving board. `--note "…"` attaches a note to the
log entry, `--quiet` drops the doc dumps, `--json` is read-only, `--probe` proves the
sandbox, and `--checks[=full]` proves the repo (see [docs/CONTEXT.md](docs/CONTEXT.md)).

The instruction this layer enforces, verbatim:

> This repo will be a template for every future repo agent mode interacts with.
>
> With the `ToDo.md`, you make another section called **Session Memory**. It logs
> everything, and when you use TypeScript instead of Bash, it compresses that agent
> workflow of running 30 commands into **one TypeScript passthrough** that runs before
> every response. This allows it to parse the full context, short-term memory, and
> problem-solve before responding to you.
>
> Read the doctrine/repo context files; and then verify the model's image-sight and
> tooling capabilities (image reading, thinking, frontier capabilities, and context
> tolerance) before continuing with the next user prompt.

How each clause is implemented here:

| Clause | Implementation |
|---|---|
| Template for every future repo | `docs/DOCTRINE.md` (the six rules), `docs/CONTEXT.md` (the machine-oriented map), `ToDo.md` (roadmap + memory) |
| `ToDo.md` → Session Memory | `## Session Memory` mirrors the 12 newest entries from `agent/memory/session.jsonl`, which keeps everything |
| 30 bash commands → one TypeScript passthrough | `agent/context.ts` — zero-dependency (Node ≥ 22.18 type-stripping), run before every response, logs every pass |
| Parse context / memory / problem-solve | the brief: git + tree + doctrine + host map + short-term window + problem-solving board |
| Read doctrine/context, then prove image sight, tooling, thinking, context tolerance | dated probe evidence in `agent/verification/CAPABILITIES.md`, with the vision probe PNG next to it |

Two things make this layer fit *this* repo rather than sit on top of it: the brief knows
it is inside a Windows C++ external (app, library, harnesses, offsets pipeline), and the
passthrough **runs the repo's own cheap invariants on every pass** — `<Windows.h>` staying
in the Win32 layer, no `ReadProcessMemory`/`WriteProcessMemory` anywhere, offsets
fallback parity — with `--checks=full` adding `check_win_tu.sh` and both g++ harnesses.

---

## Features

A ❓ row is not a missing feature: it is one whose behaviour in a live client has not been
watched yet. Every one of them has a step, an expected observation and a piece of evidence to
send back in **[docs/LIVE_CHECKLIST.md](docs/LIVE_CHECKLIST.md)**.

| Feature | Status | Notes |
|---|---|---|
| ESP (boxes, names, health, distance, tool, skeleton, china hat) | ✅ Working | Canonical AABB projection; shared stroke width / glow. Draw distance 0..25,000 studs, where **0 means infinite** and nothing is drawn past the set range: every pass (skeleton, china hat, chams, expanded hitbox, labels, tracers) asks the same gate, and a body too small to box at far range gets a marker instead of being dropped |
| ESP tracers | ❓ Unverified | Screen-anchor lines to the feet; origin, attach point, width, distance cap, colour |
| Head dot | ❓ Unverified | A marker on every head (reference: `visual/headdot`), drawn in the same overlay space and behind the same range gate and filters as the rest of the ESP. The reference scaled it with distance; this is pixels, because a marker that shrinks is a second meaning for one slider |
| Off-screen arrows | ❓ Unverified | Where a player is when the game is not showing them at all (reference: `visual/oof`): a triangle, chevron or arrow on a ring around the view's centre, in the direction their clip-space vector points — with both components negated when they are behind the camera, which is the part that makes an arrow for somebody directly behind you point the right way. Optional name and distance ride inside it. Needs a live check on arrow placement at extreme FOVs |
| Chams | ✅ Working | |
| Lead prediction | ❓ Unverified | The original simple model: the target's `AssemblyLinearVelocity` divided by a per-axis factor, added to the aim point. Two sliders (x/y), no history, no projectile maths. The fitted-velocity and ballistic models were removed with the rest of the added aim machinery - see the aimbot row |
| Aimbot + FOV circle | ✅ Working | **Reverted to the original implementation** at the user's request: target selection by FOV ring (closest body part first), `aim from` is the camera-rotation write or a relative mouse move, per-axis smoothing, an optional humanizer and the original velocity/factor lead. Sticky aim, the target-part choice and the team/wall/dead filters are the ones it always had. Everything layered on top later - the fitted-velocity tracker, the Simple/Ballistic lead model, the angle-based mouse controller and its inverted-Y detection - is gone, along with `features/aimtrack/`. The triggerbot is untouched: its own switch, key, filters and groups |
| Triggerbot | ❓ Unverified | Fires a click when a body is on the crosshair: aim part, a crosshair radius, reaction delay, shot cooldown, max distance, hit chance, optional line of sight. Its own switch and key — the aimbot's settings are its filters, not its gate. Not ported from the reference repo: its projectile/raycast hook that lets it *know* it hit. Instead it watches the **consequence**: 400 ms after each shot it reads the target's health back and logs whether it dropped — a drop proves the click was taken and connected, no change says "a miss, or a click the game never took", and the line says both rather than choosing. It speaks on the first shot and whenever the verdict changes, never per shot |
| Gravity / hip height | ✅ Working | The reference repo's two physics properties (`misc/{gravity,hipheight}`), each with its own save and restore. **No fall is no longer one of them**: its third live failure retired the velocity clamp entirely (a clamp cannot stop a fall - the solver integrates gravity into the *position*, so a clamp only sets the terminal speed). It is now a **position hold** on mover's hold thread (`mover::HoldVertical`, ~66 writes per physics step, both motion blocks): the body's Y is pinned at an anchor that follows you while you walk and freezes on a real descent (below 15 studs/s), and the anchoring is reported - `a fall started ... the position is now held at y ...`, or `the body is N studs below the held level ... send this line` when the engine is winning. `docs/LIVE_CHECKLIST.md` §7b |
| Hit sound | ❓ Unverified | The reference's `misc/hitsound`, wired to a fact this tool can prove: the triggerbot reads the target's health back 400 ms after each click, and the click sound plays **only on the landed verdict** — never on a miss. WAVs come from `sounds/` beside the exe (two decaying tones are written there on first run if it is empty), volume is a slider, and the menu's **test** button plays the selection even with the switch off. `docs/LIVE_CHECKLIST.md` §15 |
| Fling (spin) | ❓ Unverified | New: the character's **angular velocity** is written into both motion blocks from mover's hold thread, unpaced, with the axis re-rolled every 120 ms — a spin about a single axis flings almost nothing, a tumble sweeps a limb through whoever is standing next to you. Angular only, so the character keeps its own movement. The spin rate is rad/s (60–2000, default 420). Physics is the claim: the solver transfers momentum on overlap, the same mechanism a player-thrown part uses. `mover::HoldSpin` is released by the detach path |
| Jump power | ❓ Unverified | The reference's `misc/jumppower`: sets `UseJumpPower = 1`, `JumpPower = value` (10–500) and keeps `JumpHeight = value × 0.144` in step so a place that reads the height model jumps the same distance. The original three fields are captured before the first write and restored on release or key-up; a **changed Humanoid address drops the save** rather than writing a previous body's numbers into a respawned one. `docs/LIVE_CHECKLIST.md` §14 |
| Anti-AFK | ❓ Unverified | The reference's `misc/antiafk`: a spacebar tap every 1–15 minutes through the platform seam, **foreground only** (SendInput goes to whatever has focus, so the window is checked immediately before each tap, and the timer rearms on focus loss). Unverified because it needs an idle session that its server would otherwise drop |
| Noclip | ✅ Working | Always-on when enabled |
| Walkspeed | ✅ Working | Writes `Humanoid::Walkspeed` |
| Flight | ✅ Working | Velocity-driven, real dt. Under the default **stealth profile** it leaves `Workspace.Gravity` alone (the old zero-gravity write was a global, replicated change), carries a hover bias of `0.5·g·dt` trimmed from the measured drift instead, ramps the velocity at a bounded acceleration instead of stepping it, and caps horizontal speed and climb separately (`features/flight/stealth.h`, 30 Linux cases). It also reports when the body stops obeying the pin — the state that precedes a rubberband. Turning stealth off restores the old exactly-still hover |
| Infinite jump | ✅ Working | Edge-triggered impulses, mid-air chainable |
| Click teleport | ❓ Unverified | Ground-plane (feet, not eye) intersection, max range cap, and a player under the cursor resolved to their feet. **The write is anchored now**: 250 ms with `Primitive::Flags.ANCHORED` set while the hold thread hammers the destination, then the anchor lifts and the hold continues for another ~450 ms so the solver adopts the new assembly position. Racing the unanchored solver for 400 ms and letting go is a jump the client never adopts — the old "click tp doesn't work". Every hold now logs where the character actually ended up (`arrived` / `the client put it back`), so the next report names the failure |
| FOV changer | ✅ Working | Writes `Camera::FieldOfView` in the form the camera is actually in (radians below 2.3, degrees above - the first version wrote degrees into a radian field, which is how a 70 became a 4010° projection). **Off restores**: the camera's own value is captured per camera instance before the first write and written back the moment the switch goes off (and again on detach), with a read-back in the log - the user's report was exactly "your fov stays at what it is even when you turn it off" |
| Freecam | ✅ Working | Raw-input mouselook, pole-safe |
| Waypoints | ✅ Working | Pin / goto |
| Hitbox expander | ❓ Unverified | |
| Inventory checker | ✅ Working | Cursor-over-player check |
| Team check | ✅ Working | Teammates tinted in the team color; aimbot skips them |
| Dead check (aimbot) | ❓ Unverified | Skips players whose Humanoid reports 0 hp, and releases a sticky lock when the target dies. The predicate is unit-tested and the health read is the one the players list already uses; what has not been watched is the aimbot letting go of a body in a live server, so it ships off |
| Wall check | ❓ Unverified | One raycast for the ESP, the aimbot and the triggerbot (`features/raycast/`) over a 64-stud grid of world boxes plus live player bodies, rotation-aware, with a box the endpoint sits inside not counting. **Second rework - the box list was empty, not the geometry.** Two independent reasons, both now removed: the primitive-container detector required `Primitive::Size` to validate an entry, and the instance-tree walk *dropped* every part whose size read zero - and this repo already knew that field reads ~0 on the live client (esp.cpp says so). So the wall check had no world to block with. Now: detection retries on **positions alone**, the size field is **measured** by scoring every aligned float triple outside the motion blocks (a build where 0x1c4 is right still picks 0x1c4), a part with no readable size becomes a 1-stud box instead of disappearing, and the `CanQuery` gate - which reads a flag word next to the unreliable rotation - is gone. The log names the lane and the measured offset. `docs/LIVE_CHECKLIST.md` §7 |
| Skybox changer | ❓ Unverified | Six face strings written with the live SSO/heap rules and read back, every preset URL normalised to `rbxassetid://`. **Second rework of the refresh path**: the two validity bytes the file poked (`RenderView::SkyValid 0x28d` / `LightingValid 0x150`) cannot be derived from the reference's 0x2dc/0x278 - the +0x10 relationship that holds for the dump's Lighting fields does not hold for RenderView - so guessing them twice is what "skybox doesnt work" was. The bytes are now **discovered by behaviour**: a paced scan clears each isolated `1` byte in a RenderView window, waits ~3 frames, and keeps the ones the renderer puts back - those are the bytes it consumes while building frames - then clears the whole set on every apply (the "reset the cache every time" the user asked for). Originals are restored either way, and the Sky face offsets are sanity-checked against the *original* strings (a face that is not an asset url means the offsets do not match the build, and the log says so instead of a silent no-op). `docs/LIVE_CHECKLIST.md` §19 |
| Lighting / world environment | ❓ Unverified | fog, exposure, `GlobalShadows = false`, `GrassLength = 0` and the time-of-day sky stay **engine writes**, captured before the first write and restored when their own switch goes off. **fullbright, ambient and night do not**: all three share one write path whose visible result depends on the renderer re-reading Lighting, and the user's verdict on all three was "doesn't work" - so they are now a **screen grade** drawn by the overlay under the ESP (`worldenv::RenderLightingGrade`), in the same priority order (night > fullbright > ambient). A grade cannot be gated by an unknown dirty byte and has nothing to restore. `docs/LIVE_CHECKLIST.md` §12 |
| NPC ESP (bots / animals) | ❓ Unverified | The two *entity* scans from the reference (bots, animals) stay on the **visuals** page as their own group - name tables, a distance, labels - and the five **object** scans (ore, plants, tools, crates, keycards) are gone, with their settings, their config keys and their menu group: the user's report was that the World tab was a list of world objects, and NPCs belong with the rest of the ESP. `docs/LIVE_CHECKLIST.md` §13 |
| Dex (DataModel browser) | ❓ Unverified | The reference's `misc/explorer`, promoted to its **own menu page** (`dex`, page 10, `obs::icon::Cube`): a background BFS of the DataModel into a parent/child snapshot, a collapsible tree drawn with `ImGuiListClipper` (names and classes read only for visible rows), a class/name filter that searches the whole snapshot off the draw thread, a details panel with the address, class, name and copy buttons, and auto-refresh only while the page is actually the one being drawn. `docs/LIVE_CHECKLIST.md` §22 |
| Mesh chams | ❓ Unverified | A second chams mode (`mesh_chams`, own colour): each body part's canonical box is drawn as the six faces that are turned *toward* the camera (the normal comes from the part's rotation matrix, the fill is translucent and a 1px polyline closes the silhouette), sorted far-to-near. It is the cheap stand-in for a mesh outline on a client whose `Primitive::Size` cannot be trusted - the reason the ordinary chams stopped at boxes. `docs/LIVE_CHECKLIST.md` §18 |
| Config save/load | ❓ Unverified | |
| Players (teleport-to / spectate) | ✅ Working | Blacklist steers the aimbot; hiding those names from ESP is a separate switch, off by default |
| Explorer (dex) | ✅ Fixed this pass (live-unverified) | The reference's `misc/explorer` without its winhttp/commdlg/icons: a background BFS over the DataModel (20k nodes, depth 18, 400 ms budget) publishing an immutable snapshot; names and class names are read **when a row is drawn**, so a sweep costs no string reads. Own floating window (Debug → tools → explorer / dex), stays up with the menu closed. Search resolves in the worker and caps at 500 hits. Address/name copy buttons. No icons, no backend |
| Server hop / rejoin | ✅ Working | Public Roblox API, no token needed. A list older than three minutes is re-fetched instead of joined, because a dead job id leaves the client sitting on its join screen |
| Menu theming | ✅ Working | 9 presets (incl. two light), a polish switch over 10 material dials, layout/density/rail, per-group collapse — all persisted |
| Friends | ❓ Unverified | The reference's `visual/players/friends`: one button on the players page marks the selected row, and every ESP element for that player is painted in `friends_esp_color` (box, name, skeleton, chams, tracer) through the same `EspTint` the whitelist and team check use. **Keyed by user id, not by instance address** — the reference's set holds player/model addresses, which is only correct until a respawn rebuilds the model and the mark starts pointing at freed memory. The set is capped at 1,024, saved in the config as ids, and cleared only by the toggle. `docs/LIVE_CHECKLIST.md` §16 |
| FPS cap + FPS widget | ❓ Unverified | The widget's game number is now **measured**, not looked up: `Workspace::DistributedGameTime` advances once per simulated frame, so Δgame/Δreal over a ≥200 ms window is the client's frame rate (both the double and float encodings are tried; a clock that stops reads `--`). The old read, `RunService::HeartbeatFPS`, reported **1 fps on a client at 240** — whatever that offset holds on this build, it is not the frame rate. The cap writes `TaskScheduler::MaxFPS`, classified int-vs-float from its own bytes first, original bytes restored on release/detach/exit. `docs/LIVE_CHECKLIST.md` §11 |

---

## Build

1. Open `roblox_external.slnx` in Visual Studio 2022+.
2. **VS 2022 users:** the project targets toolset `v145` (VS 2026). Downgrade to `v143`:
   ```powershell
   (Get-Content "roblox_external\roblox_external.vcxproj") -replace 'v145','v143' | Set-Content "roblox_external\roblox_external.vcxproj"
   ```
3. Set **Release | x64**, then **Build** (`Ctrl+Shift+B`).
4. Output: `x64\Release\external.exe`

> **`LNK1104: cannot open file '...\x64\Release\external.exe'` means the file is in
> use, not that the code is broken.** The linker could not open its own output. The
> tool is an overlay: closing the menu (or the `HOME` toggle) only hides it, and the
> process keeps running in the tray — which is the usual holder. Exit it from the tray
> icon's **exit** entry (that path restores the writes the tool put in the client; a
> force-kill skips the restore), or:
> ```powershell
> Get-Process external -ErrorAction SilentlyContinue | Stop-Process -Force
> ```
> If it is not running, the file is read-only or something else has it open (an
> Explorer preview pane, or Defender mid-scan — a retry usually works): check
> `attrib "x64\Release\external.exe"`, clear the read-only bit with `attrib -R`,
> delete it, rebuild.

### Requirements
- **Desktop development with C++** workload
- Windows 10/11 SDK
- C++20 (`stdcpp20`)

### Dependencies (vendored)
- **ImGui 1.91.1** — `roblox_external/imgui/`
- **Glass Obsidian** UI library — `glass-obsidian/` (compiled into the project via the `.vcxproj`)

### Linked libraries
`d3d11` · `dxgi` · `dwmapi` · `shell32` · `winhttp` · `winmm`

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

### Linux / Sober (Void)

Run **`roblox-external-boot`** — the bootstrapper (`docs/BOOTSTRAPPER.md`): it
watches for Sober, readiness-gates **Load** (process found, `libroblox.so`
visible, `yama ptrace_scope=0`, display, overlay binary — each gap names its
fix), and Load launches the overlay **locked to Sober** (hidden without the
game window, geometry following it) **with streamproofing** (hides while a
capture app runs). The manager owns the overlay lifecycle; `--console` works
without GTK. Setup: `sh setup.sh` (installs gtk+3 runtime, runs the doctor).

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
  (The separate black *outline* toggle is gone: the user's verdict on it twice was that
  it does nothing, and a second copy of every stroke was the cost of keeping it.)
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
                     platform_linux.cpp (process_vm + EWMH game rect + the
                     x11overlay click-through layer), overlay_linux.cpp
                     (SDL2+GL overlay: transparent, XFixes click-through,
                     game-rect follow), main_linux.cpp (plug-and-play attach
                     + sober_scanner/offsets_dynamic PID-local offsets
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
  features/          27 dirs, one per feature or shared rule:
                     aimbot  antiafk  camera  click_teleport
                     config  esp (esp.h, range.h)  flight  fov (fov_encode.h)
                     fov_changer  freecam  gravity  hipheight  hitbox_expander
                     infinite_jump  inventory_checker  misc (body_rules.h)  movement
                     noclip  nofall  players  raycast (world_ray.h/.cpp)  servers
                     skybox_changer  triggerbot  walkspeed  waypoints  fling  explorer

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
- **[docs/DOCTRINE.md](docs/DOCTRINE.md)** — the agent operating layer: the six rules
- **[docs/CONTEXT.md](docs/CONTEXT.md)** — the agent-facing repo map and command reference
- **[ToDo.md](ToDo.md)** — roadmap and Session Memory
- **[docs/LIVE_CHECKLIST.md](docs/LIVE_CHECKLIST.md)** — the Windows session: one step per ❓ row, what the answer looks like, what to send back
- **[agent/verification/CAPABILITIES.md](agent/verification/CAPABILITIES.md)** — dated capability evidence and the check registry
- **[mirror/xbps-fetch-mirror-template.txt](mirror/xbps-fetch-mirror-template.txt)** — update delivery mirror (every external update ships as a new `mirror/files/` payload); rule 1: function frozen once proven, payload-only changes; §15 OS-gated serving is imperative

---

## License

No license specified. See `glass-obsidian/LICENSE` for the UI library's terms.
