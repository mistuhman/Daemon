# ToDo

## Tasks

`- [ ]` = open (the passthrough reads these as the session objective), `- [x]` = done.

- [x] Integrate the Repo-Template agent layer (doctrine, context, plans, memory, one
      passthrough, capability verification) into this repo — see `docs/DOCTRINE.md`
- [ ] Rename `roblox_external/process.h` / `process.cpp` → `roblox_process.*` so the
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
- [ ] linux Design schematic for Void Linux installer

## Session Memory

Logged automatically by `node agent/context.ts` — the 12 most recent entries are
mirrored here from `agent/memory/session.jsonl` (which keeps everything).

<!-- session-log:start -->
- 2026-10-17T03:39:39.348Z · arena/13aa7aab-roblox-external@04040ad · context pass + full checks (dirty:3)
- 2026-10-17T03:41:36.308Z · arena/13aa7aab-roblox-external@a07b69c · context pass + full checks (dirty:6)
- 2026-10-17T03:43:15.167Z · arena/13aa7aab-roblox-external@e4312e6 · context pass + full checks (dirty:5)
- 2026-10-17T03:44:21.692Z · arena/13aa7aab-roblox-external@aa801ac · context pass + full checks (dirty:0)
- 2026-10-17T04:03:05.987Z · arena/13aa7aab-roblox-external@e64177b · context pass + full checks (dirty:21)
- 2026-10-17T04:03:09.779Z · arena/13aa7aab-roblox-external@e64177b · context pass + checks (dirty:23)
- 2026-10-17T04:06:27.695Z · arena/13aa7aab-roblox-external@e64177b · context pass + full checks (dirty:28)
- 2026-10-17T04:07:02.147Z · arena/13aa7aab-roblox-external@0cfdb26 · context pass + checks (dirty:2)
- 2026-10-17T04:07:58.704Z · arena/13aa7aab-roblox-external@13a6ed0 · Round two on the nine live failures: no-fall is now mover::HoldVertical (position hold, anchor follows while walking), wall check measured the size field instead of trusting Primitive::Size (both lanes had been dropping every part), skybox discovers the RenderView validity bytes by behaviour and validates the Sky face offsets against the original strings, fullbright/ambient/night are a screen grade, FOV restores on the off edge and detach, outline deleted, mesh chams added with config keys, dex is menu page 10, world objects removed (NPC ESP kept on visuals). 9/9 checks pass. (dirty:0)
- 2026-10-17T04:14:15.644Z · arena/13aa7aab-roblox-external@2d52124 · context pass + full checks (dirty:10)
- 2026-10-17T04:28:51.653Z · arena/13aa7aab-roblox-external@a22669a · context pass + full checks (dirty:17)
- 2026-10-17T04:29:05.398Z · arena/13aa7aab-roblox-external@6a289b1 · Aimbot reverted to the session base implementation at the user's request (prediction modes, tracker, ballistic lead and the angle-based mouse controller removed; features/aimtrack/ deleted). Triggerbot untouched. 155/155 geometry cases, menu shots ok. (dirty:0)
<!-- session-log:end -->
