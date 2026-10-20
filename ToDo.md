# ToDo

## Tasks

`- [ ]` = open (the passthrough reads these as the session objective), `- [x]` = done.

- [ ] **BASELINE-009 follow-ups (2026-10-20, operator receipt).** The build fix is
      proven live (BUILD_DONE=YES / BOOT_DONE=YES; receipt shas and the what-works
      list in `docs/baseline-009.md`, version tagged `baseline-009`). Two live
      failures stand against it: **(1) the overlay never appeared over Sober** —
      the manager owned the process but setup's demo run names the cause verbatim:
      `SDL_GL_CreateContext failed: Could not make GL context current: BadMatch
      (invalid parameter attributes) — headless` → `falling to headless timer`;
      same failure under Load = a log-only process, no window. Fix the GLX
      visual/attrib path (alpha-visual fallback chain, log the requested attribs
      on failure) and re-verify the locked overlay shows. **(2) a crash "on a
      baseplate"** — pin which process died (overlay vs Sober); the bootstrapper
      does not capture the overlay's stdout so a crash leaves no log — capture
      overlay output from `bootcore::StartOverlay` to a file beside the exe.
- [ ] **NEXT SESSION — Linux features and a real overlay.** Live operator receipt
      confirms production attachment to the ELF-identified Sober client and a
      successful graphics build. Game-object layout remains unsupported; keep
      features disabled until validated Linux discovery replaces the old guesses.
      The current SDL shell is an ordinary window, not a satisfactory overlay.
      Investigate **Dotline (crosshair app)** as the user's reference for overlay
      priority across XFCE/GNOME/KDE; locate and inspect its actual implementation
      before attributing any technique to it. Separate X11 and Wayland/compositor
      capabilities; do not promise universal stacking/click-through without proof.
      Cite cross-checkable upstream/forum excerpts. No more PID patch loop or
      Windows-offset fallback. See docs/LINUX_ATTACH_DIAGNOSTIC.md for handoff.
- [x] Integrate the Repo-Template agent layer (doctrine, context, plans, memory, one
      passthrough, capability verification) into this repo — see `docs/DOCTRINE.md`
- [x] **Complete Windows removal and rename to Daemon (Linux-only)**
      - Removed all Windows code (main.cpp, overlay.cpp, menu.cpp, memory.cpp, nt_syscalls, roblox_process, platform_win, offsets_live, etc.)
      - Renamed project from `roblox_external` to `daemon`
      - Updated CMakeLists.txt for Linux-only build
      - Updated README.md, globals.h, overlay_linux.cpp, main_linux.cpp branding
      - Created Linux-only config.cpp (XDG_CONFIG_HOME)
      - Removed WinHTTP servers_http.cpp, using platform/servers_http_linux.cpp instead
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
      failure names its fix), Load spawns `daemon --locked` and the manager owns
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
- 2026-10-20T07:36:42.526Z · arena/fcc8ff24-daemon@f7a44e5 · Session start: read README.md, familiarizing with repo per user request. (dirty:0)
- 2026-10-20T07:42:27.154Z · arena/fcc8ff24-daemon@f7a44e5 · User note: '2log/txt committed to main' — inspecting log/txt files committed on main. (dirty:2)
- 2026-10-20T07:45:31.094Z · arena/fcc8ff24-daemon@f7a44e5 · 2log.txt (origin/main @ 89d978e, user build receipt from /home/sd/Daemon): daemon link fails — ~460 undefined refs to memory layer (is_valid_address, g_memory, read_raw/write_raw, instance::*, fetchstring, mem::*, config::*, clear_instance_caches). User: Daemon is supposed to be built off of the template's working bootloader etc. Diagnosing root cause in tree. (dirty:2)
- 2026-10-20T07:48:47.612Z · arena/fcc8ff24-daemon@f7a44e5 · 2log.txt root cause: CMakeLists.txt CORE_SOURCES omits daemon/memory.cpp (g_memory, is_valid_address, read/write_raw, fetchstring, instance::*, mem::*, clear_instance_caches) and daemon/features/config/config.cpp (config::*) — exactly the two undefined-symbol groups in the user's link receipt. Fix = add both TUs. User constraint to remember: Daemon is supposed to be built off of the template's working bootloader etc (setup-linux.sh '[setup]' flow is what produced the receipt; 'bootloader' likely = daemon-boot bootstrapper, dx9ware shape). Awaiting user clarification on which template is meant. (dirty:2)
- 2026-10-20T08:21:43.268Z · arena/fcc8ff24-daemon@f7a44e5 · Build fixed: CMakeLists now compiles daemon/memory.cpp + features/config/config.cpp (the two TUs dropped in the CMake translation — 2log.txt's undefined-symbol flood), media dup resolved (core media_tracker.cpp kept, platform/media_tracker_linux.cpp dropped from build — both defined the same media::* stubs), config::Save/Load/Delete/Rename signatures aligned to config.h (const std::string&). Full headless build now LINKS: build/daemon + build/daemon-boot. (dirty:4)
- 2026-10-20T08:31:48.206Z · arena/fcc8ff24-daemon@f7a44e5 · context pass + full checks (dirty:6)
- 2026-10-20T08:47:37.245Z · arena/fcc8ff24-daemon@f7a44e5 · Build receipt 2log.txt fixed at cause (memory.cpp + config.cpp missing from CORE_SOURCES; media:: dup; config:: signature mismatch). link_seam_check.sh was hollow (missing heredoc EOF — never compiled anything) and is now a real gate, proven both ways against the 2log shape (21 seams named) and this tree (56 TUs, 0 seams); wired into --checks=full. check_win_tu.sh revived (quote bug, menu.cpp sentinel, Linux TU list, MSVC tripwire removed). features-geometry include root fixed. Bootstrapper polish: OverlayPath rename leftovers. (dirty:11)
- 2026-10-20T08:52:24.471Z · arena/fcc8ff24-daemon@f7a44e5 · context pass + full checks (dirty:11)
- 2026-10-20T08:53:39.645Z · arena/fcc8ff24-daemon@f7a44e5 · All green: headless build links daemon + daemon-boot; boot smoke 7/7 vs real binary; --checks=full 5/5 (win32-tu-parse, link-seam, obsidian-headless, features-geometry, menu-shots). link-seam proven both ways: 2log shape → 21 named seams on daemon target; this tree → 59 TUs, 0 seams. Committing build fixes + gate completion. (dirty:11)
- 2026-10-20T08:57:07.870Z · arena/fcc8ff24-daemon@34091b9 · User: deliver the release through the established xbps-fetch mirror (not a branch pull). Packing next payload via tools/pack_linux_payload.sh, letting the frozen mirror-upload courier publish, then handing back the xbps-fetch one-liner. (dirty:0)
- 2026-10-20T09:14:03.325Z · arena/fcc8ff24-daemon@8adc9bc · PAYLOAD 009 RECEIPT (operator, Void): sha 80ea52c9...c078a6 matched, DAEMON_LINUX_009_DONE=YES, setup.sh BUILD_DONE=YES + BOOT_DONE=YES — the 2log.txt link fix is proven live. Bootstrapper live: attached pid 2512, ELF bias 0x7f1668270000, Load spawned overlay pid 2854, Unload SIGTERM restored writes, doctor set yama=0. TWO LIVE FAILURES to track: (1) overlay never appeared over Sober — SDL_GL_CreateContext failed BadMatch (invalid parameter attributes) during setup's demo run -> headless fallback; likely same under Load. (2) crash 'on a baseplate' — need to confirm which process died. User: immortalize THIS version as a baseline. (dirty:2)
- 2026-10-20T09:16:11.637Z · arena/fcc8ff24-daemon@8adc9bc · Baseline record written (docs/baseline-009.md + operator screenshot). GL diagnosis: overlay_linux.cpp forces a 32-bit ARGB X visual BY ID (SDL_VIDEO_X11_WINDOW_VISUALID) then asks GLX for core-3.0/depth24/stencil8/alpha8 — if that X visual is not a GLX-capable fbconfig (or lacks depth/stencil), glXCreateContext returns BadMatch -> headless. The visual-id force (added for the 'not 32-bit ARGB' field report) flipped one failure for another. Fix owed: pick an fbconfig that is GLX + ARGB32, or retry unforced on failure. (dirty:2)
<!-- session-log:end -->