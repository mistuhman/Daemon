# Architecture & Technical Notes

Detailed technical documentation for roblox-external.
See [README.md](../README.md) for build instructions and feature overview.

---


## 4. Open problems (with what's already been ruled out)

### 4.0 ⚠️ THE PRIMITIVE HAS TWO MOTION BLOCKS — read this before touching movement

This is the finding everything else depends on. The float probe on
`version-e7d81637d42c4b23` decodes with no ambiguity:

```
block | rotation | position | linear vel | angular vel
------+----------+----------+------------+-------------
  A   |  0xC8    |  0xEC    |   0xF8     |   0x104
  B   |  0x110   |  0x134   |   0x140    |   0x14C
```

Those numbers are as probed on `version-e7d81637d42c4b23`. On the client this build
ships for, `version-4310300497aa4917`, block A is `0xB0 / 0xD4 / 0xE0 / 0xEC` and block
B `0xF8 / 0x11C / 0x128 / 0x134` - same stride, same intra-block deltas, the whole pair
moved. Which is the habit this section exists to teach: read the **layout** here and the
**numbers** from `offsets.h`, re-derived on every update by `tools/offsets_regen.py`. The
finding has survived three client versions; the values in it have gone stale twice, and
each time it was the copied-not-derived numbers that had to be re-found.

`stride = 0x48 = 36 (rot) + 12 (pos) + 12 (lin) + 12 (ang)`, exactly. These are
**not** "two CFrames" as previously documented — each is a complete motion block,
and the second one carries **velocity too**.

**The decisive evidence:** at the instant of the probe, block B's linear velocity
at `0x140` held `(1.42, 0, -15.94)` — magnitude **16.00**, the character's exact
walkspeed. It is live, not padding.

The code wrote both *positions* but only block A's *velocity*. So every step the
engine re-synced from block B's stale velocity and integrated our write away.
That is what the write test was reporting all along:

```
POS 0xEC: ok dY 10.00  |  POS2 0x134: ok dY 10.00
POS 0xEC: ok dY  0.00  |  POS2 0x134: ok dY 10.00   <-- lost the race
```

Alternating results are a **race**, not a bad offset. A bad offset fails every
time. This one lost roughly half the samples, which is exactly what a 60Hz engine
sync racing an unpaced writer looks like.

**The rule, now enforced in one place (`features/movement/mover.cpp`):** a motion
write is one `0x48` buffer containing all four quantities, written to **both**
blocks (B last — it is the copy the engine re-syncs from). Never write a single
field of a single block. `grep` confirms no feature does any more; they all go
through `mover::WriteCFrame` / `WriteVelocity` / `WriteVelocityAndSpin`.

The block geometry is **derived at runtime** from `Primitive::Rotation` and
`Primitive::CFrame2` (the two the live offset service actually refreshes) rather
than from standalone constants, and `LayoutSane()` checks the whole shape once
and **disables movement with an explanatory log line** if it does not hold —
writing 0x48 bytes at a wrong stride corrupts neighbouring fields instead of
merely not working, so that failure mode is worth refusing.

`tools/mover_pace_test.cpp` keeps the raw probe dump verbatim and re-derives all
of the above from it (45 checks total), so the layout is evidence, not folklore.

### 4.0a ⚠️⚠️ POSITION MUST BE HAMMERED, NOT PACED — and the anchor is irrelevant

Settled by the continuous-write test (`RunTest` id 7), 4 runs. **Hammering both
motion blocks flat out holds the character at the target indefinitely:**

```
t+  0ms  A.y 28.16 (off +0.00)  vel.y 0.00  writes    1
t+125ms  A.y 28.16 (off +0.00)  vel.y 0.00  writes  305
t+500ms  A.y 28.16 (off +0.00)  vel.y 0.00  writes 1248   <- held the whole way
after+50ms  A.y 27.30 (off -0.86)                          <- released, falls normally
```

A **single** write to the identical address reverts within 50ms, every time. So
the engine restores position from a master copy every step, and holding a
position is a **continuous contest**: ~600 writes/s wins it, one write per 16ms
loses it. Occasional glitch samples mid-run (`vel.y -11.44`, `-58.86`, both
recovered by the next sample) are the engine briefly winning and being
overwritten again — that is the fight working as intended.

**Pacing position writes behind `PhysicsStep()` was the single biggest mistake in
this codebase.** It is why flight sank into the ground and why waypoints needed
spamming: we wrote once per 16ms and the engine put us back the other 15. Never
gate a position write on `PhysicsStep()`.

**The anchor does nothing and has been removed.** The anchored survival test
(id 5) held `Anchored` set (flags `0x3A`) for a full 500ms and the position still
reverted to the exact starting value at `vel.y 0.00` — byte for byte the same
timeline as unanchored. Setting it only risks stranding the character and
fighting noclip over the same `0x1b6` byte.

**The authority scan (id 6)** found 6 XYZ triples: `primitive +0xEC`, `+0x134`,
and two aliases each behind `prim->[+0x1A0]` and `prim->[+0x1F0]`. The two
pointers are views of the same primitive, not a separate master — consistent
with "restored every step" rather than "copied from elsewhere".

**Camera: Scriptable sticks.** CameraType test (id 8), 5 runs: wrote 6, read 6
for the full 500ms; the game never fights for it. Hammering type+CFrame moved the
camera exactly +30 studs (4.36 → 34.36) every run. Freecam's "rotates around your
player but will not move" was pacing again: orientation was ours because nothing
else writes it, position was the default camera module's because it wrote more
often than we did.

**⚠️ THROUGHPUT FIRST.** Before diagnosing any movement symptom, check the debug
page's `pos writes` row. Four identical runs of the write test spanned ~200x
(17127 → 88 per 500ms) purely with system load, and the run at ~2.9 writes/step
lost the position while the run at ~27 held it. **A movement feature that "works
sometimes" is almost certainly losing the write race, not misbehaving logically.**
Position holds now run on a dedicated thread for exactly this reason.

**Revert-timing decision table** — run the survival test (id 4) first on any new
symptom:

| What you see | What it means |
|---|---|
| Holds to the end of the window | Write path is fine — bug is the feature's logic |
| Reverts within 1–2 samples, v≈0 | Restored every step. **Hammer it**, do not pace |
| Reverts at ~100–300ms **with** real velocity | Server replication correction — not winnable locally |
| Smooth drift back | Fighting the humanoid controller |

**Velocity must be hammered, not written once.** Two identical single-write runs
in one session gave `climbed -0.01` and `climbed +25.24` — a coin flip, because
the humanoid controller damps vertical velocity every step and a lone write only
survives if it lands just after a step boundary. Every *hammered* run climbed
cleanly (+35.09, +34.39 studs / 375ms, velocity pinned at 100.00).

`mover::HoldVelocity(owner, v)` / `ReleaseVelocityHold(owner)` hammer from the
hold thread for as long as the caller wants. While held, velocity is **pinned**
and the climb is **linear** (~93.6 studs/s at a written 100) — a controlled rise,
not a ballistic arc. Infinite jump holds it while space is down and releases on
key-up, which is exactly what RunTest id 10 does and why that test feels right.

**Do not replace this with a fixed-length impulse.** That was tried at 90ms and
produced ~9.6 studs at the default power — correct, repeatable, and
indistinguishable from an ordinary jump (~7 studs). The mechanism was never the
problem; the duration was.

**Velocity, when it lands, works perfectly** — settled by RunTest 9/10.
A *single* write of `y=100` gives a full self-decaying arc (`100.00 → 74.66 →
50.13 → 2.72` over 400ms, climbing +25.25 studs): the engine integrates it and
applies gravity for us. **Hammering** velocity instead pins it and gives a
constant-speed lift (+30.45 studs in 375ms, linear).

So the two mechanisms are opposite and both are correct for their feature:

| | position | velocity |
|---|---|---|
| single write | reverted within 50ms | full correct jump arc |
| hammered | holds indefinitely | pinned, constant-speed lift |

Flight hammers **position**. Infinite jump writes **velocity once** and lets the
engine own the arc. Modelling the arc ourselves is doubly wrong: this place's
gravity is **243**, not the 196.2 the old code hardcoded (`World::Gravity` is
per-place), and even that does not reproduce the real curve (+25.25 measured vs
+20.54 analytic) because a humanoid controller and damper are in the loop.

### 4.1 Flight, infinite jump, waypoints, freecam — SOLVED (was "THE blocker")

**The bug was never the offsets, and it was never which CFrame field to write.**
It was the write RATE, and it was diagnosable from one sentence in the report:
*"tp to player works fine"*. Teleport-to-player and waypoint-teleport call the
same function. The only difference between them is distance — so the destination
was never the problem.

`FeatureLoop()` is `Sleep(1)`, i.e. **500–1000 iterations a second**. Roblox steps
physics and replication at **60**. Every movement feature wrote on every
iteration:

| Feature | What it did per iteration | Effect |
|---|---|---|
| Flight | integrated position with a hardcoded `dt = 0.016` and pushed both CFrames | moved ~16x the slider value; ~10 position writes inside every physics step |
| Infinite jump | re-asserted `vel.y` for 180ms (~150 writes), froze `EvaluateStateMachine`, hand-wrote humanoid state id 7 | pinned to a constant upward speed while the engine subtracted gravity; state machine left frozen if the restore path was missed |
| Teleport hold | re-wrote the destination CFrame for 280ms with the root part **anchored** | anchoring stops the client replicating a state the server can follow, so the server corrects you back — the rubber-band, and worse the further you went |
| Noclip | walked the whole character and rewrote every `Primitive::Flags` | thousands of RPM/WPM a second into the same structs the others were writing |

An engine that consumes one update per step and gets ten discards nine of them —
most landing mid-solve — and the replicator ends up with a position history the
server will not accept. **That is the rubber-banding.** A single write (tp to
player, before its hold kicked in) sails through, which is exactly why that one
felt fine.

**The fix: `features/movement/mover.{h,cpp}` — one owner for the character.**
- **A real clock.** `mover::Dt()` is QPC-derived elapsed seconds, clamped to
  `[0, 0.1]` so a stall cannot become one enormous integration step. No feature
  hardcodes a dt any more; flight does not even have one (it drives velocity,
  and velocity is already studs/second).
- **A paced gate.** `mover::PhysicsStep()` is true at most once per 16ms. Every
  write the solver also touches goes behind it. Verified: 62 ± 2 steps/second at
  loop rates from 100Hz to 2000Hz, at most one step per millisecond, and a 500ms
  stall resyncs to **one** step instead of a catch-up burst.
- **One owner at a time.** `Freecam < Jump < Flight < Teleport`. A claim by an
  equal-or-higher owner takes the body, handing back the previous owner's saved
  state on the way. This is what stops noclip's flag save and the teleport's
  anchor save from clobbering each other — the failure that left characters
  permanently anchored or non-colliding.
- **Save/restore that survives a respawn.** Every saved value carries the address
  it came from; a new character (new primitive pointer) drops all of it and
  releases the owner, instead of writing `PlatformStand = false` into a dead
  Humanoid.
- **Torn writes are gone.** `WriteCFrame` builds one 12-float buffer and issues
  **one 48-byte write per CFrame copy** — never a read between the two writes, so
  the solver can never observe frame N's rotation with frame N+1's position.

**Per feature:**
- **Flight pins VELOCITY and never writes position** — it feeds the same
  hammered-burst machinery infinite jump uses (`mover::VelocityBurst`,
  re-fed every 25ms with a 500ms deadline). Three killed iterations led here:
  a position hammer rubberbands no matter what (66 overwrites of the engine's
  own integration per step keep invalidating the assembly/render/replication
  state faster than it settles — every internal re-derive is a visible pop);
  feature-loop velocity writes sink (1-2 torn writes per step lose to the
  humanoid damper); a HAMMERED velocity is the one thing the debug test
  measured as perfectly smooth (+25 studs in 375ms, monotonic) — because the
  ENGINE computes the position itself, everything downstream stays consistent,
  and the replicator forwards a velocity the server dead-reckons with. No
  keys held pins velocity 0 = hover. PlatformStand stays on while flying (it
  stands the humanoid controller — the vertical damper — down). A teleport
  outranks flight and stops the burst for its hold; flight resumes from
  wherever the teleport landed you, with nothing to re-seed. That is the one
  mechanism the measurements verified: a one-shot position write reverts within
  a step (even ANCHORED — the anchored survival test reverted in 50ms with the
  flag set), while ~1700 writes over 500ms held +25 studs flat. Velocity is the
  wrong tool for sustained flight — the humanoid damper zeroes it every step —
  but the right one for a hammered jump (see infinite jump). Steering is
  WASD/space/ctrl relative to the camera, integrated with the real elapsed
  clock; gated on text capture, not menu-open. The lifecycle survives cache
  hiccups (a miss is tolerated — the armed hold keeps hammering the last good
  address while steering continues), stops after 500ms genuinely unresolved,
  and re-seeds onto the new body on a real respawn instead of yanking it to the
  old coordinates. While flying, space belongs to flight and infinite jump
  stands down (a pinned vy would fight the position hold). It no longer touches
  `World::Gravity` or collision flags (noclip owns those).
- **Infinite jump** is one **autonomous hammered-velocity burst per space press** — the debug test (RunTest id 10) made structural. Each rising edge of space captures the primitive address and fires `mover::VelocityBurst(prim, {vx, power, vz}, 400)`: the dedicated hold thread hammers `velocity.y = power` into both motion blocks in a tight loop until its own 400ms deadline, then stops by itself. The window never truncates on key-up (a tap gets the full burst, like one click of the test button), and a new press — mid-air, mid-window — restarts the clock, so clicks chain bursts into a continuous climb (+30–34 studs per click at power 100, measured grounded and airborne).
  **Why it is deliberately NOT routed through mover ownership:** `Tick()`'s respawn guard demolishes every hold and owner whenever one cache pass returns without a local primitive — and `fetch_local()` publishes `hrp_primitive = 0` if *any* of its ~20 chained reads fails that pass, an every-second-or-so event under load. A 400ms burst needs ~25 consecutive clean passes and died within milliseconds, every time, under every previous mechanism (impulse, 90ms hammer, hold-to-rise, click-window) — which is exactly why "the test always works but the feature never does". The burst is immune the same way the test is: it carries its own address and clock. The feature watches from its own tick and kills the burst only for a *real* respawn (a different **valid** primitive) or a body unresolved >200ms (detach). Each burst leaves one rate-limited summary line in the log (writes, velocity, climb), so a failure names itself. Horizontal velocity is captured at press so a running start carries; `JumpPower`/`UseJumpPower` are written once per burst so the engine's grounded jump agrees with the slider.
  ⚠️ `Humanoid::FloorMaterial` reads **`Enum.Material.Air` = 1792**, not 0, when
  airborne — a Roblox enum property cannot hold an empty value. Testing `!= 0`
  reports every real floor as airborne. Added as `Offsets::MaterialEnum::Air`.
- **Teleport / waypoints** no longer anchor anything, hold for 400ms (~24
  steps) of hammered tail-only writes instead of 280ms of flooding, zero
  velocity each of those steps so gravity does not accumulate during the
  settle, and tolerate transient cache misses (only >250ms sustained absence
  ends the hold early). `HoldPosition` returns whether it armed, so a failed
  arm is reported instead of silently succeeding. `GotoWaypoint` now calls `WriteRootTo`
  rather than `TeleportToWorld`: a waypoint already *is* a root-part position, so
  the `tp_land_height` lift (which exists for landing on another player) was
  adding 2 studs of drift per round trip, each one a short fall the server had to
  reconcile.
- **Waypoint markers** now project through `features::ProjectToOverlay` (esp.h) —
  the app's one projection. Their private copy skipped the client-space →
  desktop-space transform, so every marker was off by the Roblox window origin
  and the DPI/render-scale factor unless the game happened to be borderless at
  desktop resolution.

### 4.1b Freecam — SOLVED (four separate bugs, none of them the CFrame write)

1. **Mouselook read the wrong thing.** It used `GetCursorPos()` deltas. Roblox
   **locks the cursor to the centre of its client area every frame** in
   first/third person, so those deltas measure the game's own re-centring, not
   your hand: ~zero while still, a large jump the instant anything moved it. The
   camera sat frozen then flung itself. Now it uses **raw input** (`WM_INPUT`,
   `input_mouse.h`), registered `RIDEV_INPUTSINK` so it arrives even though the
   overlay is `WS_EX_NOACTIVATE` and never focused. `RIDEV_NOLEGACY` is
   deliberately *not* set — it would suppress the ordinary mouse messages ImGui
   needs for the menu. Absolute-mode devices (tablets, VM/RDP mice) are
   differenced rather than treated as deltas. If registration fails, the log says
   so and freecam simply does not turn — it does **not** fall back to cursor
   deltas.
2. **~600 camera writes a second** with the same fake `dt`. Now real dt, one
   write per paced step.
3. **The basis collapsed at the poles.** `LookAtMatrix` builds
   `right = worldUp × fwd`, a zero-length cross product looking straight up or
   down, and a degenerate basis is a camera showing garbage. The matrix is now
   built straight from yaw/pitch — orthonormal and right-handed at every angle,
   pinned by test over 289 yaw/pitch combinations including the clamp.
4. **It never restored cleanly.** The character stood in the world still taking
   WASD from the game (fly the camera away, your body walks off), and a respawn
   mid-flight meant restoring into a dead instance. Freecam now claims the body
   through mover, which zeroes walkspeed for the duration and hands everything
   back on release — including automatically on respawn.

`Camera::Rotation (0xd8) + 36 == Camera::Position (0xfc)`, so the camera CFrame is
also one 48-byte write now (asserted at runtime, with a two-write fallback if an
offset update ever breaks the adjacency).

**Tests:** `glass-obsidian/tools/mover_pace_test.cpp` — 20 checks, no Windows and
no Roblox needed:
```
g++ -std=c++17 tools/mover_pace_test.cpp -o build/mover_pace_test && ./build/mover_pace_test
```
It pins the step rate across loop speeds, the stall/burst behaviour, the dt clamp,
that real-dt integration travels the requested studs/second at any loop rate, and
the freecam basis. Three of the checks are **regression guards** that assert the
*old* behaviour was wrong (fixed-dt overshoots ~10x; `worldUp × fwd` collapses at
the pole), so nobody quietly reintroduces them.

### 4.2 ESP boxes sit slightly high / float at distance
`Primitive::Size` and `Primitive::Rotation` are **confirmed valid** (the float probe shows
Size = (2,2,1) and a sane rotation matrix; `0x1bc`/`0xc8` where it was probed,
`0x1c4`/`0xb0` on the client this build ships for - the confirmation is of the pair, not
of the numbers). The box path still
**ignores Size/Rotation** and builds one world-space axis-aligned box per part from its
centre + canonical body size (`esp.cpp → CanonicalPartSize()`), then projects the box's
8 corners. Part data is confirmed correct, so the remaining suspect was the projection:
**ESP now builds its clip matrix from the camera itself** (Position/Rotation/FOV — the
offsets every other feature uses and that are confirmed working) instead of trusting
`VisualEngine::ViewMatrix (0x180)`, which was the thing making boxes drift off the
character with distance. Needs an in-game re-test.

### 4.3 FOV changer
Was writing **degrees** into a field Roblox stores in **radians** — 70 became ~4010°,
which wrapped the projection and flipped the screen. Now converts and sanity-checks the
range. Click teleport had the same bug and was fixed too. Both unverified.

### 4.4 Skybox changer
It was doing: invalidate → write textures → set `SkyValid`/`LightingValid` back to **true**,
which tells the renderer its cached sky is still valid so it never re-uploads. Now it
invalidates *after* writing, **and** it re-applies the six face strings every 2.5s while
enabled (a core script or the renderer reverting the sky can no longer permanently undo
it). Still unverified in-game. It now also **reads each face string back after
writing** and reports `WROTE but N/6 faces read back wrong (offsets stale?)` if the
writes don't actually land, so the world tab distinguishes a bad offset from a renderer
caching problem. The world tab prints a live status string (`skybox_debug_msg`) — read
it: `No Sky in game`, `FAIL: RenderView invalid`, `Skybox applied (...)` each point
somewhere different.

---

### 4.5 Three "the menu is lying" bugs found while adding the world page

- **The rail's GAME row said "Ugc".** `game::GetGameName()` was `dm.get_name()`, and
  the datamodel's *instance name* is the DataModel kind (`"Ugc"`, `"Game"`, ...) -
  not the experience's name, never was. There is no memory location holding the name
  the website shows. It now comes from
  `games.roblox.com/v1/games/multiget-place-details?placeIds=<place id>`, fetched
  once per place by `features/servers`, and the row is **absent** until it answers:
  an honest blank beats a confident wrong word. Anyone tempted to read a name out of
  the datamodel again should note that `get_name()` is correct *as a name of an
  instance* - it is only useless as a name of a *game*.
- **`DataModel::JobId` (0x120 on the current client, 0x118 on the one this was found
  on) is trusted only after it is argued with.** The value is
  required to look like a guid (36 hex, dashes at 8/13/18/23) *and* to match one of
  the ids the server list returned. Fail either test and REJOIN is disabled with the
  reason, because the three ways this goes wrong - nothing cached yet, an offset that
  moved, or you are in a private/reserved server that is not in the public list -
  need different fixes and only one of them is ours. A stale offset producing a
  plausible-looking link to a stranger's server is the failure mode worth more than
  any amount of convenience.
- **Fetching the list crashed the overlay, and it took a *full page* to do it.** The
  snapshot copy in `menu.cpp` was written as `s.servers[s.server_count]` - an append
  idiom copied from `FillPlayers`, where the index *is* the running count and is
  incremented at the bottom of the loop. Here `server_count` was computed once, before
  the loop, so every iteration wrote `servers[n]`. With fewer than 24 rows that is the
  unused slot after the list, which is why the card drew empty rows and nothing more;
  with a full page (any popular game, since the API page is 100 and the card holds 24),
  `servers[24]` is 48 bytes *past the end of the array*, and the fields that live there
  are `server_count`, `server_current`, the two gates and the head of `servers_msg`. So
  `server_count` stopped being a number and became the ASCII of a guid - around 870
  million - and `menu_ui`'s row loop runs `for (i < snap.server_count)`: an
  out-of-bounds walk tens of gigabytes wide, one frame after the reply landed.
  Reproducible on any busy server, invisible on an empty one.

  Three rules came out of it. The copy moved into `menuui::FillServers`, a function the
  host harness can call with a View that lies; it clamps `count` to `kMaxServers` and
  `current` into the list. `menu_ui`'s draw loop clamps to the array *again*, from its
  own knowledge, because a count the UI cannot check is not a bound. And the worker's
  out-tray now shares the View's mutex, rows built locally and swapped in under it - the
  same shape of bug one level down, where a half-copied `Row` is a guid with no NUL in
  it and `ShortGuid` opens with `strlen`.

  Two more things moved while this was being fixed, both in the same direction: the
  render thread and the request slot should not be doing work nobody asked them to do.
  Pressing FETCH no longer reads the datamodel chain on the render thread (the worker
  reads it, and says so in the tray when there is no experience to ask about), and the
  hop is no longer waited for by a second thread polling memory every 100ms for five
  seconds with `g_busy` held - which is what made the card say FETCHING for five seconds
  after a REFRESH that had already finished. `RunLaunch` opens the link and returns;
  `Tick()`, which reads that same chain once a second anyway, compares the job id and
  says whether the client moved. And because the failure that actually bites is a *dead*
  job id - public servers last minutes, and the client will sit on its join screen rather
  than say so - `ListUsable` refuses to hand a link over from a list older than three
  minutes, fires a refresh instead, and states which of the two it did.

### 4.6 Team check (recolor, not hide) + wall check vs real geometry

Both toggles had a honesty problem found while answering "they don't work":

**Team check worked, but did the wrong thing.** The cache *erased* teammates
from the ESP/skeleton snapshots, so teammates' boxes vanished instead of
being marked - the ESP looked broken, and there was no colour to tell friend
from foe at a glance. Teammates are now flagged (`is_teammate` on the
snapshot entities, still read from `Player::Team` instance-pointer equality)
and *drawn*: box outline, name, skeleton bones and chams render in
`team_esp_color` (config-persisted, colour picker on the visuals page; the
whitelist colour still wins - an explicit per-player choice beats a team-wide
tint). The aimbot skips flagged teammates at target-search time (both the
skeleton pass and the ESP fallback pass) and drops a sticky lock if team
check gets toggled mid-lock. In games with no Roblox `Team` objects the flag
is simply false for everyone - team check cannot work there by principle,
not by bug.

**Wall check could not have worked: it only ever tested other players'
bodies.** The comment said "we don't have the world primitives list offset" -
stale; `World::Primitives = 0x2b0` has been live-confirmed for a while. The
container layout at that offset is undocumented, so:

- `world_detect.h` (pure, unit-tested - `tools/world_detect_test.cpp`, 13
  checks against synthetic memory) tries two layouts - an inline
  `{begin,end}` vector and a vector behind one pointer - and accepts an
  interpretation only when the entry count is sane, sampled entries are
  canonical pointers, at least 3 sampled entries read believable
  Position/Size floats, and the count is within 2x of
  `DataModel::PrimitiveCount`. Anything else: `LAYOUT_NONE`, the wall check
  stays player-bodies-only, one log line says so, and the **world probe**
  (debug page) dumps the raw container so the layout can be pinned by hand.
- A dedicated slow sweep thread (never the 16ms cache thread - a 20k-part
  map takes real time) publishes axis-aligned boxes for every world
  primitive that is not a character part (character prisms are excluded:
  the target's own body would eat every ray; other players' bodies are
  checked live by the callers) and whose `CanQuery` flag is set (default
  `RaycastParams` semantics - CanQuery-clear parts are invisible to
  raycasts by design, e.g. foliage). Cadence is adaptive: 1.5s + 2ms/part.
- `LineOfSightClear` (aimbot) and `LOSClear` (ESP) test world boxes first,
  then live player bodies. A box that *contains* the aim point does not
  block - otherwise a target hugging a wall or standing in a doorway would
  be permanently unaimable.

Known limits, by design for now: rotated parts get their AABB (over-blocks a
little, never under-blocks), Roblox Terrain voxels are not primitives and do
not block, and sweep latency means a door that closed within the last ~2s
may not be in the list yet. The wall check row in the README stays
"Unverified" until the container layout is confirmed against a live game -
that is what the probe is for.

**The third filter: `dead_check`.** Asked for as "the option to not lock onto
dead players". There is no state machine in it, and that is the interesting
part: `Offsets::Humanoid::HumanoidState` + `HumanoidStateID` exist and are
refreshed live, but nothing in this repo has ever verified which internal id
`Dead` is, and guessing a magic number into an aimbot filter is how you get a
build that refuses to aim in one game and locks onto corpses in another. So the
predicate is the one the app already trusts for its "dead" label in the players
list - the Humanoid's own health - lifted out of `menu.cpp` into
`cache::EspEntity::IsDead()` so there is exactly one definition:

```cpp
bool IsDead() const { return max_health > 0.0f && health <= 0.0f; }
```

The `max_health` half is what makes it safe to use for aiming. The cache reads
health *only when it found a Humanoid child*, so a player whose humanoid could
not be read this pass has `0` in both fields - and `health <= 0` alone would
report the whole server as dead for one bad frame, releasing locks and emptying
the target list. Unknown is not dead; it is unknown, and `FindPartForPlayer`'s
own validation is what ends a lock on somebody who actually left.

With the switch on, a dead player is skipped in both search paths (skeleton and
ESP fallback), and - the half that matters most with sticky aim - a lock is
released the moment its target dies, because a corpse keeps its head part
readable and would otherwise be aimed at until it despawns. `tools/dead_check_test.cpp`
pins the predicate (14 checks, no Windows, no game): `g++ -std=c++17 -O1
-I"../roblox_external" tools/dead_check_test.cpp -o build/dead_check_test`.

### 4.7 A font re-bake has to re-create the renderer's font texture (found by the screenshots)

`ui_scale`, `ui_font_size` and `ui_title_size` are live settings, and the only legal
place to act on them is between frames — the atlas is locked inside one, which is what
`menuui::SyncTheme()` exists for (`ui/menu_ui.cpp`). What used to be missing is the
second half of the rule: `obs::LoadFonts()` calls `ImFontAtlas::Clear()`, which frees
the atlas buffer and zeroes `TexID`, while the DX11 backend creates its font texture
only in `ImGui_ImplDX11_CreateDeviceObjects()` and leaves `io.Fonts->TexID` pointing at
the view it made there. Re-bake without telling the backend and the overlay keeps
sampling the bake that no longer exists — every glyph after a scale change is stale.
So `overlay.cpp` does:

```cpp
if (menuui::SyncTheme())                      // true = the atlas was rebuilt
{
    ImGui_ImplDX11_InvalidateDeviceObjects(); // releases the old view, clears TexID
    ImGui_ImplDX11_CreateDeviceObjects();     // re-uploads, re-tags
}
```

The headless harness has the same requirement and had the same bug: its software
rasteriser holds a pointer into ImGui's atlas, and re-pulling it *after* `Render()` is
one frame too late — the commands' `TextureId` was already taken from the zeroed atlas,
so a captured shot came out as a page of solid rectangles (a draw command with no
texture id is rasterised as flat vertex colour). `App::Frame()` now re-tags and
re-pulls at the top of every frame, which is also why the scale test can be followed by
a screenshot of the result.

## 5. Offsets — important

`offsets.h` carries the dump for `version-4310300497aa4917` (RbxDumperV2 2.2.4,
15/09/2026), cross-verified between offsets.imtheo.lol, cheatoffsets.com and the
`Offsetmanager/Roblox-Internal-External-Offsets` GitHub mirror (all three agree on
every value the tool reads; 392 offsets in the dump, 399 in the app's list).

30 values moved since `version-c5aecda2245e4fae` - 25 from the dump, 5 derived - and
348 did not, which is what a Roblox release usually looks like from the outside: the
datamodel and physics chains shift by a pointer's worth and most property offsets do
not move at all.

That file is **merged, never pasted**. It is not the dumper's field list: it is ~20
fields longer (the derived second motion block, the mesh-cache classes), and every
value is a mutable `inline uintptr_t` where a dumper writes `constexpr`, because
`RefreshOffsets()` writes into them at launch. A paste is the same wire format the
`Offsetmanager` mirror serves, so it reads as legitimate and then breaks three ways at
once: the fields no dumper emits vanish (three translation units stop compiling), the
rest turn `constexpr` (the live fetch cannot take their address - `offsets_live.cpp`
now `static_assert`s exactly that), and the `0x0` a dumper writes for "not found" lands
where a real offset was, turning a read at a known address into a read at `address + 0`.

    python3 glass-obsidian/tools/offsets_regen.py <their_header.h> --write

reports every one of those differences instead of shrugging at them, derives block B from
block A + stride *only* after the dump's own intra-block deltas confirm the model, and
regenerates `offsets_fallback.json` from the result so the shipped fallback cannot drift
from the header. `docs/offsets_dump.txt` stays the raw dump, so any merge can be checked
again against its source.

### Where offsets come from at launch — `RefreshOffsets()`

offsets.imtheo.lol is **behind Cloudflare bot protection** that drops WinHTTP's
TLS ClientHello (win32 12175 on `WinHttpSendRequest`, before any HTTP). That is
server-side blocking, not the ntdll unhook — the fetch already runs first in
`WinMain` and still failed. So the launch chain is now, in order:

- **version**: imtheo → `ltseverydayyou/rdd` (GitHub raw, current version hash) →
  the WEAO Pages mirror → imtheo via the r.jina.ai reader proxy;
- **offsets**: imtheo's **pinned** `/{version}/offsets.json` (the path the
  service's own docs — `/docs/cpp-auto-update` — document for deterministic
  fetches; no race between the version check and the dump) → plain `/offsets.json`
  (latest) → `Offsetmanager` `External_Offsets.hpp` for the live version hash
  (GitHub raw, updated minutes after each Roblox release, parsed by
  `ApplyOffsetsHpp` — same wire format as `offsets.h` itself) → imtheo via jina;
- imtheo requests present the UA its docs send (`OffsetsClient/1.0`) — if their
  Cloudflare config ever carves out documented clients, that is the identity to
  be wearing;
- every URL is fetched **WinHTTP first, then `System32\curl.exe`**
  (`GetAny`/`GetViaCurl`): curl's Schannel ClientHello is a different
  fingerprint and passes where WinHTTP's is dropped;
- when WinHTTP fails with 12175, the log now carries the decoded
  `WINHTTP_OPTION_SECURITY_FLAGS`: "untrusted certificate chain" means something
  **on the machine** re-signed the TLS (AV / corporate proxy / broken root
  store), "the channel was cut during the handshake" means the **remote** dropped
  the hello (bot filter / IP block). Before this, both looked like the same
  bare `12175`;
- then the on-disk cache (`offsets.json` + `offsets.version` next to the exe,
  used only when saved for the live version), then **`offsets_fallback.json` —
  the full dump shipped with the build** (it is a `Content` item in the vcxproj
  now; for a long time it sat in the repo unshipped, so the fallback path was
  dead code and a stale-cache situation looked like "attached but every read
  fails"), then a knowingly-stale cache with a loud log line, then compiled
  defaults.

When the live version is known but the applied dump is for another one, the log
says so (`WARNING - applied X but the live client is Y`) — silent wrong offsets
are exactly what the 600× `0x8000000D` read-fail storm looked like. A log is not where a
user looks, so the debug page now shows the same pair as the **first row of its runtime
card** (`OffsetsLoadedVersion()` / `OffsetsClientVersion()`, snapshotted by
`RefreshOffsets()`): green when they agree, red reading `CLIENT IS <hash>` when they do
not, dim with `(client unknown)` when no version source answered. Every pointer row
underneath is only interpretable against that row, which is why it is first.

**Known-wrong in the older `f5a60436` dump, still worth remembering:**
- `Humanoid::HumanoidRootPart` read `0x0` back then (`0x470` now); unused — name
  lookup is used instead.
- `Primitive::Position (0xd4)` / `Rotation (0xb0)` / `Size (0x1c4)` and both velocities
  are right in shape per three independent float probes. The numbers are per-release
  (`0xec`/`0xc8`/`0x1bc` on `f5a60436`, `0xd4`/`0xb0`/`0x1c4` now); what has never been
  wrong is the layout: the primitive holds **two 0x48 motion blocks** (rot/pos/lin/ang),
  and block B carries a LIVE velocity the code used to skip. See **§4.0** - read it
  before changing anything here. Block B's four offsets are published by nobody, so
  `offsets.h` *derives* them as block A + stride; the regen tool checks the dump's own
  intra-block deltas against that model first and refuses to derive anything when they
  stop matching, which is the closest thing to verification available without a client.

**Entries the current dump gives as `0x0`** (the dumper can't resolve them - 0 is its
"not found", not an offset). The previous cross-checked values are kept rather than
zeroed, which is the same rule the live apply uses, and `offsets.h` tags each line. Two
of the six are load-bearing in a way worth stating: the skybox changer *writes*
`RenderView::SkyValid` / `LightingValid` to invalidate the cached sky, guarded only by
the target address being mapped - so if RenderView's layout moved and no dump can say
whether it did, the result is a byte flipped somewhere inside the renderer rather than a
crash or a garbage read. `MouseService::SensitivityPointer`, `GuiBase2D::AbsoluteSize`
and RenderView's other two are not referenced from feature code at all.

**The mesh-cache classes were restructured by the dumper**: `MeshContentProvider`
now walks `LRUHolder → MemEnforcedLRUCache → Head` with `LRUNode`/`CachedItem`/
`FileMeshData` alongside. The old `MeshContentProvider::{Cache, LRUCache, MeshData,
ToMeshData}` entries in `offsets.h` are kept for reference - nothing in feature code
consumes them, they exist only in the live parser's slot table - and the new classes are
not wired up either. `offsets_regen.py` leaves both alone and says so: a dumper renaming
a chain the app does not read is not evidence about a chain it might want.

### ⚠️ The `NameContainer` gotcha — read before updating offsets
The dump splits the instance name into **two** offsets:
```
Instance::NameContainer = 0x70
Instance::Name          = 0x8
```
`get_name()` must therefore do a **two-step** read (same pattern as `get_class_name`):
```cpp
uintptr_t container = read<uintptr_t>(address + Offsets::Instance::NameContainer);
return fetchstring(container + Offsets::Instance::Name);
```
Older dumps had a single absolute `Instance::Name = 0xb0`. Getting this wrong silently
returns **garbage names**, which breaks every name-based lookup — player detection, body
parts, skeletons, the root part — while class-name lookups keep working. That mismatch
caused a long stretch of "nothing works but walkspeed". Fixed in `8906f70`.

`docs/offsets_dump.txt` is the historical flat dump from the `f5a60436` era, kept
for the diff trail. The current full dump ships as `roblox_external/offsets_fallback.json`
(imtheo JSON format - 70 classes / 399
fields for `version-4310300497aa4917`, regenerated from `offsets.h` by the same tool that
merges a dump into it). Both parsers — the JSON one and the
Offsetmanager-header one — plus the version extractor and the whole source/cache/
fallback decision tree are pinned by `glass-obsidian/tools/offsets_apply_test.cpp`
(63 checks) and `tools/offsets_flow_test.cpp` (30 checks), which include the real
`offsets_live.cpp` with a canned network rather than a copy of the code. The apply test
also reads `offsets.h` as *text*, which is where a paste gets caught instead of argued
about: no `constexpr uintptr_t` anywhere in it, the fields a dumper does not emit still
declared, and the same version string as the shipped fallback.

---

## 6. Diagnostics built in

- **debug page** — base address, VisualEngine, DataModel, game name, place id, game
  loaded, viewport, view matrix, local player, humanoid, hrp primitive, position,
  cached player count, and three **write test** buttons (position, velocity, probe).
  It is two stat cards in the new grid; `menuui::Snapshot` carries the strings, so
  the UI never reads memory and the page can be screenshotted without the game.
- **the menu itself** — `glass-obsidian/tools/menu_shots.cpp` renders all nine pages
  headlessly (no GPU, no Windows, no Roblox), plus two states a page loop cannot
  reach (`00-detached`: not attached, rail reduced to its status chip; `00-faded`:
  transparency at 90%). It asserts the pages exist and are named, that no surface is
  painted outside its own clip, and that the **clip-intersected** extents of every
  command stay within 2px of the window rect — i.e. that the menu puts no visible
  pixel outside itself. Run it after any layout change and look at the PNGs; §10 has
  the three real bugs the last run caught. It is also where the snapshot's *data*
  contracts live, being the ones no screenshot can show: `menuui::FillServers` is called
  with a View claiming 9999 rows and an out-of-range `current`, and the assertions are
  that the count clamps, that each row lands at its own index (fixtures whose *tail*
  carries the index, because the tail is the half `ShortGuid` keeps), that
  `server_count` / `server_current` / `servers_msg` behind the array come out unharmed,
  and that an unchanged list is not reformatted every frame - proven by scribbling a row
  and requiring the scribble to survive until the message changes. 64 checks. The committed PNGs (`preview/img/menu/`,
  `out/`) are those renders re-packed by `tools/encode_shots.py`, losslessly — check
  with `compare -metric RMSE build/shots/01-overview.png out/01-overview.png null:`
  which must read `0 (0)`. If that script is missing, whatever shots are committed are
  not evidence of anything.
- **click-tp geometry** — `features/click_teleport/tp_math.h` is the ray/plane/box
  math with no Win32 and no RPM in it, so it has a host test:
  `g++ -std=c++17 -I"../roblox_external" tools/tp_math_test.cpp` then
  `./build/tp_math_test` (16 checks). The important one round-trips the cursor ray
  against `esp.cpp`'s own `WorldToScreen` — copied into the test — for ~1500
  pixel/point combinations, which pins "the pixel you clicked" to "the pixel ESP drew
  a box at". If either projection changes, that is where you find out.
- **the movement pacing** - `glass-obsidian/tools/mover_pace_test.cpp` (20 checks):
  `g++ -std=c++17 tools/mover_pace_test.cpp -o build/mover_pace_test` then
  `./build/mover_pace_test`. mover.cpp itself cannot run here (it reads another
  process) but its two former bugs are pure arithmetic: the 60Hz step gate and the
  dt clamp. It pins the step rate across loop rates from 100Hz to 2000Hz, that a
  stall resyncs rather than firing a catch-up burst, that real-dt integration
  travels the requested studs/second at any loop rate, and that the freecam basis
  stays orthonormal at the poles. Three checks assert the OLD behaviour was wrong,
  so the fixed dt and the `worldUp x fwd` look-at cannot come back unnoticed.
- **the server-hop reply readers** — `features/servers/servers_json.h` parses both
  Roblox replies with no Win32 and no memory reads, so it has a host test like the
  click-tp math: `g++ -std=c++17 -I"../roblox_external" tools/servers_json_test.cpp`
  then `./build/servers_json_test` (33 checks). It pins the parts that a real client
  cannot tell you apart: that a nested `players[]` array does not split an entry in
  half, that an entry with no `playing` is dropped rather than shown as `0/40`
  (the HOP button picks on that number), that a truncated reply says "cut off"
  instead of reading as "no servers", and that `"notplaying":5` is not a match for
  the key `playing`.
- **log tab** — in-GUI log (replaces the old console), 200-line ring buffer, `LogLine(...)`.
  Click-teleport reports every refusal *and* every jump through it, so "I clicked and
  nothing happened" names its cause instead of being guessed at.
- **world tab** — live skybox status string.
- "not in a game" warning when `place id == 0` (home page has no world/players).

---

## 7. Performance work already done

- **Zero-copy cache snapshots.** `GetEspEntities()` returned `EspEntity` **by value** from
  9 call sites/frame — ~1.4 MB of memcpy per frame at 30 players. Now
  `shared_ptr<const vector<...>>`.
- **`EspEntity` shrunk 5,320 → 2,760 bytes** (`part_names[64][64]` → `[64][24]`).
- **Batched child reads** — one `ReadProcessMemory` for the whole child array instead of
  one syscall per child.
- **Class-name cache** keyed by descriptor pointer (descriptors are stable, shared per class).
- **Single-pass traversal** — `fetch_entities` walked each child list 3× per player, now 1×.
- **`read_string_raw`** tries 128→64→32→16 bytes; a fixed 200-byte read fails entirely if
  it crosses into unmapped memory, which silently blanked names near page boundaries.
- **Release flags:** `/Ox`, Speed, AnySuitable inlining, omit frame pointers, string pooling,
  fast FP, AVX2, `/MP`, LTCG, `OptimizeReferences`, `COMDATFolding`.
- **Vendor tree slimming.** The whole `Clipper2/` tree (C++ lib + C# + Delphi + DLL
  wrappers + tests/examples) was removed — ~2.7 MB / 230 files across four languages —
  along with the dead `DrawMergedPoly` that pulled it in. Dead `esp.cpp` helpers
  (convex hull, R15 chain lookup, mesh-logging sets) and ~20 unused globals in
  `globals.h` were also deleted. `offsets.h` dropped two junk entries not present in
  the dump (`Humanoid::PlatformStatePointer = 0xb9fe4b32`, `Instance::Attribute*`).

> On "use a faster language": C++ is already correct here. The workload is **syscall-bound**
> (`ReadProcessMemory` ≈ 1–3µs each), not CPU-bound. Rust/C/asm would make identical
> syscalls at identical speed. Wins come from *fewer, larger* reads and less copying.

---

## 8. Malware audit (done at the start — clean)

No RAT/backdoor/stealer/dropper/persistence. Checked: outbound endpoints, HTTP verbs,
injection APIs, persistence, credential theft, encoded payloads, build-event hijacking,
binary artifacts. Only "risky-looking" APIs are inherent to the design:
`WriteProcessMemory` into Roblox, and `ShellExecuteA` to open the config folder.

Since the mesh backend was deleted, the project makes **no outbound network requests at all**.

---

## 9. Housekeeping / possible next cleanups

- ~~Rename namespace `discord_overlay`~~ → done, it's `overlay` now.
- ~~`overlay.h` shim~~ → folded away, include `overlay.h/.cpp`.
- ~~`.vcxproj.filters`~~ → regenerated (imgui + per-feature folders).
- ~~Clipper2 vendor tree~~ → removed entirely (see §7). The only user
  (`DrawMergedPoly` in esp.cpp) was dead code left over from the mesh backend.
- **`roblox_external/process.h` shadows the CRT's `<process.h>`**, because
  `AdditionalIncludeDirectories` starts with `$(ProjectDir)`. Anything in the standard
  library that does `#include <process.h>` gets *this* file instead: MSVC's `<thread>`
  does, needs `_beginthreadex`, and reports `C2039` + `C3861` at
  `VC/Tools/MSVC/<ver>/include/thread:79` - two errors in a file nobody can edit, with
  no mention of the project header that caused them. The clean cure is a rename
  (`process.h`/`process.cpp` -> `roblox_process.*`: 6 TUs include it, plus the vcxproj
  and its filters), which would also make `std::thread`, `std::async` and `<future>`
  usable here. Until then `check_win_tu.sh` refuses `#include <thread>` /
  `std::thread` / `std::async` in the app with a message saying why, and background
  work uses `CreateThread` like `main.cpp`, `cache.cpp` and `features/servers` do.
  (Also worth renaming for the same reason: nothing else - `memory.h` has no CRT
  counterpart, so it is safe.)
- `tools/check_win_tu.sh` now also parses `features/servers/servers_http.cpp`, and
  the stub folder grew `winhttp.h` + `shellapi.h` for it (plus the wide-string,
  `GetExitCodeProcess`, `PostMessageW` and `ULONG_PTR` bits in `Windows.h`). That
  caught five MSVC-only mistakes the first real build would have hit, and it is the
  pattern to follow: **when a host TU cannot be compiled here, give the stub the real
  type names rather than convenient ones** - `URL_COMPONENTSW` (there is no
  `WINHTTP_URL_COMPONENTS`), `WinHttpSetOption(..., LPVOID, ...)` (so `&const`
  fails), `SE_ERR_FNF` (there is no `SE_ERR_FNMF`), and `WinHttpOpenRequest`'s real
  seven arguments with no version slot (the off-by-one handed `WINHTTP_NO_REFERER` to
  a `DWORD`). A stub that invents a comfortable alias proves nothing.
- The `#define WIN32_LEAN_AND_MEAN` trap: with it, MSVC's `<thread>` reaches
  `_beginthreadex` with no declaration and fails *inside the standard header* (C3861 +
  C2039). No other TU in this project defines it, so neither does
  `servers_http.cpp` - see the comment at its includes. Anyone adding it later gets a
  confusing error in a Microsoft file, which is a bad afternoon.
- The proof-shot encoder lives in the repo (`glass-obsidian/tools/encode_shots.py`).
  It used to be a scratch-dir script; the sandbox was rebuilt, the script vanished, and
  `out/` was left holding whatever a stale render had written - shots that looked
  current and weren't. A pipeline that disappears silently is worse than none.
- ~~Uncompiled vendored ImGui extras~~ → removed: `imgui_demo.cpp`, `imadd.cpp`,
  `TextEditor.*`, `imgui_toggle*`, `addons/`, `includes.h`, `imgui_offset_rect.h`,
  `misc/freetype/`. The whole repo is now C/C++ only and the ImGui tree contains
  exactly what the project compiles.


---

## 10. The platform seam — `platform/platform.h` (2026-09)

Two goals, one seam. The app was written as a Windows program with a Linux
port "some day", and every `GetAsyncKeyState` / `Sleep` / `CreateThread` in a
feature file was both a port blocker and an import-table string a signature
scanner can anchor on. Both are cured by the same move: **features and core
code call `platform::` only.**

### The surface

`platform/platform.h` includes nothing but `<cstdint> <cstddef> <string>`:

- **boot/process**: `Init()` (wraps `nt::Init()` on Windows), `FindGameProcess`,
  `GameBaseAddress`, `OpenGameHandle`, `SetGamePid`, `ProcessAlive`,
  `CloseHandle`, `ReadMem`, `WriteMem`, `AllocMem`, `FreeMem`
- **time**: `NowMs` / `NowNs` — `uint64_t` everywhere, no DWORD stamps
- **threads**: `StartThread` (fire-and-forget, `void fn(void*)`), 
  `StartJoinableThread`/`JoinThread(ms)` (cache.cpp), `BoostThread`, `YieldThread`,
  `SleepMs`
- **input**: `key::*` constants + `SampleKey`/`IsKeyDown`/`KeyPressed`,
  `MoveMouseRelative`, `SetMouseScreen`, `CursorScreen`, `CursorClient`,
  `GameClientRect`, `ScreenDpi`
- **shell**: `ExeDir`, `CurlPath`, `OpenUrl` (the `SE_ERR_*` zoo lives in
  `platform_win.cpp`, not in callers)

`platform/platform_win.cpp` is the real implementation (thin, over
`nt_syscalls` + Win32); `platform/platform_linux.cpp` is the deliberately
minimal starting point — it compiles clean with `-Wall -Wextra` and is
intentionally **not** in the `.vcxproj`. Note the one honest asymmetry:
`AllocMem` has no Linux implementation (no remote allocator on this side of
`process_vm_writev`), so skybox strings — the only caller — stay
Windows-only until the port grows one; `platform_linux` refuses and the
feature no-ops, it does not pretend.

`key::*` values are **VK numbers**, not an enum of our own: saved configs
store keybind ids and must keep loading. `LBUTTON=0x01 … RCONTROL=0xA3`.

### Who may still include `<Windows.h>`

The files that own it are the ones that *are* the Windows layer:

- `main.cpp` (WinMain), `overlay.cpp/.h` + `menu.cpp` (Win32 UI: window,
  tray, keybind capture), `media_tracker.cpp` + `smtc_raw.h` (SMTC via raw
  COM), `config.cpp` (`FindFirstFileA` enumeration), `servers_http.cpp`
  (WinHTTP + curl transports), `nt_syscalls.*`, `platform/platform_win.cpp`.

Everything else under `roblox_external/` — all 19 feature TUs, `memory`,
`process`, `game`, `cache`, `offsets_live` — is Windows.h-free, and the
26-TU `check_win_tu.sh` pass keeps it that way (the list covers every TU
in the app except `main.cpp`, `overlay.cpp` and `media_tracker.cpp`, which
need the D3D/COM headers the stub does not carry; `cache.cpp` parses with
the project's own `media_tracker.h`, which is self-contained raw COM).

### Lessons the sweep charged for

- **Never name platform constants after Win32 macros.** `key::XBUTTON1`
  collided with `#define XBUTTON1 1` — in the *stub* headers too — and took
  down four TUs until the rename to `MOUSE_X1`/`MOUSE_X2`. The same class
  bit again on a real MSVC build: `key::DELETE` met `winnt.h`'s
  `#define DELETE (0x00010000L)` (the access right) and every
  Windows.h-first TU got C2059 at the declaration. The stub now carries the
  real bare macros (winnt.h access rights, wingdi.h `TRANSPARENT`/`OPAQUE`/
  `ERROR`, `XBUTTON1/2`) so the parse check fails here first — which is how
  it must work: a stub missing the hostile macro proves nothing.
- **Converting lock scopes leaks reads.** Five mover.cpp variables were
  declared inside a `{ lock_guard }` block and read after the closing brace;
  the parse check caught all five. When Enter/Leave pairs become scopes,
  audit every variable that crosses the new brace.
- The stub folder must carry the *real* Win32 names (`BYTE`,
  `WideCharToMultiByte`; `_wcsnicmp` is an MSVC-only CRT name) — and grep
  before adding, or you redeclare an `IsWindow` that was already there.

### Detection surface (the other half of the job)

Everything an external scanner can enumerate about this tool now lives in
exactly one place per name: window class `GlassWidgetWnd`, window name
`GlassWidget` and taskbar class `GlassWidgetApp` are statics in
`overlay.cpp`; the tray tip and taskbar caption ("Glass Widget") are set in
the same file; the exe is `external.exe` (vcxproj `TargetName` - deliberately
generic, and independent of the project's own name, so the file on disk says
nothing either way); the
main.cpp log lines no longer mention the game ("attached - pid %u, base
0x%llx", "waiting for the game..."). Process and memory access was already
indirect-syscall-only through `nt_syscalls` (fresh ntdll from disk); this
pass did not touch that, per the standing constraint.

### Tests after the sweep

`check_win_tu.sh` default list covers **the whole parseable app**: 26
TUs — all 19 features plus menu, config-side, offsets_live, memory,
process, game, cache (via the media stub) and platform_win. Extending it from the original 14 is what
caught the two files the hand-sweep missed: `aimbot.cpp` still had six
`DWORD` timestamp stamps (a cascade: `DWORD now = platform::NowMs()` kills
the `now` declaration too) and `esp.cpp` still called MSVC-only
`sprintf_s` twice. Both fixed (`uint64_t`, `snprintf`).
`offsets_apply_test` and `offsets_flow_test` stub `platform::ExeDir() -> "./"`
and `Init() -> true` so they still run the real `RefreshOffsets` pipeline in
a scratch CWD. 25/25 parse + win32-glue parse; 47 / 30 / 33 / 16 / 243
checks; menu_shots renders clean. The 243-check mover pace test is the
proof the seam changed no timing: same 250µs write period, same 10/20kHz
read pacing, same 500µs echo cap.
