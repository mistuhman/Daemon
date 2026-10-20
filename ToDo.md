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
- 2026-10-20T08:57:07.870Z · arena/fcc8ff24-daemon@34091b9 · User: deliver the release through the established xbps-fetch mirror (not a branch pull). Packing next payload via tools/pack_linux_payload.sh, letting the frozen mirror-upload courier publish, then handing back the xbps-fetch one-liner. (dirty:0)
- 2026-10-20T09:14:03.325Z · arena/fcc8ff24-daemon@8adc9bc · PAYLOAD 009 RECEIPT (operator, Void): sha 80ea52c9...c078a6 matched, DAEMON_LINUX_009_DONE=YES, setup.sh BUILD_DONE=YES + BOOT_DONE=YES — the 2log.txt link fix is proven live. Bootstrapper live: attached pid 2512, ELF bias 0x7f1668270000, Load spawned overlay pid 2854, Unload SIGTERM restored writes, doctor set yama=0. TWO LIVE FAILURES to track: (1) overlay never appeared over Sober — SDL_GL_CreateContext failed BadMatch (invalid parameter attributes) during setup's demo run -> headless fallback; likely same under Load. (2) crash 'on a baseplate' — need to confirm which process died. User: immortalize THIS version as a baseline. (dirty:2)
- 2026-10-20T09:16:11.637Z · arena/fcc8ff24-daemon@8adc9bc · Baseline record written (docs/baseline-009.md + operator screenshot). GL diagnosis: overlay_linux.cpp forces a 32-bit ARGB X visual BY ID (SDL_VIDEO_X11_WINDOW_VISUALID) then asks GLX for core-3.0/depth24/stencil8/alpha8 — if that X visual is not a GLX-capable fbconfig (or lacks depth/stencil), glXCreateContext returns BadMatch -> headless. The visual-id force (added for the 'not 32-bit ARGB' field report) flipped one failure for another. Fix owed: pick an fbconfig that is GLX + ARGB32, or retry unforced on failure. (dirty:2)
- 2026-10-20T09:20:46.027Z · arena/fcc8ff24-daemon@493dac9 · Operator: Sober (not the daemon) died on a baseplate — target-process death, write-path risk against the client; filed. Directives: fix the overlay visuals, then hand payload 010 via the mirror. Question to answer: why GL rather than a spliced Vulkan swapchain (externals draw via the game's Vulkan present to defeat capture/internal checks) — explain our own-window GL vs injection doctrine. (dirty:1)
- 2026-10-20T09:39:32.712Z · arena/fcc8ff24-daemon@493dac9 · Payload 010 changes proven: GLX-validated ARGB visual picker (fix for baseline-009 'no overlay window over Sober' — BadMatch root cause), overlay window creation fallback chain with per-attempt logs, bootcore overlay.log capture. Six compile shapes clean vs real codeload/API headers (SDL_ResetHint caught by real SDL 2.30.10). Full build + smoke green. (dirty:5)
- 2026-10-20T10:14:40.644Z · arena/fcc8ff24-daemon@b06a28e · Payload 010 receipt verified: outer, archive, and payload hashes all match and bootstrapper launched overlay. Operator requests visible ti-XX testing-iteration flag beside Daemon's name for every version; each new shipment must clear previous iteration to avoid desktop clutter; document this in mirror guidelines without changing frozen courier functions. Desktop compositor never permits ordinary windows above Sober fullscreen/exclusive fullscreen; Sober still crashes randomly. Investigate overlay log and implement iteration identity plus safe prior-install cleanup. (dirty:2)
- 2026-10-20T10:18:00.234Z · arena/fcc8ff24-daemon@b06a28e · Implemented ti-11 identity in bootstrapper title/header/log, SDL title, startup log, and desktop launcher. Added post-success iteration cleanup: marker-based going forward, strict legacy fingerprints only for 009/010, current tree preserved, each deletion printed. Added mirror authoring rule 13.7 without modifying frozen courier functions. Cleanup fixture, full build, boot smoke, and full gates run. (dirty:9)
- 2026-10-20T10:46:10.244Z · arena/f3c07cfc-daemon@39b2df1 · context pass (dirty:0)
- 2026-10-20T10:49:26.871Z · arena/f3c07cfc-daemon@39b2df1 · context pass + checks (dirty:8)
- 2026-10-20T10:51:30.885Z · arena/f3c07cfc-daemon@39b2df1 · context pass + full checks (dirty:8)
- 2026-10-20T10:51:57.158Z · arena/f3c07cfc-daemon@39b2df1 · context pass + full checks (dirty:8)
- 2026-10-20T10:52:01.066Z · arena/f3c07cfc-daemon@39b2df1 · context pass + checks (dirty:8)
<!-- session-log:end -->