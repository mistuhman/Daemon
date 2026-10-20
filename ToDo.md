# ToDo

## Tasks

`- [ ]` = open (the passthrough reads these as the session objective), `- [x]` = done.

- [x] Integrate the Repo-Template agent layer (doctrine, context, plans, memory, one
      passthrough, capability verification) into this repo — see `docs/DOCTRINE.md`
- [x] Rename `roblox_external/process.h` / `process.cpp` → `roblox_process.*` so the
      project header stops shadowing the CRT's `<process.h>` (6 TUs + the vcxproj and its
      filters). Until then `std::thread` / `std::async` / `<future>` do not compile in
      this project and `check_win_tu.sh` refuses them by grep — ARCHITECTURE §9
- [ ] Settle the ❓ rows in `README.md`'s feature table — tracers, hitbox expander, dead
      check, wall check, skybox validity bytes, config round-trip. Each one needs a live
      server, a measurement, and a doc line; that table is the repo's honest task list
- [x] Re-derive the world-probe path for a client version whose layout check fails — the
      instance-tree fallback in `cache.cpp` does it without the guessed container. What is
      still owed is which lane ran on a live client: `LIVE_CHECKLIST` §7 names both lines
- [ ] **The ten reported failures now have mechanisms, not proofs.** Each has a log line
      that answers it: fps (`--` vs a measured number), click TP (`arrived` vs `the client
      put it back`), no fall (`holding vertical speed at 0` / `the hold is being
      overwritten`), wall check (which lane filled the grid, then a wall that blocks),
      outline (a black band OUTSIDE the colour at any width), OOF arrows (on the ellipse,
      never past it), skybox (6/6 faces + the 300 ms byte read-back), fling (does the body
      tumble and does the neighbour launch), dex (a non-zero object count and a tree that
      expands). `docs/LIVE_CHECKLIST.md` §7, §7b, §11, §17-§21. Zoom is deleted.
- [ ] Keep `--checks=full` green after every UI/measure change; it is the only automated
      proof this half of the repo has off Windows
- [ ] **Flight stealth needs a live verdict: did the disconnects stop?** ARCHITECTURE §16.
      The three controllable tells are gone (no gravity write, a ramp, caps) and the feature
      reports the two states that precede a kick (a body that stops obeying the pin, a
      Humanoid animating a knockdown while we fly) - but none of that can prove the server
      is satisfied. Run LIVE_CHECKLIST §5, and if a disconnect survives with no watch line,
      the cause is elsewhere: position deltas (teleport), hitbox expander, or the server's
      own movement checks. The next pass starts from those log lines, not from this section
- [ ] **FPS cap needs a live verdict: does the field classify, and does the client keep
      our value?** ARCHITECTURE §17. The offset dump publishes `TaskScheduler::MaxFPS` with
      no type, so the feature classifies the 4 bytes before writing and refuses an
      unrecognised field. What only a client can answer: whether it is an int on this
      build, and whether the client honours the write past its next focus change. Run
      LIVE_CHECKLIST §11 - if the widget says `-- game fps`, the log line names the raw
      bits, which is the answer to the type question either way
- [ ] **No-fall: the slider can now express the feature, and the verdict is still owed.**
      0 used to mean "released", so every reachable value was a fall SPEED (1-200 studs/s).
      0 now holds the downward component at zero from mover's hold thread (~66 attempts per
      step, both motion blocks). LIVE_CHECKLIST §7b: with 0 the body hovers; a non-zero slider
      is a slow fall by design, not a bug.
- [ ] **The ported world features need live verdicts (LIVE_CHECKLIST §12-§15).** Lighting:
      one group per switch, night wins over fullbright; the restore that matters is per-switch,
      not global. World objects: the count line and whether markers sit on the objects. Jump
      power: a visibly higher jump and a clean restore. Hit sound: audible on confirmed hits,
      silent on misses. Each one is a log line plus an observation, not a guess
- [ ] **The live session: run `docs/LIVE_CHECKLIST.md`.** One step per ❓ row in the README
      table, in the order that needs the least setup, with what the answer looks like and what
      to send back. The three physics features now report their own round trip (ARCHITECTURE
      §13 addendum), so §1 of the checklist has log lines to read rather than jump heights to
      guess at. Answering a row = the ❓ becomes ✅/❌ plus one CHANGELOG line with the
      observation. Then the reader/overlay decision below has data under it
- [x] Port the reference repo's movement/misc half: `misc/{gravity,hipheight,nofall,zoom,antiafk}`
      -- see ARCHITECTURE §13. The rest, in the order the user chose (smallest first):
- [x] The reference's visual half: `visual/headdot` and `visual/oof` -- see ARCHITECTURE
      §14 (both in esp.cpp, behind the shared camera/projection/range gate)
- [x] The reference's aim half: `aim/tracking` + `aim/predict` -- see ARCHITECTURE §15.
      Header-only maths (features/aimtrack/), 27 new Linux cases, two selectable models.
      NOT ported: the reference's per-weapon ballistics table (30 Fallen weapons with
      muzzle speeds/gravity), because those are that game's numbers and this repo cannot
      read a weapon's muzzle speed from the client -- the muzzle speed is a setting here
      until the offsets say otherwise
- [ ] Watch the far-range ESP on a live server: nametags + distance at 25k studs with the
      distance set to 0, and that nothing draws once a range IS set. The rule is
      unit-tested (12 cases); the pixels are not
- [x] linux Design schematic for Void Linux installer — `docs/VOID_INSTALLER_SCHEMATIC.md`
- [ ] **P3 overlay second iteration + the bootstrapper pass ship in payload 013 — live
      verdict owed on the Void box.** The 012 binary could not link
      (`platform::HasGraphicalDisplay` undefined — fixed, and
      `glass-obsidian/tools/link_seam_check.sh` now fails on that exact class of
      break; proven against the 012 file). The app now opens as a bootstrapper
      (dx9ware shape, `docs/BOOTSTRAPPER.md`): watches for Sober, readiness-gates
      Load (libroblox maps, yama ptrace_scope, display, overlay binary — each
      failure names its fix), Load spawns `external --locked` and the manager owns
      the lifecycle. The overlay is locked to Sober (hidden without its window,
      geometry follows the EWMH rect) and streamproofed (hidden while obs/vlc/
      ffmpeg/wf-recorder/… runs; `streamproof_apps` extras via globals — config
      persistence is the P2 config work). Sandbox-proven: link-seam 59 TUs clean,
      full app links + runs headless, `boot/test/smoke.sh` 7/7 against a fake
      libgtk-3.so.0. Live owed: bootstrapper window on XFCE (gtk+3), Load click →
      overlay over Sober only, yama doctor line, streamproof hide/show with OBS
      running, Unload restores the game. Still owed after that: XTest input
      injection (aimbot mouse-aim, anti-AFK tap stay stubs) and Wayland-native
      click-through (XWayland is the target)

## Session Memory

Logged automatically by `node agent/context.ts` — the 12 most recent entries are
mirrored here from `agent/memory/session.jsonl` (which keeps everything).

<!-- session-log:start -->
- 2026-10-19T09:00:53.310Z · arena/80af9bb9-roblox-external@b294440 · context pass (dirty:2)
- 2026-10-19T09:04:37.436Z · arena/80af9bb9-roblox-external@b294440 · Operator receipt: P3 menu wired, SDL2+GL shell active on Void — continuing with XFixes click-through + transparent overlay + EWMH game rect + sober hook/GUI wiring (dirty:2)
- 2026-10-19T09:40:11.193Z · arena/80af9bb9-roblox-external@b294440 · context pass + full checks (dirty:7)
- 2026-10-19T09:42:04.734Z · arena/80af9bb9-roblox-external@b294440 · P3 overlay second iteration: transparent ARGB + XFixes click-through + EWMH game-rect follow + root-grabbed HOME/F8 (x11overlay in platform_linux), sober-scan/overlay rows on debug page, Void font paths. Compile-proven vs real X11/SDL2/GL headers (IsViewable+Status macro collisions fixed); --checks=full 5/5+4/4. Live Void verdict owed. (dirty:9)
- 2026-10-19T10:22:20.919Z · arena/80af9bb9-roblox-external@b294440 · Operator receipt 012: sha gates green but BUILD FAILED (undefined platform::HasGraphicalDisplay - my platform edit dropped it; syntax-only missed link errors). UI verdict: windowed menu works but wrong shape - wants dx9ware-style bootstrapper (GTK, find Sober, Load) + overlay locked to Sober + streamproof. Building that. (dirty:9)
- 2026-10-19T11:02:35.792Z · arena/80af9bb9-roblox-external@48c9844 · context pass + full checks (dirty:12)
- 2026-10-19T11:04:15.482Z · arena/80af9bb9-roblox-external@48c9844 · context pass + full checks (dirty:12)
- 2026-10-19T11:05:15.266Z · arena/80af9bb9-roblox-external@48c9844 · context pass + full checks (dirty:12)
- 2026-10-19T11:06:40.628Z · arena/80af9bb9-roblox-external@48c9844 · context pass + full checks (dirty:12)
- 2026-10-19T11:10:58.610Z · arena/80af9bb9-roblox-external@b4755fc · Bootstrapper pass committed (fd442a4 P0 fix + gate, b4755fc bootstrapper); packing payload 013 (dirty:0)
- 2026-10-19T19:30:55.870Z · arena/80af9bb9-roblox-external@b294440 · 013 fetch failed on operator box: xbps-fetch Service Unavailable from cnet (paste.c-net.org/BorderBrownie). Diagnosing dead-host recovery per mirror doctrine (dirty:17)
- 2026-10-19T20:00:09.717Z · arena/80af9bb9-roblox-external@b294440 · User: create and merge the PR for this session branch (bootstrapper pass + payloads 013/014) (dirty:17)
<!-- session-log:end -->
