# The live-session checklist

*Written 2026-10-17. The counterpart to `agent/verification/CAPABILITIES.md`: that file is
what is proven off Windows, this one is what only a running client can answer.*

Every ❓ row in `README.md`'s feature table appears here once, with what to do, what the
answer looks like, and what to send back. Nothing here needs a debugger. When a row is
answered, replace its ❓ with ✅ in `README.md` and add one line to `docs/CHANGELOG.md`
saying what was seen — the evidence, not "verified".

Two habits worth keeping while you run it:

* **The debug page is the instrument.** It is the last tab; the log card at the bottom is
  where every read-back below appears, and the offset write tests there are the ones to run
  when a write does not stick.
* **One change at a time.** Two features on at once turns "it did not work" into a guess
  about which one moved the number.

---

## 1. The physics pair — `movement` page, *physics* group

Gravity and hip height each write a property and put it back, and report their own round trip
(the same shape as the FOV changer's read-back):

| | how | the log line that answers it |
| --- | --- | --- |
| gravity | enable, set a value (e.g. 50), then move and release | `gravity: wrote 50.0 (original 196.2), read back 50.0 - round trip ok` — **or** `... the write is NOT sticking, something else owns World::Gravity` |
| hip height | enable, set 4, walk | `hip height: wrote 4.00 (original 2.00), read back 4.00 - round trip ok` |

**No fall left this group.** Its third live failure retired the velocity clamp: the solver
integrates gravity into the *position*, so a clamp can only set a terminal speed - it cannot
stop a fall. It is a **position hold** now; its step is **§7b**.

Then the part no single step can answer, and the reason these are ❓: **respawn and change
place while each is on.** Expected log lines:

* `gravity: the World instance changed (0x... -> 0x...) - the saved 196.2 belongs to the
  place that is gone, dropped rather than restored into an unrelated place`
* `hip height: the Humanoid changed (0x... -> 0x..., a respawn) - the saved 2.00 belongs to
  the old body, dropped rather than written into the new one`
* and on release: `gravity: restored ...` / `hip height: restored ...`

**Send back**: the log lines for both, plus whether the character's numbers were normal after
the respawn.

## 2. Zoom — RETIRED

Zoom was removed at the user's request (`remove zoom`). There is nothing to test here.
The FOV storage-form check now belongs to the **FOV changer** slider alone: changing it
must change the projection without smearing, and a log line naming a different form
(`fov: wrote ... and read back ...`) means the classifier guessed wrong — send it.

## 3. Anti-AFK — `movement` page

Needs an idle session: enable it, then leave the client alone longer than the tap interval
(1–15 minutes; use 1 to test). It taps spacebar **only while the window is focused** —
`SendInput` goes to whatever has focus, so clicking another window first is the correct
behaviour, not a failure.

**Answer**: does the session survive the idle timer at all. If the client does not need it,
that is a finding too (the row becomes "not needed", with a line saying why).

## 4. The visuals — `visuals` page

| row | what to look at | the specific thing that is unverified |
| --- | --- | --- |
| head dot | its own group; toggle fill/outline, colour, thickness | that it sits on every head, and that a fixed pixel size still reads at 200 studs. The reference scaled it with distance; this is pixels on purpose |
| off-screen arrows | *off-screen* group; arrow / chevron / triangle | **a player directly behind you**: the arrow must point behind, which is the clip-space negation. Then an extreme FOV (FOV changer at 120) — the arrows ride a fixed screen ring, so check they are not in the wrong place at the edge of a very wide view |
| tracers | markers group | screen anchors at the feet |
| far range | set draw distance to 20000+ | nametags and distance still render — and at **0** (infinite) they must render at any range, and nothing may linger past a set range |
| hitbox expander | markers group | that boxes grow and shots land |
| dead check / team check | aimbot + players page | the aimbot lets go of a body when it dies; teammates are skipped |

Screenshots are the evidence here — one per row is enough, and they can go in the PR
description rather than the repo (the tree keeps small PNGs only).

## 5. Flight — `movement` page, the stealth switch

The report this came from: occasional disconnects while flying. Nothing here can see the
game's checks, so the feature removes what it *can* control and reports the one failure it can
observe. Watch three things, in this order:

1. **The profile line**, on the first activation:
   `flight: stealth profile - 80 studs/s cap, 180 studs/s2 ramp, climb 45/descend 60,
   gravity 196.2 LEFT ALONE (hover bias 1.64)`.
   The gravity number is read from the world — if it says 196.2, the world was never touched.
2. **The hover.** Let go of every key mid-air: the character should sit still, not drift up
   and not sink. The bias is trimmed from the measured drift, so the first seconds may settle
   by a stud; a *continuous* sink means the trim is not running (a hover, by eye, is the test).
3. **A disconnect, if it still happens.** The log is the evidence, and there are two lines to
   look for before it:
   `flight: the assembly has not held the pinned velocity for 3 checks ...` — the body was
   being taken back;
   `flight: the local player stopped resolving after ...ms of flight (stealth=on, ...)` — the
   feature saw the body vanish, and that line names the profile.
   `flight: the Humanoid reports state 23 (a knockdown) while we pin a velocity ...` — the
   game was already animating the body as something other than a standing character.
   **Send all of them** and whether they appeared at all: a disconnect with none of these
   lines is not the velocity pin, which is the finding.

Turn the stealth switch **off** once to confirm the old behaviour still works (it zeroes
gravity again and steps instantly) — that is the comparison the switch exists for.

## 6. Triggerbot — `aimbot` page, *triggerbot* group

Its own switch and key; the aimbot's filters (aim part, crosshair radius, distance, hit
chance, line of sight) are its settings. **The unverified part is the click itself**: the
reference hooks the game's projectile/raycast code to know it fired, this repo sends a real
`SendInput` click and cannot see the server's verdict. So it watches the consequence — the
target's health 400 ms later:

| log line | what it means |
| --- | --- |
| `triggerbot: shot 1 at Name LANDED - health 100 -> 87 after 400ms (1 of 1 judged shots landed)` | the click was taken and connected — the row is answered ✅ |
| `triggerbot: shot 1 at Name: health unchanged (100 -> 100) after 400ms - either a miss, or a click the game did not take (SendInput goes to the window with focus)` | the click did nothing. First check the focus: click the game window, then the triggerbot key |
| `... the body left the snapshot before the 400 ms read-back` | no verdict; the target died or left |

If shots land but the health line never appears, check that the body was still in the ESP
snapshot after 400 ms (a one-shot weapon and a fast kill can outrun the read-back).

## 7. Wall check — which lane filled the grid, and does a wall block?

With the ESP on (and **wall check** on either page - the toggle is shared), the log names the
lane that produced the geometry, once:

`wall check: world grid live - 4123 boxes in 902 cells (7 always-tested), build 1` — the
engine's primitive container was recognised, and that is the fast path.

`wall check: instance-tree sweep found 2190 parts in 18744 instances` — the container was
not recognised and the **Workspace instance-tree fallback** walked the map instead.

**Second rework.** The empty grid was not a container problem: the detector required
`Primitive::Size` to accept a container, and the walk *dropped* every part whose size read
zero - and this repo's own ESP notes say that field reads ~0 on the live client. The list was
empty for both lanes. Now:

* detection **retries on positions alone**, and says
  `wall check: the primitive container was accepted on positions alone ... measuring it`;
* the size field is **measured** - `wall check: the part size field measures at
  Primitive+0x1C4 (the dump's offset - sizes are real)` or the same line naming a different
  offset when the dump's is wrong;
* a part with no readable size is still a box (one stud), and a map that is mostly those says
  `wall check: N of M parts had no readable size`;
* the `CanQuery` gate is gone (its flag word sits next to the rotation this build already
  documents as garbage).

Then the behavioural test: stand where a wall is between you and a target. The target's box
must stop drawing (or the aimbot must refuse the lock) while it is behind the wall, and come
back the moment you step out. Rotate: a ramp or a rotated wall must block only where it is -
the test is an oriented box, not a world AABB. **Send back** the lane line, the size line, and
whether a wall now blocks.

## 7b. No fall — where the character actually is

The velocity clamp is **retired**; this is a position hold now. Turn **no fall** on and walk
off a ledge. The body must stay at the height it left at. The log says, once:

`no fall: a fall started at -52 studs/s - the position is now held at y 41.5 from mover's hold
thread (N writes so far)`

while the hold is working. Once a second the feature reads the body's actual Y back; if it is
more than a stud below the anchor you get
`no fall: the body is 3.2 studs below the held level (v -60) - the position hold is being
overwritten; send this line` — that line is the bug report.

Slopes and stairs still work by construction: the anchor **follows** the body whenever the
body is not really falling (below 15 studs/s downward), and only freezes when a fall starts.
If you freeze on a ramp, the threshold is too low for that place; it is `kFallSpeed` in
`features/nofall/nofall.cpp`.

## 8. Aim — `aimbot` page

The aimbot is the original implementation again (the fitted-velocity and ballistic lead
models, and the angle-based mouse controller, were removed). What to check:

1. **aim from** = `camera` writes the camera rotation; `mouse` sends a relative move. Both
   must land on the chosen **target bone**.
2. **smoothing x / y**: higher is slower. At the default 4 the aim arrives in a fraction of a
   second; at 20 it glides.
3. **lead prediction** on, `predict x` / `predict y` around 10: a player running sideways is
   aimed slightly ahead. Lower the divisor (2) and the lead grows; raise it and the lead
   shrinks. A target standing still must not move the aim at all.
4. **sticky aim** on: the lock holds while the key is held (or latched), including through a
   brief loss of line of sight; off, the target is re-picked every ~100 ms.
5. **target bone**: head, upper torso, lower torso, hands, feet. Each must aim at that part of
   the skeleton, not at the root.

**Send back**: which mode, the bone, and whether the aim landed on the bone or above/below it.
The triggerbot's own step is **§6**; it is unchanged by the revert.

## 9. Wall check, when the grid did not come up — `debug` page → **world probe: wall check source**

Only needed if §7 logged *not recognised*. Run the probe and read the log: it dumps the
physics world's primitive container, the auto-detection verdict and sample primitives. **Send
that log** — the ESP, aimbot and triggerbot all share that raycast, so this row gates three
features. Once the grid is live, the behavioural test: stand behind a wall with the ESP on
and confirm nothing draws through it, then behind a ramp (a rotated part) and confirm the
same.

## 10. Config round-trip

Save a profile, change a dozen settings across pages, load it back, restart the app. Every
setting that has a card must come back - the ones added this round are `mesh_chams` and its
colour, and the two NPC switches. A setting that saves but does not load is a `ReadLine` that
fell out of the block; the .cfg is plain text, so the fastest check is to open it and search
for the key.

The skybox step moved to **§19** (it is no longer a "smaller row": it discovers its own
invalidation bytes).

## 11. FPS cap and the FPS widget — the *frame rate* group, and the number in the corner

Two questions, one session. The widget must answer the first before the cap is touched:

1. **Is the widget showing the game?** It draws two lines now — `<n> game fps` and
   `<n> overlay`. The game line is **measured**, not read from a field: `ClientFps()` watches
   `Workspace::DistributedGameTime` and divides how far it moved by how long it took (a
   >=200 ms window, smoothed). On a client running at 240 it must read ~240, and it must move
   when the game is capped or when a heavy scene drops frames. `-- game fps` means the clock
   was not advancing or could not be read in either encoding — that is the line to send.
   The OLD read (`RunService::HeartbeatFPS`) printed 1 on a 240 fps client; if this widget
   ever prints a suspiciously constant small integer again, the meter has been bypassed.

2. **Does the cap take?** Turn on **FPS cap**, set a number well below what the client
   runs at (60 is the default), and read the log, in this order:
   `fps cap: the field holds an int 60.000; armed at 60` — the classifier's verdict and the
   value the client had before we wrote;
   `fps cap: the client's frame rate is capped at 60 (it was 240)` — the write was read
   back and held.
   If instead it says `wrote 60 but the field reads 240 - the client is overwriting it`, the
   client re-applies its own cap and does not honour ours; the feature re-asserts once a
   second and the line repeats at most once a second, which is the evidence.
   `neither as an int nor as a float` means the offset is not this field on this build —
   nothing was written, and that is the finding.
3. **Watch the game, not the widget.** Confirm the client's own frame rate fell (FPS overlay
   of the game, or the game's own stats), then turn the switch **off** and read
   `fps cap: restored the client's own value (read back, ok)`. A restore that says
   `MISMATCH` means the client had already replaced our value, which is harmless but worth
   recording.
4. Close the tool with the cap still on and confirm the game's frame rate returns to
   normal — the tool restores on detach and on exit.

Send both directions (capped and restored) plus the widget's two lines. The unproven pair is
the field's type and whether the client keeps our value past a focus change; everything else
here is arithmetic that already has cases.

## 12. Lighting — `world` page, *lighting* group

**fullbright, ambient and night are a screen grade now**, drawn by the overlay before the ESP
(`worldenv::RenderLightingGrade`), in the order night > fullbright > ambient. They are not
engine writes and there is nothing to restore:

1. **night**: the world darkens and goes cold-blue; the slider scales the darkness.
2. **fullbright**: the blacks lift (a gamma look, not a white sheet); the slider scales it.
3. **ambient**: a colour tint over everything, with an opacity.

All three are visibly applied the moment they are switched on, and vanish the moment they are
switched off. If one of them does nothing, the overlay is not drawing frames - check that
`g_overlay_busy` includes them (it does) and that the menu is not the only thing rendering.

**fog, exposure, no shadows, no grass and time of day are still engine writes**, captured
before the first write and restored when **that** switch goes off:

1. **fog**: start/end/colour. `end` below `start` is clamped to `start + 1`.
2. **exposure / no shadows / no grass**.
3. **time of day**: slider to e.g. 6.5 or 18. The line is
   `world: clock set to 18.00 h (sun ..., source ..., latitude ...)`; a value outside 0..24 says
   `world: clock 25.00 is not an hour - nothing written`.

A failed `Lighting` resolve logs `world: the Lighting service did not resolve - nothing applied
(the skybox changer is the same service)`; the grades still work, because they do not need it.

**Send back**: one screenshot per switch, and whether every restore looked right.

## 13. NPC ESP — `visuals` page, *npc esp* group

The five **object** scans (ore, plants, tools, crates, keycards) are gone - they were the
"bunch of world objects" the World tab was made of - and the two entity scans moved to the
visuals page as **bots** and **animals** with a shared distance and label switch.

The scan announces itself once per count change:
`world: N named objects found; they refresh from their parts from here on`.

* **The numbers are the finding.** Count the bots/animals in view and compare. If the count is
  0, either the place has none or the name table does not match: the reference's tables are
  matched as **substrings**, case-insensitive.
* **Labels** show `name  distance`. Turn them off to confirm the squares alone.
* **Distance** defaults to 2,000 studs; 0 means unlimited.

**Send back**: the count line, a screenshot, and whether the markers sit on the NPCs.

## 14. Jump power — `movement` page, *physics* group

Enable with the slider at 50 (the reference's default). The log opens with
`jump power: saved <power> / height <height> / model power|height` — that is the body's original
values, read before the first write. Then jump: the jump must be visibly higher, and
`jump power: 50.0 (was 16.0), height model switched to power` says the write landed.

Set 100, jump again (higher); set 10 (barely a hop). Then release the key / turn the switch
off and confirm `jump power: restored <power> / <height>` and a normal jump.

**Respawn while it is on**: expected is *not* a restore into the new body —
`jump power: the Humanoid changed (0x... -> 0x..., a respawn) - the saved ... belongs to the
old body, dropped rather than written into the new one`; jumping stays normal until you
re-enable.

**Send back**: both log lines and whether the jump height changed with the slider.

## 15. Hit sound — `aimbot` page, *triggerbot* group

First run writes two tones into `sounds/` beside the exe (`hitsound: 2 wavs in sounds/`).
**test** plays the current selection with the switch off — this proves the audio path.

Then enable the triggerbot with the sound on and fire at a body: the sound must play **once
per confirmed hit**, i.e. after the 400 ms read-back says the target's health dropped — a
miss must be silent, and that is the whole point of wiring it to the verdict. Drop your own
`.wav` in `sounds/`, press **refresh** (or restart), pick it in the combo; a file that is
not 16-bit PCM WAV may not play, and `PlaySoundA` is documented to refuse anything it cannot
parse.

**Send back**: whether the tone plays on hits and not on misses, and whether the combo sees
a dropped-in file.

## 16. Friends — `players` page, *lists* group

Pick a row and press **FRIEND**: the button becomes **UNFRIEND** and that player's box, name,
skeleton, chams and tracer all turn green (`friends_esp_color`), on top of whatever the
whitelist and the team check would have said — the order is whitelist > friend > team.

The thing this checklist exists for: **respawn while the friend is on screen.** The mark must
follow the player, because the set holds the user id, not the model address the reference keyed
on. If the colour disappears after either of you respawns, the id read is wrong (the row's
`user_id` comes from `Player::UserId`); if the colour jumps to an unrelated player, the set was
keyed by address.

Save a config with a friend, load it in a new session, and confirm the mark comes back before
anyone spawns — the set is restored from `player_friends=`, so it does not depend on the player
being in the server when the config loads.

**Send back**: whether the mark survived both respawns, and whether it came back from the config.

## 17. Click teleport — `movement` page

Click somewhere on the ground a few studs away. The log ends with one of:

`teleport: 14.2 studs from the start, 0.4 studs from the target (arrived)` — the client kept
it. This is the only line that proves the feature, and it is printed by the hold itself.

`teleport: 0.1 studs from the start, 14.1 studs from the target (the client put it back)` —
the write was reverted. That is the anchored/un-anchored question answered negatively on this
client; send the line, the "no root part" / "could not arm" lines above it, and whether the
character visibly moved at all.

Before the hold, a refusal names its cause (`click-tp: cursor/window read failed`,
`click-tp: the menu is open...`, `click-tp: not armed...`). Any silence with a click means
the press never reached the feature: check the trigger (`max range` card shows the bind, and
with no bind the left mouse button is the trigger).

## 18. Mesh chams — `visuals` page, *chams* group

**mesh chams** on, with its own colour. Every body part must be filled with the translucent
colour, showing only the faces turned toward you, with a 1px edge closing the silhouette - the
difference from plain chams (an axis-aligned box) is that a character's limbs read as limbs.
Because `Primitive::Size` cannot be trusted on this build, the shape is the part's **canonical**
size at its live rotation, so a mesh that is visually larger than its canonical box will show a
smaller shell than the model. That is the known limit, not a failure.

**The black outline toggle has been removed entirely** - the user's verdict on it twice was
that it does nothing, and a second copy of every stroke was its cost. The OOF ring is in the
same section and is unchanged: the arrow sits on an ellipse whose radius is the radius
setting, and must never leave the window.

## 19. Skybox changer — `world` page

Pick a preset. Within a frame or two the sky must change, and the log says
`skybox: applied <name> - 6/6 faces read back as written, N discovered validity byte(s)
cleared`. The bytes are **found, not guessed**: the first time the feature runs, it scans a
RenderView window and clears one isolated flag byte at a time, keeping the ones the renderer
puts back on its own:

`skybox: scanning 14 candidate validity bytes in RenderView (this is paced, ~1540 ms)` then
`skybox: 3 renderer-consumed validity byte(s) found - the sky cache is now reset by behaviour,
not by a guessed offset`.

If the sky still does not change after that line, the texture write is the problem, not the
cache - and the file now checks exactly that: the **original** face strings must look like
asset urls, or it says
`skybox: the Sky face offsets do not match this build ... Nothing was written` and names the
first string it found. (The read-back cannot catch this: it reads the address it wrote.)
**Send back** that line if it appears, plus the two scan lines.

## 20. Fling — `movement` page

With a player standing next to you, turn **fling** on. The log says
`fling: spinning the character at 420 rad/s about a randomized axis`. The character should
visibly tumble and the neighbour should be launched. If it spins and nothing happens, the
angular write is not reaching the solver: send the rate and whether the body rotated at all
(a body that jitters in place is an orientation fight, not a velocity write).

## 21. FOV changer — `visuals` page

Set **fov** to 100 while the game sits at 70: the view widens and the menu's circle does not
change size (the circle is the aim FOV, not the camera's). Now set the switch **off**: the
view must snap back to 70. The log says
`fov: saved the camera's own value (1.222 rad) before writing` on the first pass after
enabling, and `fov: restored 1.222 rad (70.0 deg) - read back, ok` on the off edge.

If it stays wide, send the two lines - the restore either did not run (the call is gated:
`main.cpp` calls `RunFovChanger` ungated precisely so the off edge runs) or the camera
instance changed under the save (a respawn swaps it; the feature then restores the old camera
and re-saves the new one, which is what the log will show).

## 22. Explorer (dex) — its **own page**, `dex`

Open the **dex** tab (last page, the cube icon). The tree shows the DataModel with the
services expanded. Expectations:

* the object count settles within a second (`N objects`, or `(capped)` on a huge place);
* expanding `Workspace` shows models, expanding one shows its parts (the arrow is on the row);
* the filter box, after Enter, lists matches by class or name (`Humanoid`, `Baseplate`)
  flat, with the matching part of the name in the list;
* clicking a row shows its address and class in the footer, and **copy address** puts
  `0x...` on the clipboard;
* the browser only refreshes while the dex page is the page being drawn - leave the page and
  the count stops moving (that is the design: no background walk for a page nobody sees).

A page that says "the browser is not wired into this build" means the callback is missing
(`menu.cpp`'s `draw_dex`). A page that stays empty while the game is loaded is the BFS failing:
send whether the count ever became non-zero.

---

## Writing the results back

For each row that gets answered, the doc change is one line: the ❓ becomes ✅ (or ❌ with the
reason), and `docs/CHANGELOG.md` gets the date, the observation and the evidence — "gravity
round trip: wrote 50, read back 50, restored 196.2 on release; dropped across a place change
(log, 2026-10-17)". A row that fails is worth more than a row that passes: it is the one that
tells us which assumption in this repo was wrong, and that is the line the next change
starts from.
