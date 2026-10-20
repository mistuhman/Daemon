# ToDo

## Tasks

`- [ ]` = open (the passthrough reads these as the session objective), `- [x]` = done.

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
- 2026-10-19T23:02:38.020Z · arena/ba61af3a-roblox-external@3de5400 · Cold-start familiarization: read README.md Agent Operating Protocol, ran full brief, read DOCTRINE/CONTEXT/CAPABILITIES; probes 9/9 (cmake absent by design, windows-toolchain absent by design); invariants 5/5; vision probe PNG transcribed exactly (AGENT VISION PROBE / K7-Q3-91 / RGB order). Ready for next user prompt. (dirty:2)
- 2026-10-19T23:05:27.257Z · arena/ba61af3a-roblox-external@3de5400 · Session start: continue from latest PR — real Roblox object discovery on Sober (no guesswork, established methods) + main GUI as a real overlay on Sober. (dirty:2)
- 2026-10-20T00:12:57.320Z · arena/ba61af3a-roblox-external@3de5400 · context pass (dirty:30)
- 2026-10-20T00:51:06.174Z · arena/ba61af3a-roblox-external@3de5400 · context pass + checks (dirty:36)
- 2026-10-20T00:52:17.930Z · arena/ba61af3a-roblox-external@3de5400 · context pass + full checks (dirty:36)
- 2026-10-20T00:52:44.080Z · arena/ba61af3a-roblox-external@3de5400 · GUI+overlay stretch landed. (1) menu_ui.cpp rewritten to the user's dx9ware spec: one main column window (name at top, dim status, 22 feature rows with +/- dropdown markers, menu key + exit), each row opens a settings window in a column; strictly two colours (white accent + semi-opaque black plate) via one PushBareStyle (51 colours/6 vars) over obs::ApplyTheme; settings windows now land in a 3-wide grid cascade beside the column (side-by-side, none buried — fixes the overlap seen in w01-three-windows); checkboxes get a 1-frame white border (unchecked was invisible), colours are NoInputs swatches (120px RGBA floats clipped). menu_shots harness extended to 83 (FeatureCount/FeatureRect bleed API + w01-three-windows) = 83 PASS / 0 FAIL. (2) Overlay plan applied: x11overlay::ArgbVisualId() + SDL_VIDEO_X11_WINDOW_VISUALID hint before SDL_CreateWindow (the SDL-supported way to force the 32-bit visual; alpha-8 GLX alone still gave depth-24 in the field); SDL_VIDEODRIVER=x11 pinned when DISPLAY exists (XWayland ok) else headless; DowngradeToWindowed DELETED — overlay-or-headless via FailHeadless; EnsureAboveGame() re-asserts ABOVE+XRaise only when _NET_CLIENT_LIST_STACKING says we are under the game, called every follow tick (500->250ms); _NET_WM_STATE_STICKY added to dock hints; MSAA 4x dropped (vsync kept). (3) Verification: all app TUs -Wall clean; overlay_linux also compiled -DHAS_SDL_OVERLAY=1 against fetched SDL2+Khronos headers; platform_linux also compiled -DHAS_X11=1 -DHAS_XFIXES=1 against cloned libX11/xorgproto/libXfixes headers; node agent/context.ts --checks 5/5 and --checks=full 4/4 (win32 parse, obsidian 13/13, features-geometry 155/0, menu-shots all ok). Evidence crops: _menu_col.png, _menu_dropdowns.png (untracked). PENDING: fixture test for RTTI discovery; live operator check of ARGB visual + stacking on Sober. (dirty:36)
- 2026-10-20T01:58:28.326Z · arena/d0f99e6b-daemon@22ec1f6 · context pass + checks (dirty:164)
- 2026-10-20T02:00:23.507Z · arena/d0f99e6b-daemon@22ec1f6 · context pass + checks (dirty:165)
- 2026-10-20T03:19:41.477Z · arena/d0f99e6b-daemon@8896f4d · context pass + checks (dirty:183)
- 2026-10-20T03:21:12.641Z · arena/d0f99e6b-daemon@8896f4d · context pass + checks (dirty:183)
- 2026-10-20T03:21:33.936Z · arena/d0f99e6b-daemon@8896f4d · context pass + checks (dirty:183)
- 2026-10-20T03:22:32.709Z · arena/d0f99e6b-daemon@8896f4d · context pass + checks (dirty:183)
<!-- session-log:end -->