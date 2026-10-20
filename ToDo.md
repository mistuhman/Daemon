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
- 2026-10-19T21:46:44.757Z · arena/41a1b6f9-roblox-external@0c2b5af · 018 operator evidence: actual process_vm_readv succeeds on three Sober processes; only libbadcpu named mapping shown, not Roblox base. Window advertises inner PID 3; NSpid maps Main to host 6456, picker erroneously read host kernel PID 3. V2 matches advertised PID to innermost NSpid among same-user module-backed candidates; refuses namespace ambiguity. Compact per-path inventory and other file/memfd ELF header probes, no guessed Roblox identity. Tests pass live fixture reads, memfd, headless window-PID resolver, parser nested/absent fields, invalid args. Preparing 019 diagnostic only. (dirty:5)
- 2026-10-19T22:04:07.909Z · arena/41a1b6f9-roblox-external@d619230 · 019 and replacement020 both failed courier FINAL verification (do not give these URLs as verified). Workflow rerun/dispatch unavailable to integration; no frozen courier changes made. Namespace-aware diagnostic source/tests saved and pushed. Existing018 explicit mode can collect full target mappings now, selecting current PIDs from its automatic receipt rather than stale machine-specific PIDs. Delivery blocked by mirror verification, not code tests. (dirty:0)
- 2026-10-19T22:15:14.412Z · arena/41a1b6f9-roblox-external@d619230 · context pass (dirty:2)
- 2026-10-19T22:16:05.359Z · arena/41a1b6f9-roblox-external@d619230 · Operator018 expanded inventory: Main host PID20115 inner3 has /memfd:sober, unnamed /memfd:, base.apk, Roblox assets, NVIDIA/graphics mappings; sibling inner2 has normal runtime libs only, inner11 has memfd:sober and deleted temp backings but no shown APK/unnamed memfd. This supports Main as runtime candidate, not proof unnamed memfd is libroblox. Deduplicated BACKING output discarded all candidate ranges/perms/file offsets; need preserve raw OTHER_MAP records for memfd/APK/deleted temp regions before deriving ELF load bias. No namespace or Windows-offset assumption warranted. (dirty:2)
- 2026-10-19T22:19:03.677Z · arena/41a1b6f9-roblox-external@d619230 · context pass (dirty:2)
- 2026-10-19T22:22:12.173Z · arena/41a1b6f9-roblox-external@d619230 · Raw operator maps isolate unnamed executable offset-zero memfd in Main, absent companions; not yet Roblox identity. Added bounded ELF PT_DYNAMIC/SONAME resolution for absolute or relative string-table pointers, PT_LOAD range validation, missing/ambiguous refusal; corrected GNU note runtime VA. Compact --images mode, standalone ~small diagnostic-only021 using same courier, no install or dependencies. Tests pass actual native-loader SONAME + synthetic relative-pointer memfd, prior reads/namespaces/errors. Payload shell syntax/source hash/failure exit tested. (dirty:5)
- 2026-10-19T22:28:31.482Z · arena/41a1b6f9-roblox-external@c6655cf · Standalone diagnostic021 published and its individual courier status E2E=PASS at https://paste.rs/YqG56 (FILE_SHA e7694b87c73a1b2197213f774303cd02fffbd7e5e8d377ae2328de21e32a1320, 15107 bytes). Overall courier run failed on older artifacts; only021 success is asserted. Ready to run no-install compact ELF identity receipt on operator; actual engine SONAME/build ID still unverified. (dirty:0)
- 2026-10-19T22:31:02.793Z · arena/41a1b6f9-roblox-external@c6655cf · context pass (dirty:2)
- 2026-10-19T22:41:16.719Z · arena/41a1b6f9-roblox-external@c6655cf · 021 live receipt confirms unnamed memfd SONAME libroblox.so and build ID913ea9839b6084470a25e4be8feddc88214a26a3 only in Main. Integrated shared Linux ELF reader into production PID/base/handle discovery, no hardcoded PID/address/build. Removed name scores, nsenter/self maps, Wine and guessed object anchor fallback. Layout unsupported explicitly; feature loop gated, shell safe. Real production scanner tests pass unnamed executable ELF, comm/data-only rejection, ambiguity, unload/restart; smoke7/7, syntax changed Linux TUs. Small source-verified022 upgrade tested apply/idempotence/local-edit rejection. Full graphics build/live upgraded app unverified. (dirty:14)
- 2026-10-19T22:48:39.596Z · arena/41a1b6f9-roblox-external@40f14a1 · 022 individual mirror courier status success/E2E=PASS at https://paste.rs/am8t4; SHA0936aedb202d3ac9a6e398faf19ea3ad69051377cbfd86ddbfcf4e5e5672be52,98966 bytes. Overall run failed on old artifacts, not022. Applied022 to actual self-extracted018 and compiled/ran production scanner fixtures successfully. Offering source upgrade + existing setup; shell memory attachment enabled but object-layout features explicitly disabled; no claim of full live graphics/features proof. (dirty:0)
- 2026-10-19T22:58:11.491Z · arena/41a1b6f9-roblox-external@40f14a1 · context pass (dirty:2)
- 2026-10-19T22:58:54.042Z · arena/41a1b6f9-roblox-external@40f14a1 · User confirms022 full Void graphics build and installed bootstrapper Load attaches to live Sober (new PID/base, verified handle). Features deliberately disabled; SDL visual not32bitARGB and ordinary shell window unacceptable. User requests create+merge PR now, next chat real Linux feature discovery and proper overlay; investigate Dotline crosshair app actual source for cross-DE priority, cite evidence and distinguish X11/Wayland. Handoff recorded; no Dotline implementation claims or extra patches in closing turn. (dirty:3)
<!-- session-log:end -->
