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
- **the menu itself** — `glass-obsidian/tools/menu_shots.cpp` renders all ten pages
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

---

## 11. The agent layer — `agent/context.ts` (2026-10)

This repo carries the operating layer from
[Glockerz/Repo-Template](https://github.com/Glockerz/Repo-Template) — doctrine, context,
plans, memory, and a capability-verified single-command context loader — integrated as a
**seam**, not as a second system. Nothing under `roblox_external/` is part of it; the
window is not a preview host and `node` is not a build dependency of the exe.

### The surface

```bash
node agent/context.ts [--note "..."] [--quiet] [--json] [--probe] [--checks[=full]]
```

Zero dependencies (Node ≥ 22.18 executes the `.ts` by type stripping). It prints a brief:
environment + git, the tree, the doctrine/context/plans dump, the host invariants, the
short-term session memory, and a problem-solving board whose `objective` and `next
actions` are read from `ToDo.md`'s open `- [ ]` items. Every non-`--json` pass appends an
entry to `agent/memory/session.jsonl` and mirrors the last 12 into `ToDo.md`.

### Why the checks live in the passthrough

This repo already had the habit the doctrine asks for — `check_win_tu.sh`,
`mover_pace_test.cpp`, the headless harnesses — so the layer extends it instead of
inventing a workflow. The five fast invariants run on **every** pass (~50 ms, printed in
every brief, exit code 2 if any fails):

| Check | Rule it enforces |
|---|---|
| `windows-h-containment` | `<Windows.h>` only in the 10 documented includers of §10; a feature reaching for Win32 fails here rather than on the SDK machine |
| `no-winapi-read-write` | the `nt_syscalls.h` tripwires are the only textual hits; reads/writes are `nt::ReadVirtualMemory` / `WriteVirtualMemory` |
| `offsets-parity` | `offsets_fallback.json` parses and names the same client version as `offsets.h` (drift bit this repo twice) |
| `no-deprecated-crt` | no CRT call MSVC flags as unsafe (C4996: `strncat`, `strcpy`, `sprintf`, `fopen`, `getenv`, ...) outside the Linux platform stub — a `strncat` here stopped a Windows build once, and defining `_CRT_SECURE_NO_WARNINGS` would have hidden the next one (§13 addendum) |
| `required-files` | the layer and the host's load-bearing files still exist |

`--checks=full` adds the ones that need a compiler: `check_win_tu.sh` (39 TUs now), the
library's headless harness (13/13 invariants + 13 PNGs), the host app's own geometry+rule
suite as `features-geometry` (184 cases: wall-check geometry, draw range, FOV storage form,
save/restore, aim tracking, ballistics, flight's stealth rules, the frame-rate arithmetic,
the sky's arithmetic — §12 to §18) and `menu_shots` against the app's real
`ui/menu_ui.cpp` (82 checks, 39 PNGs, 0 commands outside their clip). Cold ≈120 s, warm
≈80 s on this sandbox; binaries cache under `<tmp>/roblox-external-checks/`, never in the
tree (§9's artifact rule, and the reason `glass-obsidian/out` is documented as the
deliberate exception). §12–§15 date the sections that grew it; §11's "three that need a
compiler" was the first version of this paragraph.

### The asymmetry it is built around

**This side can parse and reason; it cannot build or run.** There is no `msbuild`, no
`CMake` in the sandbox, and no client to attach to. So no check in the registry can only
answer "N/A", no doc claims a Windows verification, and `main.cpp` / `overlay.cpp` /
`media_tracker.cpp` remain checked on the Windows side by hand — that gap is recorded
rather than papered over (`agent/verification/CAPABILITIES.md`, `docs/CONTEXT.md`).

### What it deliberately does not do

It does not commit, push, merge or open PRs. It does not touch the game: nothing in
`agent/` opens a process, reads memory or makes a network request. It does not replace
this file — ARCHITECTURE is the record of *why the code is shaped this way*; the layer is
the record of *what the repo is* and *what the last session did*. And it does not prune
`session.jsonl`: a log that omits its author's mistakes is not a log.

---

## 12. One raycast, and the triggerbot on top of it (2026-10)

### The problem, measured

The wall check existed three times: `esp.cpp`'s `LOSClear`, and two layers inside
`aimbot.cpp`'s `LineOfSightClear` (world boxes, then player bodies). Each was a flat loop
over **every** published world box, each tested an axis-aligned box because that is what a
loop over `{cx,cy,cz,hx,hy,hz}` can do, and each carried its own idea of how big a part is
— the aimbot's copy and the ESP's copy disagreed about a head by a stud. On a map with
20k boxes, one ray walked all 20k. The benchmark in `glass-obsidian/tools/world_ray_test.cpp`
(20,001 boxes × 10,000 rays) is the receipt: **18.9 ms with the grid, 859 ms flat** —
1,951,320 box tests instead of 200,010,000.

### The shape of the fix

`features/raycast/world_ray.h` is **pure geometry**: no `<Windows.h>`, no ImGui, no
offsets table, no cache. Slab tests, an oriented-box test, and a uniform grid — so it can
be compiled and run on Linux, which is the only reason the numbers above exist.

* **Grid.** 64-stud cells, 21 bits per axis keyed from +2^20, oversized boxes in an
  `always` list (a map-sized floor plane cannot be filed into a cell), and an
  Amanatides–Woo walk with a step cap so an INF/NaN endpoint cannot spin forever. A ray
  visits the cells it crosses; `Grid::for_each_candidate` stops the moment the callback
  says hit.
* **Rotation.** A part with a usable rotation (`rotation_usable`: finite and orthonormal —
  Roblox's own padding matrix fails this on some parts) is tested as the oriented box it
  is; a part without one falls back to the AABB. The flat loops could only ever do the
  fallback, which is why a ramp used to block like a wall.
* **The delta-vs-endpoint footgun is inside the header, named.** `slab_hits(org, delta, …)`
  takes a *delta*, and passing two absolute points to it compiles and lies (it tests a
  segment from the origin — this produced two spurious test failures and a "hit" on an
  unrelated wall before it was caught). The parameter is called `delta` with a warning
  comment, and callers use the two-point wrappers `segment_hits` / `segment_hits_rotated`,
  which are finite-checked.

`features/raycast/world_ray.cpp` is the half that needs the game: it mirrors
`cache::GetWorldBoxes()` and rebuilds the grid **only when the cache publishes a new
snapshot pointer** (`uintptr_t primitive` rides along in `WorldBox` for exactly this), then
re-reads `Position` and `Rotation` per candidate, so a moving or turning part is tested
where it *is*, not where it was when the sweep ran. Bodies come next, at
`features::CanonicalPartSize` — the ESP's table, made public, with the aimbot's duplicate
deleted. `worldray::Blocked(from, to, skip_player)` is now the single wall check for the
ESP, the aimbot and the triggerbot; `WorldBlocked` is the world-only variant. Boxes that
contain the ray's endpoint do not block, which is the old aimbot rule kept at the layer
that needed it.

### Draw distance, in one place

`features/esp/range.h`:

```cpp
DrawRangeAllows(dist_studs, setting_studs)   // 0/negative/NaN setting = infinite
                                            // NaN distance = never drawn
```

That is the whole rule, and it is `--checks=full`-tested (§11's `features-geometry`, 38
cases). The ceiling is one constant, `esp_draw_distance_max = 25000` in `globals.h`,
because the draw-distance slider, the triggerbot's own max distance and the harness's
assertion all have to agree. Every pass asks it — aim viewer, skeleton, china hat, chams,
expanded hitbox, labels, tracers, main ESP — which is what stops the "it lingers past the
range" behaviour: the gate is evaluated per pass and per entity, never cached into an
entity's own state. Two consequences worth naming:

* `0` really is infinite: with `esp_render_distance = 0` nothing is filtered, so nametags
  and the distance text draw at 25k studs. They used to vanish there for an unrelated
  reason — `ComputeBoxForPrimitives` rejects a projection under ~2 px, and a character at
  25k studs is under 2 px. `LabelPoint` (live head, root fallback) plus a 3×5 `degenerate`
  marker replaces the rejected box, and the wall check now asks head *or* root.
* The range anchor is the local player, or the camera when the character has not been read
  yet, or — when neither exists — "draw": a filter with nothing to measure from must not
  silently delete every entity.

### The triggerbot

`features/triggerbot/` — ported from `jatosm/RobloxExternal-base`'s `aim/autoshoot`, which
is the feature this repo was missing. Once per 8 ms pass: the key (hold, or the aimbot's
latch), the shared filters (blacklist, team, dead, range), then the crosshair test — the
**body box the ESP is drawing** (`features::EntityScreenBox`, which owns the definition of
"that player's box", far-range marker included) **or** a radius around the aim part, since
at range the box is a few pixels; then `worldray::Blocked`; then the two timers
(`delay` on target, `cooldown` between shots); then one `platform::ClickMouseLeft()` —
down and up in a single `SendInput` batch, because two separate calls can be separated by
a frame and a one-frame hold reads as a drag. `chance` rolls per shot and a *missed* roll
still consumes the cooldown, or "70%" becomes "always, one pass later".

The crosshair is the centre of the **game's** client rect (`platform::GameClientRect`), not
of the desktop: in windowed mode those differ, which is the same distinction the ESP's
client→desktop transform exists for, and mixing the two is a bug this repo has already had
once.

**What it deliberately is not:** a hit-registration oracle. The reference repo reads hit
results through a remote hook it installs into the game's raycast path; that is a
different piece of machinery with a different risk profile (exec-cave thunk, CFG-safe
remote hook), and it is not ported. This fires on the same evidence a player has, which is
why its README row is ❓ and not ✅.

### What proves it, off Windows

| Evidence | Where |
| --- | --- |
| 38 geometry + range cases, grid-vs-flat benchmark | `glass-obsidian/tools/world_ray_test.cpp`, run as the `features-geometry` check |
| `-Wall -Wextra -fsyntax-only` on the TU, clean | same build line |
| the two new TUs parse against the stub `<Windows.h>` | `check_win_tu.sh` (28 TUs now) |
| the triggerbot group renders with the aimbot off, inside the window | `menu_shots` (`t101-triggerbot.png`), plus the 25k-ceiling assertion |
| everything else | `node agent/context.ts --checks=full` — 8/8 |

What is **not** proven here: whether the world sweep finds primitives in a live client, and
whether a click lands. Both need a server; both are ❓ rows in `README.md`.

---

## 13. The misc set — four reference features, two shared rules (2026-10)

Ported from `jatosm/RobloxExternal-base`: `misc/{gravity,hipheight,nofall,antiafk}` (the
reference's `misc/zoom` was ported too and **removed at the user's request**: "remove zoom".
Nothing replaced it; the FOV changer is the only FOV control).
They are small; what makes them worth a section is that three of them are the same
problem twice, and that no-fall's first two implementations were both wrong.

### The rule they all needed — `features/misc/body_rules.h`

Gravity and hip height write a property and put the original back on release. The hard
part is one question: **the value I just read — is that the game's original, or my own
write coming back?**

Get it wrong one way and the character keeps 20 studs/s of gravity forever, because the
"original" that got saved was what we wrote. Get it wrong the other way and a respawned
body inherits the previous body's numbers — a bug this repo has already had once, in
mover's save/restore.

```cpp
RememberOriginal(cur, lo, hi, target, prev_saved, fallback)
//  within eps of `target`  -> that read is OURS: keep prev_saved
//  outside [lo, hi]        -> not an original at all: fallback
//  a failed / NaN read     -> keep prev_saved rather than poison it
```

13 Linux cases over it (including the NaN and "the second pass reads our own write" cases),
and each feature drops its save when the *instance* changes: a new `Humanoid` for hip
height, a new `World` for gravity. `no fall` needs none of that — it is a clamp, not a
property — but its rule is the same shape: only the downward component, only past the cap,
and a NaN velocity is left alone, because "clamp" on a value we could not read is a write
that turns a failed read into a physics change.

### No-fall writes through mover, and yields to it

The reference writes `Primitive::AssemblyLinearVelocity.y` directly. A Primitive holds
**two** identical motion blocks and the engine re-syncs from the second one, so a
single-field write is reverted within a step (§7, and `mover.h`'s own note). So this goes
through `mover::WriteVelocity` — both blocks, position untouched — and it stands down
while `mover::Current() != None`: flight, a teleport hold and the jump feature are driving
the velocity on purpose, and clamping their downward component from the outside is fighting
the feature the user turned on more recently.

### Zoom: ported, then deleted

`misc/zoom` (hold a key to narrow `Camera::FieldOfView`) was ported and lived on the
visuals page. It shared `Camera::FieldOfView` with the FOV changer and needed the
encode/decode classifier in `features/fov/fov_encode.h` to know whether the client stores
degrees, radians or a tangent scale. The user's verdict was **"remove zoom"** — one FOV
control is the feature. The feature directory, its globals, its config keys, its keybind,
its page card and its build entries are gone; nothing replaced it. What survives is
`fov_encode.h`, because the FOV changer still needs it, and its classifier is unchanged.

### Anti-AFK is the one feature that types at the game

It taps space through `platform::TapKey`, and `SendInput` delivers to whatever window has
focus — so `platform::GameWindowForeground()` is checked *immediately before* the tap, not
once at enable time, and the timer rearms on focus loss (the first tap after alt-tabbing
back is a full interval away, which is the one moment a random space press is obviously not
the user's). The interval is clamped to 1–15 minutes; 0 would be a key macro.

The stub `<Windows.h>` grew the anonymous union the real `INPUT` has (`mi`/`ki` overlap),
because a stub that cannot express both members stops parsing exactly the TU it exists to
parse.

### What proves it, off Windows

| Evidence | Where |
| --- | --- |
| 68 cases (38 geometry + range, 15 FOV form, 13 save/restore, 2 misc) | `glass-obsidian/tools/world_ray_test.cpp` → the `features-geometry` check |
| 33 TUs parse (was 28) | `check_win_tu.sh` |
| the new groups render with every switch on, inside the window | `menu_shots`, `t102-physics.png` |
| config round trip, slider ranges, binds filed by tab | `menu_shots` structural checks + `config.cpp`'s sanitize block |

Not proven here: a live round trip of gravity or hip height across a respawn, whether the
live client stores FOV in the form this repo assumes (the read-back check is what will say
so in the log), and whether an idle session actually needs the anti-AFK tap. Those are the
❓ rows in `README.md`.


---

### Addendum (2026-10-17): the three now report their own round trip

The README row for the trio is an unverified live round trip, and a silent feature cannot
answer one — so gravity, hip height and no-fall grew the read-back the FOV changer already had,
and nothing else changed:

* **gravity / hip height** read their own write back **once per activation**, 250 ms later
  (several physics steps), and say what they saw: the value, the original they saved, and the
  read-back. A read-back within 0.01 says "round trip ok"; anything else names the failure
  ("the write is NOT sticking, something else owns World::Gravity"). The 250 ms exists
  because a write the engine also owns is only worth anything if it is still there after the
  engine has had a step to overwrite it.
* **The both-ways save is now visible.** The two rules the Linux cases test — *our own write
  is not an original* and *the saved value belongs to the instance it came from* — report
  themselves: `gravity: restored 196.2 ...` on release, and on a place change / respawn the
  line saying the saved value was **dropped** rather than written into a World or a Humanoid
  that never had it. That drop is the bug this repo has had twice, and it was invisible.
* **no-fall watches its own failure mode**, which is the one this feature has a rule about: a
  Primitive holds two motion blocks, so a half-written velocity is reverted within a step. It
  therefore reads the falling speed back once a second (before its own clamp, with a wide
  margin: gravity adds ~3 studs/s per 16 ms step, a reverted write reads hundreds) and speaks
  only when the clamp it wrote is being undone. Silence is the proof; the line is the bug
  report.
* **The format strings were checked, not eyeballed.** `LogLine` is a bare varargs
  declaration, so a wrong specifier is a live crash rather than a compile error. Adding GCC's
  `format(printf, 1, 2)` attribute to `globals.h` for one build and compiling the three TUs
  with `-Wall -Wextra -Wformat=2` produced **no warnings** (with a negative control — a
  deliberately short and a deliberately long call — confirming the attribute catches exactly
  that). The attribute is GCC-only and MSVC does not take it, so the header is unchanged and
  the check stays a documented one-off. To repeat it after adding log lines: add
  `__attribute__((format(printf, 1, 2)))` to the `LogLine` declaration in `globals.h` for one
  build, then

  ```
  g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wformat=2 \
      -Iglass-obsidian/tools/win32_stub -Iroblox_external -Iroblox_external/features \
      -Iroblox_external/imgui -Iglass-obsidian/include roblox_external/features/nofall/nofall.cpp
  ```

  and remove the attribute again before committing (it is a GCC extension; MSVC would
  reject it).

### The other three silent features (2026-10-17, same pass)

The same rule — a feature whose row says "unverified" should report what it can see — applied
to the three features that had no read-back at all:

* **triggerbot** reads the target's health back 400 ms after the click. That is not the
  server's verdict and the line does not pretend it is ("either a miss, or a click the game
  did not take"); it separates the two failures that matter (the click is not reaching the
  game, vs the shot missing) from the third (the feature never fired), because the first two
  have different fixes. The name is copied out of the snapshot: the entity pointer is
  borrowed from a shared_ptr that may be replaced before the deadline, so the pointer is not
  held, only the address and the health.
* **wall check** announces the grid once it is non-empty (`N boxes in M cells`), and again
  only when the map's box count moves by more than 25 % — i.e. a place change. The point is
  not the numbers; it is that the log always states which lane answered, the geometry
  raycast or bodies only, and that statement is the README row.
* **skybox changer** re-reads its two invalidation bytes 300 ms after writing them. A
  renderer that consumed the invalidation sets them back to true; one that never looked
  leaves them false. That splits "wrong byte" from "right byte, nothing drew", which the
  menu's note card (the face read-back) could not do.

None of these can prove the *outcome* — whether the shot was accepted, whether the wall
really blocked, whether the sky changed on screen. They prove the mechanism moved, and they
name the remaining possibility in the line itself. The outcome stays the user's observation,
and `docs/LIVE_CHECKLIST.md` is where that observation goes.

## 14. The visual half — head dot, off-screen arrows (2026-10)

Ported from the reference's `visual/{headdot,oof}`, both living in `esp.cpp` on purpose.

### Why they are not their own feature dirs

Every pass in this file needs the same four things: `GetCamera` (the view matrix and
viewport), `WorldToScreen` (which is the **only** projection that converts client-space
pixels into the overlay's desktop-space ones), `InRenderRange`, and the filter trio
(blacklist / team / dead). A pass in its own file would either re-derive those or import
them — and the repo already has one recorded bug from a second projection (the waypoint
markers' copy, correct only in borderless-fullscreen at native resolution). So the two
new passes are functions in `esp.cpp`, and `main.cpp` calls them next to the others.

### The far-range half of the draw-distance bug

The report was "distance 0 = infinite, but nametags and distance don't show at far
ranges". The gate was already infinite at 0 (`features/esp/range.h`). The other half was
`ComputeBoxForPrimitives`: it rejects any projection under ~2 px, a character at 20–25k
studs *is* under 2 px, and the name and distance were drawn *from the box*. So they went
away with it — and `LabelPoint` (head, root fallback) already existed for exactly this
case. Now every label-ish pass uses it, and a collapsed box draws the 3×5 marker instead
of nothing.

### The off-screen direction

A player the game is not showing has no projected point, so the direction comes from the
**clip-space** vector:

```cpp
clip_w < 0.1f  ->  ndc_x = -ndc_x;  ndc_y = -ndc_y;   // behind: the opposite flank
```

That negation is the whole trick — without it the arrow for somebody directly behind you
points the wrong way (the classic form of this bug), and it is why this pass does not go
through `WorldToScreen`, which *rejects* behind-camera points rather than reporting them.
The arrow then sits on a ring (`oof_radius`) around the view centre and is clamped inside
the screen edges by a 30 px margin; the optional name/distance labels are clamped the same
way, because a corner arrow must not push its label off the bezel.

### What proves it, off Windows

| Evidence | Where |
| --- | --- |
| the draw-distance rule, 12 cases (0/negative/NaN = infinite, NaN distance = never drawn) | `world_ray_test.cpp` → `features-geometry` |
| the off-screen group renders as a set, inside the window | `menu_shots`, `t103-offscreen.png` |
| head dot + off-screen arrows behind the same filters as the box pass | same file, same `InRenderRange`/filter lines (greppable) |
| config round trip for 11 new keys | `config.cpp` save/load + sanitize block |

Not proven here: arrow placement at extreme FOVs (the clamp is arithmetic, the aesthetics
are not), and whether the head dot reads well against a bright skybox in motion. Both are
❓ rows in `README.md`.


---

## 15. The aim half — retired, and why (2026-10-17)

The fitted-velocity tracker (`track.h`), the ballistic projectile model (`predict.h`) and the
Simple/Ballistic lead switch were **removed**. The aimbot was reverted to its original
implementation at the user's request: the reported failure ("locks on and immediately glitches
your mouse out above the player") arrived after the aim had grown four layers of correction,
and the cheapest way to make the aim predictable again is to go back to the code that was
predictable before those layers existed.

What stays: FOV target selection, the target-part choice, sticky aim, per-axis smoothing, the
humanizer, the original velocity ÷ factor lead, the team/wall/dead filters, and the triggerbot
(its own switch, key and groups — untouched by the revert). What went: `features/aimtrack/`,
`prediction_mode`, `prediction_speed`, `prediction_gravity`, `prediction_damp_turn`, the menu's
lead-model combo and its three ballistic sliders, and the config keys for all of them.

The history is still in git (`3834fc6`, `a22669a`); the design notes are in this section's own
log. If a lead model comes back, it should come back with a **live measurement** attached, not
as another layer on an aim that nobody can predict.

## 16. Flight's stealth profile — the three things a game can notice (2026-10-17)

The report was "occasionally disconnected while flying". Nothing in this repo can see the
game's anti-cheat, so what follows is **risk reduction against the mechanisms an external
actually controls**, not a claim of invisibility. The honest framing is in the README row
and repeated here because it matters: an external changes memory; a server compares what a
client claims against what it can derive; the only lever a tool like this has is to make
what it writes *look like something the engine would have produced*. That is what this
section is.

### What was noticeable, ranked

1. **`Workspace.Gravity = 0`.** A *global, replicated* property. For the whole time flight
   was on, every falling object in the place behaved differently, and a game script that
   reads `workspace.Gravity` — a one-line read, common in anti-cheat and in physics-driven
   games — saw zero. This is by a wide margin the loudest thing the feature did.
2. **A velocity step.** 0 → 70 studs/s in one write is an acceleration no character
   reaches. Repeated every time the key was tapped, in any direction.
3. **An unbounded climb.** The vertical component was whatever the speed slider said, so
   holding space could climb at 300 studs/s — the least plausible motion a Roblox body can
   make.

### The three fixes, and why they are arithmetic

All three live in `features/flight/stealth.h`, header-only and free of memory, Windows and
offsets, so the Linux harness runs them (30 cases):

* **No gravity write.** The world is left exactly as the game left it. The hover is
  produced by pinning `v_y + 0.5·g·dt`: with the velocity pinned at the top of a step, the
  solver integrates `v − g·t` and moves the body `v·dt − ½g·dt²`, so `v = ½g·dt` cancels it
  to zero. Gravity is still applied; the body is *held*, which is what a hovering body
  physically is. The bias assumes a 60 Hz step to *seed* it and is then **trimmed** from the
  measured drift (integral correction, deadband, clamped) so a client that steps at another
  rate settles instead of sinking.
* **A ramp.** `Approach` moves each axis toward the command at `flight_accel`, never
  overshooting, with the frame delta floored and capped (0.1 s) so a stalled feature loop
  cannot turn into a lurch. Take-off, turns and stops are curves with a slope.
* **Caps.** `flight_max_speed` on the horizontal component, `flight_max_climb` /
  `flight_max_descent` on vertical, separately — climbing is the axis a game is most likely
  to find implausible. `0` in a hand-edited config means "no cap" in the rule, so the
  config sanitiser enforces the real floors.

### The write watch — a diagnostic, not a kill switch

Once a second, while flying, the feature reads back what the assembly holds and compares it
with what it pinned (`VelocityHeld`: 15 studs/s plus 35 %, tight enough that a body stopped
dead is a miss and loose enough that the gravity swing between two pins is not). After three
consecutive misses it says so, **once per flight**:

> `flight: the assembly has not held the pinned velocity for 3 checks (want …, read …). If
> you are also rubberbanding, the server has taken the body back — that is a step before a
> kick, and continuing to fly is what makes it worse`

It deliberately does **not** stop flight: a wall, a ladder or a seat can hold the body while
you fly into it, and a feature that quits there is a bug the user blames on the feature. The
point is that a disconnect now has a line to correlate with, including what profile was
running.

### The two cases that failed first

Both are in the harness because both were wrong in the first version of the code:

* `TrimBias` **added** the drift, so a body sinking got *less* lift — the correction doubled
  the error on both sides. The case (`sinking raises the bias`) caught it.
* `VelocityHeld` with a 50 % fraction said a body that **stopped dead** was still holding a
  50 studs/s pin. Tightening it to 15 + 35 % made the stopped body a miss while the 3.3
  studs/s gravity swing still passes — both are cases now.

### The state watch — the earliest observable half

`features/players/humanoid_state.h` is a second pure rule (10 cases): the Humanoid's own
state id, classified into "knocked down" (Ragdoll, FallingDown, GettingUp, Seated) and
"upright" (everything a player does on purpose). Flight reads it once a second while flying
and reports a knockdown state once per flight, because a body in Ragdoll while we pin a
velocity is a picture the engine will not draw for a standing character — and a server that
has taken the body back usually shows it **here** before the position starts to rubberband.
`Plausible()` rejects a failed read (outside 0..31) so a hiccup reads as "unknown" rather
than as Ragdoll; a state watch that fired on a read failure would stop a feature for no
reason. The same rule is what the aimbot's "don't aim at a corpse" filter wants, which is
why it lives in `players/` rather than inside flight.

### What proves it, off Windows

| Evidence | Where |
| --- | --- |
| 30 cases: ramp (bounded, no overshoot, reversal, broken clock), caps (horizontal vs vertical, 0 = no cap), hover bias (value, zero/negative gravity, zero step), trim (sign, deadband, clamp, NaN), write watch (held, stopped, wild, NaN, mid-step swing) | `world_ray_test.cpp` → `features-geometry`, 184 cases |
| 10 cases: the knockdown set, the upright set, a corpse never upright, garbage never a state | same |
| the new settings render as a set when stealth is on | `menu_shots`, `t105-flight-stealth.png` |
| the flight TU and the menu parse | `check_win_tu.sh`, `menu-shots` |

**Not proven here — and this is the row that matters:** whether it stops the disconnects. That
needs the Windows client, and the log lines above are what turn "it disconnected again" into a
timeline. If a disconnect still happens with stealth on and no write-watch line, the cause is
not the velocity pin — it is somewhere else (teleport, hitbox, the server's own checks on
position deltas), and the next step is the same evidence-gathering, not more of this section.

---

## 17. Three frame rates, and the one that is a write (2026-10-17)

### The report

"The FPS widget is for the menu's FPS." It was: `render_ui` drew
`ImGui::GetIO().Framerate`, which is the overlay's own present rate. On an uncapped
Present that number is in the thousands while the client renders at sixty, so the
widget was measured, correct and answering a question nobody asked. The tool has three
frame rates and only one of them is the game's:

| rate | who owns it | how it is read | how it is set |
| --- | --- | --- | --- |
| the client's | the game's simulation clock | `Workspace::DistributedGameTime` delta / real time (§19) | `TaskScheduler::MaxFPS` — a 4-byte write |
| the overlay's | `overlay.cpp`'s `Present` | `GetIO().Framerate` | paced, not written |
| the menu's | the same loop | the same number | the same pacer |

The widget prints the first and the second as two labelled lines; `--` stands in for the
first when the clock does not advance, because a number derived from a misread bit
pattern is worse than no number. The first version of this fix read
`RunService::HeartbeatFPS` — and on the user's client that field read **1** while the game
ran at 240, which is §19's whole subject.

### The cap is a write, and the type is not in the dump

`Offsets::TaskScheduler::MaxFPS` is published as `0xb0` into the scheduler instance. The
dump does not say whether the field is an `int` or a `float`, and writing the wrong one
is not "a cap that does not work" — it is 1,114,636,288 in the scheduler's frame budget.
The two possibilities do not overlap at all in the range a cap lives in:

| field holds | bits | read as int | read as float |
| --- | --- | --- | --- |
| int 60 | `0x0000003C` | 60 | 8.4e-44 (denormal) |
| float 60.0 | `0x42700000` | 1,114,636,288 | 60.0 |

So `fps_field.h` classifies before it writes: exactly one reading in `[1, 1000]` picks
the form, neither or both means **Unknown**, and Unknown writes nothing and says so once.
That is the rule this repo keeps re-learning (`humanoid_state.h`, the arrow/box code):
when a field's type is not published, read it, do not assume it. The same classifier
reads `HeartbeatFPS`, which is how the widget works on either client build.

The write itself is conservative in the three ways an external should be:

* the original 4 bytes are saved **verbatim** (not as a float or an int) and restored on
  release, on detach, and at exit — a cap left in a live client outlives the tool;
* re-asserted at most **once a second**, and only when the field no longer holds the
  wanted value. The client re-applies its own cap on focus and resize; a per-frame write
  would be a fight and a syscall storm;
* every write is read back, and the log distinguishes "held" from "the client is
  overwriting it", which is the only evidence a live session can give about whether the
  client honours the field at all.

`RunFpsCap` runs in the feature loop (a memory write, not a draw). The scheduler pointer
is re-resolved once a second rather than every pass — resolving it is a process-handle
read, and the loop runs at roughly a kilohertz.

### The overlay's pacing is not a cap on the game

`Present(0, 0)` is a busy Present: it returns as soon as the frame is queued, so the
overlay used to spin as fast as D3D11 accepted with nothing on screen — a core burned to
draw a transparent window over the game. The pacer changes only *this* process's rate:

* `overlay_fps_cap` (default 144) while anything is being drawn — the menu is open, or
  any visual pass is on (`g_overlay_busy`, set in `render_ui` from the same conditions
  that gate the draws);
* `overlay_idle_fps` (default 20) when nothing is. Idleness is decided by the render
  side (`render_ui` sets `g_overlay_busy` from exactly the conditions that gate its
  draws), not by a window query: a window that draws nothing is idle by definition,
  and a minimised game needs no special case;
* `0` for either means unpaced, i.e. the old behaviour, for anyone who wants to measure
  it.

The arithmetic is `fpsfield::PeriodNs` / `NextDeadlineNs` — a period per frame and a
deadline grid that **drops** a missed deadline instead of accumulating debt, so a stall
cannot become a burst of catch-up frames. It lives in the header, free of Windows, and
the Linux suite runs it.

The wait is the part that does not port. Windows' default `Sleep` granularity is ~15.6 ms
unless a process raises the system timer, so a naive `SleepMs(7)` at 144 fps would
deliver ~60 and turn the pacer into a stutter generator. `overlay.cpp` uses a
high-resolution waitable timer (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, Win10 1803+),
falls back to `Sleep` if it cannot be created, and spins the last half-millisecond
because `Sleep` cannot be trusted below that. `timeBeginPeriod` is deliberately not used:
it changes the timer resolution for the whole system, including the game.

### What proves it, off Windows

| Evidence | Where |
| --- | --- |
| 28 cases: int/float/zero/garbage classification, Encode/Value round trip in both forms, `ClampCap` (0, negative, NaN, floor, ceiling), `PeriodNs` (0, negative, 60/144/240 fps, 1 ms floor), `NextDeadlineNs` (first frame, on time keeps the grid, late is dropped, no period) | `world_ray_test.cpp` → `features-geometry`, 184 cases |
| the three cards render as a set and fit their clip | `menu_shots` |
| the feature TU and the menu parse against the stub | `check_win_tu.sh` |

**Not proven here:** whether the live client's field is an int or a float, and whether the
client keeps our value past its next focus change. Both are what `LIVE_CHECKLIST` 11 is
for, and both already have a log line that says which happened.

## 18. The world fills in: eight switches on one service, seven scans, and two properties (2026-10-17)

### All eight lighting switches write the same service

`visual/world/env` in the reference is not one feature, it is a page of small ones — ambient,
fog, exposure, fullbright, shadows, grass, night, time — and **three of them write
`Lighting.Ambient`**. That is the whole design constraint: a restore that puts back "the
ambient I saved" is only correct for the switch that saved it. So each switch keeps its own
prior value (and its own "was I on last pass" flag), restores **its own group** the pass it
goes off, and the frame order is fixed: night over fullbright over ambient. Turning night off
cannot leave fullbright's white behind because night never owned fullbright's white.

The service itself is a moving target. When `Lighting` re-resolves to a new address (a place
change), the saved backup belongs to the old place and is **dropped before the new capture**
rather than written into an unrelated service — the same rule gravity learned the hard way
(§13). A `Lighting` that does not resolve writes nothing and says so once.

The clock is arithmetic, not a write. `features/world/sky.h` recomputes the gradient top and
bottom, the light colour, the sun and moon positions and which of the two is the source, from
(hours, latitude). It is a pure header, so the Linux suite runs it (§18 evidence) — including
the two cases that would otherwise be silent: `24.0` is midnight of the next day, and an hour
or latitude that did not read produces **no** sky rather than a wrong one.

### The scan is amortized; a freeze would be the bug

Seven categories (ore, plants, bots, animals, tools, crates, keycards) with the reference's
name tables **verbatim**, matched case-insensitively as substrings. The scan is a BFS from
`Workspace` that keeps its queue between passes and spends about a millisecond per pass:
8,000 nodes, depth 6, a full rescan every 5 s, on a `Workspace` change, or when the category
mask changes (a switch turned on must not wait five seconds for its first marker). Matches
survive a rescan; each entry re-reads its position from its first primitive every 100 ms, and
an entry whose primitive will not resolve is dropped after one re-resolve attempt — a marker
that stays where an object used to be is worse than no marker.

The render is deliberately plain: an 8 px square and an optional `name distance`, behind the
same "0 means unlimited" gate the player ESP uses (`world_esp_distance`).

### Jump power is three fields, one of which decides the model

`Humanoid.UseJumpPower` is what decides whether the client reads `JumpPower` or
`JumpHeight`, so setting one without the other is how the reference's feature *looks* on in
the menu and does nothing in the game. All three are written together, `JumpHeight` kept at
`value × 0.144`, the originals captured before the first write and restored on release or
key-up. The saved body is checked by address: a respawn drops the save (with a line that says
so) instead of writing a dead character's numbers into the new one.

### The hit sound would have lied if it detected its own hits

The reference plays a click under its cast; this repo already has something better — the
triggerbot's 400 ms health read-back, which *proves* a click connected. So `Play()` is called
from exactly one place, on the landed verdict. A miss is silent by construction.

The audio path and the folder scan live behind the platform seam (`ListFilesWithExt`,
`PlayWav`), because a feature file that includes `<Windows.h>` fails the containment check —
and did, before the seam was added. The two default tones are written as RIFF by hand (bytes,
not host integers) so the same generator ports.

### What proves it, off Windows

| Evidence | Where |
| --- | --- |
| 19 cases: ramp clamping, midnight/noon ambient, light colour, sun-vs-moon at noon/midnight/sunrise, `24.0`, bad hour, bad latitude, rotation length, the warmer dusk gradient | `world_ray_test.cpp` → `features-geometry`, 184 cases |
| the world cards, jump-power card and hit-sound card render, and stay inside their clip; the lighting group is shot from the top and scrolled to night/hour | `menu_shots`, 82 checks, 39 PNGs (`t107`, `t108`) |
| the new TUs and the menu parse against the stub; `env.cpp`, `world_esp.cpp` and `friends.cpp` are in the 39 | `check_win_tu.sh` |
| the platform seam still contains `<Windows.h>` | `windows-h-containment` |

**Not proven here:** how many objects a live place matches, whether the lighting restores
look right on a real sky, whether a jump is visibly higher, and whether a hit is audible.
Those are `LIVE_CHECKLIST` §12–§15.

### The friend set, and why it is not an address set (§18 addendum)

The reference keeps `unordered_set<uintptr_t>` of player *and* model instance addresses.
An instance address is a live heap pointer: the model is rebuilt on every respawn and the
whole datamodel is rebuilt on a place change, so after either event the set still contains
numbers — just not the ones the new instances have. The mark either vanishes or follows
whatever is allocated at the old address.

`features/players/friends` holds user ids instead. `Player::UserId` is a property of the
account, stable for the session, and already read by the ESP snapshot; the skeleton cache
now reads it too, because skeletons were the one draw path whose entity had no id. The
consequence is that a friend list survives a respawn, a rejoin, and a config round-trip —
`player_friends=` is a list of ids, restored before anyone spawns.

One predicate, `EspTint`, decides the colour and the order is whitelist > friend > team >
the feature's own colour. Every element (box, name, skeleton, chams, tracer) already asked
it, which is why the feature is a set and a button rather than a fifth copy of the draw
code.


---

## 19. When a field is not the rate: four live failures and what replaced them (2026-10-17)

The user's verdicts this round were short — *"game fps isnt accurate it shows 1 fps even
though im at 240"*, *"click tp doesnt work"*, *"no fall still wont work"*, *"wall check
doesnt work"*, *"black outline doesnt work"*, *"the arrows can sometimes go past their limit
on the ring"*, *"remove zoom"*, *"add fling by spining the character"*, *"its still missing
features such as dex"*. Each one had a mechanism behind it, and in three cases the mechanism
was *the thing we believed was the measurement*.

### FPS: a rate cannot be looked up in a field whose meaning is guessed

`HeartbeatFPS` classified cleanly as an int — it read 1. A plausible value is the worst kind
of wrong reading, because the classifier cannot see it. A frame rate is a rate, so it is
now derived: `Workspace::DistributedGameTime` advances once per simulated frame, and
Δgame/Δreal over a window of at least 200 ms **is** the client's rate. The meter tries the
double encoding (the property's real type) and the float one, keeps whichever produces a
rate in `[1, 2000]`, smooths with a 0.4/0.6 EMA, and publishes nothing while the clock is
still, moving backwards, or jumping more than a second. No offset is trusted to *be* fps;
the evidence is two consecutive samples.

### Wall check: an empty world list is indistinguishable from a clear line

`worlddet` validates a guessed layout of `World::Primitives`. When the guess fails it
publishes an empty vector, `world_blocked` returns false for every ray, and the feature
reports "nothing in the way" — which is exactly what a broken wall check looks like. The
fallback does not guess: **walk the Workspace instance tree**. One `BasePart::Primitive`
pointer read decides part-vs-not-part, so a walk costs one read per instance (+ position,
size and flags per part); it runs on the sweep thread with a 250 ms budget, honours the same
`CanQuery` gate, skips character primitives from the ESP/skeleton snapshots, and publishes
the same `WorldBox` list, so the raycast layer cannot tell which lane ran. The grid, the OBB
math and the per-ray live re-read are unchanged — the bug was never in them.

### Teleport: winning the race is not adopting the position

`players.cpp` hammered the root's position for 400 ms and let go. The solver owns an
unanchored root, so the moment the writes stopped it re-settled at the old assembly position;
the user saw nothing. `mover::Anchor` already existed for this — "anchoring is what made
teleports work in the first place" — and the fix uses it for a phase, not the whole hold:
anchor → hammer the destination for 250 ms → lift the anchor → keep writing ~450 ms so the
assembly adopts the new position → release the claim. Every hold logs the displacement
actually achieved, so "did the click move me" is in the log instead of only on screen.

### No-fall: the slider could not express the feature

The clamp treated `0` as *release*, so the reachable settings were 1–200 studs/s: fall
*speeds*. "No fall still won't work" is a correct reading of a feature that could only slow
the fall. `0` now holds the downward component at zero from `mover`'s hold thread (~66
attempts per physics step, both motion blocks), and values above zero remain the terminal
velocity they always were.

### The rest, in one line each

* **Black outline** was a 1 px difference between two stroke widths on the same path. The
  path is expanded by half the band, so the black is `w` px outside the colour at any width.
* **OOF ring arrows** were clamped to a rectangle, so a diagonal arrow's radius changed with
  the angle and could pass the limit. Placement is now on an ellipse by construction.
* **Skybox** strings were half legacy URLs; they are normalised to `rbxassetid://`, and the
  invalidation bytes are held false for 250 ms so a frame must sample them.
* **Zoom** is deleted, not disabled: directory, globals, menu, config, project entries. The
  FOV changer keeps `fov_encode.h`.
* **Fling** is a spin: angular velocity only, both motion blocks, unpaced from the hold
  thread, axis re-rolled every 120 ms. Linear velocity is untouched, so you can still move.
* **Explorer** is the reference's dex without its backend: a budgeted BFS publishing an
  immutable snapshot, names read at draw time, search resolved in the worker, its own window.

## 20. Round two: five failures that were mechanisms, not tunings (2026-10-17)

The first pass treated the reports as parameters to tune. The second pass found that five of
them were the *wrong mechanism*, and replaced each rather than adjusting it.

### No fall: a clamp cannot stop a fall

The solver integrates gravity into the **position** every step. Clamping
`AssemblyLinearVelocity.y` therefore changes how fast the position falls, never whether it
falls - a terminal speed by construction, which is exactly what the user kept reporting.
`mover::HoldVertical` writes the position back instead: read Y, and if it is below an anchor,
write the anchor into both motion blocks and zero the downward velocity in the same iteration.
The caller (`features/nofall`) owns the anchor and moves it down with the body while the body
is *walking* (below 15 studs/s of descent), so slopes and stairs still work; only a real fall
freezes it. The feature reads the body back once a second: `the body is N studs below the held
level ... send this line` is the honest failure report.

### Wall check: the box list was empty

`Primitive::Size` is documented in this repo as reading ~0 on the live client (esp.cpp says
so, and that is why the ESP box uses canonical part sizes). Both wall-check lanes depended on
it anyway: `worlddet` required a plausible Size to accept a container entry, and the
instance-tree walk *dropped* every part whose Size read zero. Two independent reasons for an
empty list, and an empty list is "nothing in the way". The rework: detection retries on
positions alone; the size field is **measured** by scoring every aligned float triple outside
the motion blocks over a 24-part sample (a build where 0x1c4 is right still picks 0x1c4); a
part with no readable size becomes a 1-stud box instead of disappearing; the `CanQuery` gate is
removed, because its flag word lives next to the rotation this build already documents as
garbage. The log names the lane, the measured offset and the fallback count.

### Skybox: the validity bytes cannot be derived

`RenderView::SkyValid 0x28d` / `LightingValid 0x150` are guesses. The reference's 0x2dc/0x278
are for a different build, and the +0x10 relationship that holds for the dump's Lighting fields
does **not** hold for RenderView (0xc38 vs 0xc30 is +8, not +0x10), so there is nothing to
derive them from. The bytes are now found by **behaviour**: a paced scan clears each isolated
`1` byte in a 0x300-byte RenderView window, waits ~3 frames, and keeps the ones the renderer
puts back, because only the renderer restores a flag it consumes. Every touched byte is put
back; the whole discovered set is cleared on each apply. The face offsets are sanity-checked
against the *original* strings - a read-back reads the address it just wrote and cannot catch a
wrong offset, but "rbxassetid://..." at Sky+0x1E8 can.

### Fullbright / ambient / night: a grade instead of a write

All three wrote Lighting fields and depended on the renderer re-reading them - the same class
of unknown dirty byte as the skybox. They are drawn by the overlay now
(`worldenv::RenderLightingGrade`), under the ESP and before the menu, with the priority order
the engine-side code had (night > fullbright > ambient). A grade cannot be gated by an offset,
and has nothing in the client to save or restore. `g_overlay_busy` counts them, or the overlay
would idle while only a grade is on.

### FOV: the off edge never ran

`RunFovChanger` was called only while the switch was on, so the value it wrote could never be
put back - the user's "your fov stays at what it is even when you turn it off". The call is
ungated now, the camera's own value is captured per camera instance before the first write, and
the off edge (and detach) restores it, with a read-back.

### The smaller verdicts

* **Outline** is deleted outright (esp.cpp, globals, config, menu card, docs). Two reports of
  "doesn't work" are not a tuning problem.
* **Mesh chams** replaces it in the chams group: per-part canonical box, only the faces turned
  toward the camera, far-to-near, translucent fill plus a 1 px silhouette edge.
* **Dex** is a menu page (`dex`, page 10). The old floating window and the Debug-page toggle
  are gone; `Run()` only walks while the page was drawn in the last second.
* **World objects** are removed from the World page, with their name tables, globals and
  config keys. Bots and animals moved to the visuals page as the **npc esp** group.

## 21. The aimbot, one day later: reverted (2026-10-17)

The angle-based mouse controller described in the previous revision of this section is
**gone**. It replaced three real bugs in the pixel path - mixed client/screen coordinates, an
open loop under mouse-lock, and pixels standing in for mouse counts - and it tested clean
(15 aim-angle cases), but the user's instruction after trying it was to put the aimbot back to
its original implementation. That is the rule this repo follows for a feature the user has
lost confidence in: revert first, keep the analysis, and rebuild only with evidence.

The triggerbot - the other half of the aim page - is untouched and keeps its own switch, key,
filters and groups. `features/aimtrack/` no longer exists.
