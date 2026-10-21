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
- 2026-10-21T07:33:07.865Z · arena/6bd2b9b2-daemon@b19f4f6 · context pass + checks (dirty:7)
- 2026-10-21T07:33:13.540Z · arena/6bd2b9b2-daemon@b19f4f6 · context pass + checks (dirty:7)
- 2026-10-21T07:33:13.678Z · arena/6bd2b9b2-daemon@b19f4f6 · ti-13 payload 012 staged: follow targets compete on area within evidence tier (attach pid > sober guess > class gate, >=320x240), XFixes input shape scoped to the menu's own windows via SetInputRects (menuui::LastRect/FeatureRect), --locked starts menu closed (click-through at launch), StatusLine follow= d-decl compile fix (main was broken in X11 shape), input=thru|menu|full wording. 5-shape compile proof vs real headers (mirror/libX11, rwiggins/xorgproto, libXfixes mirror, SDL 2.30.10, libglvnd GL/KHR). (dirty:7)
- 2026-10-21T08:33:45.369Z · arena/2c231ec3-daemon@6951347 · context pass + checks (dirty:8)
- 2026-10-21T08:33:45.635Z · arena/2c231ec3-daemon@6951347 · ti-14 payload 013: tiered presentation (menu=interactive, ESP=33ms, plates=500ms, idle=UNMAP -> game unredirected), cache walk gated 16ms<->250ms on consumers, overlay override-redirect via SDL_X11_FORCE_OVERRIDE_REDIRECT + AttachWindow(xid from WMInfo) + XQueryTree EnsureAboveGame @100ms, menu focus click-to-grab/hand-back, StatusLine or/viewable/active + 10s heartbeat, doctor prints xfwm4 compositing facts, menu_linux %.31s fix. 012 receipt: game FPS 4->20 (compositor bill), overlay vanishes on game focus (xfwm4 fullscreen stacking), Sober error screen at launch (suspect ban, not us — reads are process_vm_readv; asked for kill-daemon-boot retest + error text). (dirty:10)
- 2026-10-21T09:06:22.777Z · arena/2c231ec3-daemon@3a19e99 · context pass + checks (dirty:6)
- 2026-10-21T09:06:22.954Z · arena/2c231ec3-daemon@3a19e99 · ti-15 payload 014: crash diagnosis with sources — Sober=Android client, no Hyperion; dominant crash class RBXCRASH OutOfMemory/OutOfMemoryGraphics (VinegarHQ troubleshooting + issues #1141/#1600/#2374); doctor now prints GPU + Sober client log RBXCRASH tail from ~/.var/app/org.vinegarhq.Sober/data/sober/appData/logs/latest.log + documented DFIntTextureQualityOverride fix when OOM; overlay logs runtime compositor (_NET_WM_CM_Sn, status cm=, inline due to held mutex); setup.sh marker now derived from CMakeLists DAEMON_ITERATION (013 mislabeled ti-13). 013 receipt: sticky overlay + click-through + cost tiers all confirmed; menu in game = HOME/F8 (starts closed); waiting on: error screen text / latest.log tail, GPU model, pkill retest. (dirty:8)
- 2026-10-21T09:31:40.267Z · arena/2c231ec3-daemon@6e9d190 · context pass + checks (dirty:6)
- 2026-10-21T09:31:40.449Z · arena/2c231ec3-daemon@6e9d190 · ti-16 payload 015: rblxlog.txt verdict — NO crash; clean Android teardown (leaveGame/destroyLuaApp/setStage:None) 16.9s after join, healthy 139fps, no RBXCRASH. Killer identified: operator's Fleasion 2.4.1 asset proxy (Clear Cache on Launch terminates Roblox on proxy start, Linux bootstrap delay; 86 TlsVerificationFail = its MITM). gpu_memMB=64 = Sober VRAM clamp quirk (#2208/#2165), red herring. Daemon focus audit: takes focus only on explicit menu click, hands back on close, default attached state never touches focus; hotkeys via root grab. Shipped assets/sober-doctor.sh (~/.local/bin/sober-doctor): multi-path log discovery + death classification (RBXCRASH/OOM-FFlags vs clean teardown vs proxy fingerprints vs 64MB clamp + fps/join telemetry); verified against operator's real log. Focus take/handback now logged in overlay.log. menu %.47s warning fixed. STILL UNKNOWN: original path of operator's rblxlog.txt (flatpak path empty at doctor time); fleasion-off isolation test pending. (dirty:8)
- 2026-10-21T09:58:42.931Z · arena/2c231ec3-daemon@5c6c683 · context pass + checks (dirty:3)
- 2026-10-21T09:58:43.121Z · arena/2c231ec3-daemon@5c6c683 · ti-17 payload 016: operator receipt — 'it still crashed' (015 changed zero daemon behavior by design); sober-doctor missed the log: flatpak dir right, FILENAME not latest.log. Fixed: enumerate logs dirs, analyze newest regular file, any name; arg path override wins; verified vs renamed real log + arg + empty home. Operator test flow: browser-launched game + daemon via ulauncher; fleasion state during test UNKNOWN (not controlled). Screenshots (structural only, no OCR): image-1 2560x1440 = game under overlay healthy (same light-gray baseplate sky, UI top-left); image-2 626x238 = small white dialog, grey header, few text lines left-of-center, small greenish element = the 'crash' face — TEXT UNREADABLE from sandbox, must ask operator to type it. Case state: only read log = healthy 139fps client closed by external request (fleasion prime suspect); fresh log unread until 016 runs. Receipts needed: (1) sober-doctor after next close, (2) dialog text, (3) fleasion on/off during test + quit-fleasion retest. (dirty:5)
- 2026-10-21T10:17:33.104Z · arena/2c231ec3-daemon@4b57dde · ti-17 handoff (PR #6 merged, chat ending): OPERATOR RECEIPTS — (1) crash is INDEPENDENT of fleasion (quit-fleasion retest still crashes; both daemon-writes and fleasion theories dead); (2) F8 does nothing; (3) menu has NEVER appeared over Roblox (only in windowed smoke test where it starts open — overlay_linux.cpp:82 vs :555 s_locked_to_sober forces closed). ROOT CAUSE OF DEAD F8 (code audit): HotkeyThread platform_linux.cpp:639-664 — XGrabKey(HOME/F8, AnyModifier) under IgnoreXError, async BadAccess swallowed, never verified; g_hotkeys=true merely for thread creation (StartToggleHotkeys :843-854) so status 'hotkeys=on' is a lying indicator; with menu closed the input shape is EMPTY so the root grab is the ONLY menu entry point over the game. Everything past the grab is wired (toggle consumed even while unmapped, idle path pumps events, menu_open drives draw/unhide/input-shape/render_ui). Fix plan in docs/NEXT.md: verify grabs with recording error handler + fallback keys, add SIGUSR1 no-grab entry point (pkill -USR1), menu must be draggable/resizable/feature-flipping (verify toggle->globals wiring), retest over game. Crash: fresh log STILL UNREAD — sober-doctor (016+) now reads newest file any filename in the logs dirs; first action next chat = operator runs sober-doctor right after a crash. Also open: crash dialog text (small white dialog, text never captured), timing pattern. Full handoff: docs/NEXT.md. (dirty:0)
<!-- session-log:end -->