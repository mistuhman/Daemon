# Changelog

Branch-level changelog. Commit hashes reference `Glockerz/roblox-external`.

---

## 10. Changelog (this branch)

```
HEAD     THE SERVERS CARD NO LONGER TAKES THE OVERLAY DOWN WITH IT, AND THE WORK AROUND
         IT. "make everything more stable and optimized when i fetch servers the external
         crashes" was the report; a full page of servers was the trigger.
       * The bug: FillSnapshot's row copy read `menuui::Snapshot::ServerRow& d =
       s.servers[s.server_count]` inside a loop over i. It is the append idiom from
       FillPlayers, where the index IS the running count and is incremented at the bottom
       - here server_count was computed once beforehand, so every iteration wrote the slot
       AFTER the list. Under 24 rows that is a harmless unused slot (the card drew empty
       rows, which is the other half of what users reported). At exactly 24 - any popular
       game, since the API page is 100 and the card holds 24 - servers[24] is 48 bytes off
       the end of the array, and what lives there is server_count, server_current, the two
       gate bools and the head of servers_msg. The guid text written over server_count made
       it ~870 million, and menu_ui's row loop runs `for (i < snap.server_count)`: an
       out-of-bounds walk, on the frame after the reply landed. Now `s.servers[i]`.
       * Three rules so the shape cannot come back: the copy moved out of menu.cpp into
       menuui::FillServers (a function the host harness can call with a View that lies); it
       clamps the count to kMaxServers and clamps `current` into the list, and menu_ui's
       draw loop clamps to the array AGAIN from its own knowledge - a count the UI cannot
       check is not a bound; and the worker's out-tray now shares the published View's
       mutex, rows built locally and swapped in under the lock, because a half-copied Row is
       a 40-byte guid with no NUL in it and ShortGuid begins with strlen on it. Say()
       formats outside the lock and copies inside it, so a worker mid-sentence cannot make
       the render thread wait or read half a message.
       * Less work on the wrong threads. Pressing FETCH / HOP / REJOIN ran
       ReadPlaceFromMemory - four dependent reads plus a string, through a handle the attach
       loop may be swapping - ON THE RENDER THREAD, because Start() did it before spawning
       the worker. The read moved into the worker; the button press is now two atomic loads
       and a thread. And the hop no longer waits inside its launch thread: TryInInstanceHop
       (servers_http) polled the datamodel every 100ms for five seconds holding g_busy, so
       the card said FETCHING for five seconds after a fetch had finished and REFRESH
       refused during it. That file does URLs now; RunLaunch opens the link and returns, and
       Tick() - which already reads that same chain once a second - compares the job id and
       reports "in-instance hop", "the client opened a different place", or "still on the
       same server". One fewer thread reading memory, g_busy released in ~0ms, same answer.
       * A dead server id is the reason a hop hangs the CLIENT (Roblox shows its join screen
       and does not say the instance is gone: public servers last minutes, not hours). The
       card has always printed "fetched 8s ago"; now the number has a consequence. JoinRow,
       HopToOpenest and Rejoin go through ListUsable, which refuses a list older than 180s,
       fires a refresh so the next click has something current, and says which of the two
       happened. JoinRow also bounds the index against kMaxRows, not just against count.
       * Faster where it was silly: FillServers skips the row copy when nothing changed
       (24 rows x two snprintfs + a strlen per FRAME before; a list now reformats when the
       rows, the current row, the busy flag or the message change - the age and gates still
       update every frame).
       * Startup got a 25s network budget, one wall-clock deadline checked before each
       source in RefreshOffsetsImpl rather than a timeout per URL. It is the FIRST thing
       WinMain does, before the unhook and before the window exists, and "try all eight
       sources over two transports" on a machine that cannot reach them (no egress, an AV
       that intercepts TLS, a firewall that drops probes) multiplies out to minutes of no
       overlay and a log that says only "started" - indistinguishable from a hang, which is
       what it now sounds like when it is slow. The budget costs freshness only: the
       bundled fallback is generated from offsets.h for the client this build was made
       against. Each skip says so in the log with the source it gave up on.
       * Host tests: menu_shots 51 -> 64 checks, the new ones calling FillServers with a
       full page, with a View claiming 9999 rows and current=9998, and with a scribbled row
       to prove both directions of the skip (unchanged list leaves it alone, a new message
       re-copies). Fixture guids carry their index in the TAIL, since the tail is the half
       ShortGuid keeps and an all-zeros head cannot tell one row from another.
       servers_json_test is 33 checks (docs said 30); offsets_apply_test 63 and
       offsets_flow_test 30 unchanged and still green; all 26 Win32 TUs parse; the shots are
       pixel-identical (the draw clamp cannot move a 6-row list) so preview/img was not
       re-encoded.
       * Docs: ARCHITECTURE's world-page section is now "three 'the menu is lying' bugs",
       with this one written up in full including why it needed exactly 24 rows to fire.
       README's server hop row says what the age gate does, and §5's JobId example - which
       still read 0x118 - is now the current 0x120, marked per-release.

HEAD     OFFSETS FOR version-4310300497aa4917, MERGED NOT PASTED - AND A TOOL SO IT
         STAYS THAT WAY.
       * User: "i dont think the offsets are correct version it updated". The version was
       right (offsets.imtheo.lol still answers version-4310300497aa4917 today, the hash the
       paste was for) - the file was not. Pasting RbxDumperV2's header over offsets.h is
       three breakages wearing one: it deletes the ~20 fields the app reads that no dumper
       emits, its `constexpr` values cannot be written by RefreshOffsets() (offsets_live.cpp
       takes their addresses in kSlots[]), and its `0x0` answers - the dumper's "not found" -
       land where real offsets were. Build state after the paste: menu.cpp, offsets_live.cpp
       and features/movement/mover.cpp all failed; 3 of 26 TUs could not compile.
       * Offsets merged back properly, 30 values moved (25 from the dump, 5 derived): the
       DataModel chain +0x8 across the board (JobId 0x118->0x120, PlaceId 0x190->0x198,
       Workspace 0x158->0x160, GameLoaded, ServerIP, ToRenderView1...), all three static
       pointers (TaskScheduler 0x8b5cee8->0x8bdd8e8, VisualEngine 0x83f0a08->0x846f768,
       FakeDataModel 0x8dc2258->0x8e42c98), and the primitive's motion block A
       (Rotation 0xc8->0xb0, Position 0xec->0xd4, Linear 0xf8->0xe0, Angular
       0x104->0xec, Size 0x1bc->0x1c4, Flags, Owner). Block B is DERIVED as A + 0x48, so
       0xf8 / 0x11c / 0x128 / 0x134 - and the derivation was only allowed after the new
       dump's own Position-Linear-Angular deltas were checked against the +0x24/+0x30/+0x3c
       block model, which they still fit. 348 values did not move.
       * Six fields the dumper answers 0x0 for keep the previous cross-checked numbers, now
       tagged in the file (`// dump has no value; kept`): RenderView::{DeviceD3D11,
       LightingValid, SkyValid, VisualEngine}, GuiBase2D::AbsoluteSize,
       MouseService::SensitivityPointer. This matters more than it looks: the skybox
       changer WRITES RenderView's two validity bytes, guarded only by the address being
       mapped, so a stale one is a byte flipped inside the renderer rather than a failed
       read. README's skybox row moves from "Broken - stale offsets" to "Unverified" with
       that as the specific unknown, because the offset set is now for the running client
       and only this pair is a version behind.
       * New: glass-obsidian/tools/offsets_regen.py. Give it a dumper header and it merges
       the values into the app's field list, reports every move / keep / app-only field /
       dump-only field, re-derives block B (or refuses, naming the deltas that stopped
       fitting), and regenerates offsets_fallback.json from the result - so the shipped
       fallback and the header cannot drift, which they otherwise would have: the JSON
       still said 393 fields for the old version. Next release is one command, not a paste.
       * Cross-checked, as before, against a second source: the Offsetmanager mirror's
       External_Offsets.hpp for this exact hash agrees with the imtheo JSON on every value
       the app reads (the whole moved set - Humanoid, DataModel, Camera, Instance, Player,
       Primitive's block A, all three static pointers - and both report GuiBase2D::
       AbsoluteSize / MouseService::SensitivityPointer as absent). They disagree on
       Humanoid::PlatformStatePointer (0x4a4684ad vs 0x5b7b9ba1), which is a reminder that
       the un-guessable entries are guesses: nothing in this app reads it, and nothing
       should read liveness off HumanoidState either.
       * New guard: static_assert in offsets_live.cpp that the offsets are not const, with
       the message naming the tool. offsets.h compiles fine as constexpr until something
       tries to write it, which is why the build error named kSlots[] and not the cause.
       * New in the UI: the debug page's runtime card leads with an `offsets` row - the
       version the loaded offsets are for, and the version the client reports, from
       OffsetsLoadedVersion() / OffsetsClientVersion() snapshotted by RefreshOffsets().
       Green when they match, red as `... - CLIENT IS <hash>` when they do not, dim with
       `(client unknown)` when no version source answered. "Attached but every read fails"
       is now a label rather than a mystery, and every pointer row below it is only
       readable against it.
       * Tests: offsets_apply_test 47 -> 63 checks (the shipped-fallback block re-pinned to
       this version, plus block B's derived values, plus the kept zeros, plus offsets.h read
       as TEXT: no constexpr declarations, the dumper-less fields still declared, same
       version as the JSON). offsets_flow_test 30 checks, unchanged - its fixtures use
       synthetic hashes on purpose. All 26 Win32 TUs parse.
       * docs/offsets_dump.txt (their raw 392-line dump) is kept as the raw record; offsets.h
       is the app's file, and the difference between the two is now written down in both.

HEAD     THE CAMERA MOVES TO VISUALS; THE MOVEMENT PAGE IS ONLY MOVEMENT NOW.
       * Ask: "reorganize it like for example move fov changer to visuals since it is
       visual same with freecam." Applied with the rule from the last pass. FOV changer
       (+ field of view) and freecam (+ freecam speed) left the movement page and are the
       last group on visuals, named `camera`: everything above them on that page is drawn
       on the overlay, and this is the thing the overlay is drawn on. Movement's `view`
       group no longer exists.
       * Movement then split what was left: one group named after the page it is on, plus
       leftovers, is a bucket and not a filing. `movement` is noclip, flight (+ fly speed,
       hold to fly), walkspeed (+ speed) and infinite jump (+ jump power) - switches about
       how the character moves. `teleport` is click teleport (+ max range, land above by) -
       a switch about arriving at a place, which is also what the players page's GO and the
       world page's waypoints do with those same two numbers.
       * Keybinds groups read `menu / aimbot / movement / visuals` now. Every bind on that
       page is borrowed, so each group is named after the tab that owns the setting the key
       flips; "view" was the last group named after a category instead of an owner.
       * Rail counts moved with the cards again (visuals +2, movement -2), and the movement
       subtitle says where the camera went, because the question this raises is "where did
       my freecam go?" - and it gets asked on the page that used to answer it.
       * Two things deliberately NOT moved. The skybox changer stays on `world`: it writes
       to the place rather than to the screen, and its hero is that page's master switch -
       say the word and it joins visuals too. "draw fov ring" stays in the aimbot's `feel`
       next to `fov radius`, because that ring visualises a combat number, not the view.
       The new FOV card says so in 25 characters, which is what fits: the card ellipsises
       near 36 and a cut-off hint is where the information used to be.
       * Three hints were shortened for the same reason - they trailed into an ellipsis
       mid-sentence: the two latch hints ("Off: one press arms, one disarms" / "one press
       starts, one stops", kept as a matched pair because they are one idea) and freecam's
       "WASD to move, RIGHT MOUSE to look", which moved with the card.
       * Filing became a test rather than a taste. menu_ui exposes HasGroup("page/name"),
       built from the registry the pages fill as they draw, and the harness walks all nine
       pages before asserting: visuals/camera and movement/teleport exist, movement/view,
       keybinds/view and visuals/checks do not. Without the walk the negatives pass for the
       wrong reason, since every SetClosedGroups() rebuilds the registry. A picture of the
       page a card moved TO looks right either way - only the old tab can say it let go.
       * New shot t100-visuals-camera.png at an 820px window: seven collapsed rules are
       ~300px of chrome and the group is the last thing on the page, so the frame that
       clipped it would have proven nothing. Preview images re-encoded from it.
       * Harness: all ok, 51 checks, nine pages, page names stable, the camera group inside
       the window. All 26 Win32 TUs parse. No global moved, no config key renamed, no
       behaviour changed: same .cfg, same keys, and a stale "movement/view" entry in an old
       ui_closed_groups string is inert because closed groups are matched by lookup.

HEAD     MENU REFILED: EVERY SETTING ON THE TAB THAT DECIDES IT, LOUDER BUTTONS QUIETER.
       * Ask: "reorganize stuff to be more intuitive and correct to the tab, and remove
       subtext in some buttons in the tabs." The rule applied, then written into the
       preview gallery: a setting lives where the DECISION is made, and a tab that
       borrows it says so in the group name. Moves:
         - hitbox expander + its size: visuals "chams + hitboxes" -> aimbot, new `hitbox`
           group. Expanding a hitbox changes what your bullets hit; the visuals tab is
           where you change what is DRAWN. The draw switch stays on visuals (group renamed
           to `chams`) with one line saying where the expander went, and it keeps the
           shared size slider so nothing becomes unreachable from the tab you are on.
         - inventory checker: movement `view` -> visuals `extras`, beside "equipped tool",
           because it is the hover-only version of the same answer and it draws on the
           overlay. Movement's subtitle gained the camera, which is what its `view` group
           is now actually about.
         - "hide blacklisted" (blacklist_hides_esp): players `lists` -> visuals, in the
           group renamed `shared with aimbot`. The players tab owns WHO is on the list;
           whether the list also stops drawing them is an overlay decision.
         - the visuals `checks` group was named nothing: team check, wall check and now
           hiding blacklisted names are aimbot filters the ESP obeys, so the group says
           who else they belong to instead of leaving it to be guessed.
         - keybinds tab: `menu / combat / movement` -> `menu / aimbot / movement / view`.
           "combat" held one row and named a category the rest of the menu does not use;
           freecam and the inventory checker were "movement" because one of them moves
           you, which is true of half this tool and useless as a filing rule.
         - movement page: its `binds` group (six rows) is gone - the keybinds tab groups
           those same six, once. Two homes for a bind is two answers to "where do I set
           that", and the tab whose only job is binds is the one that keeps it.
         - flight's "hold instead of toggle" is "hold to fly", phrased like the aimbot's
           "hold to aim", because two wordings for one concept is how a user keeps
           looking for a setting that is already on screen.
         - the aimbot page's second "enabled" card is gone: it drove the same global as
           the hero above it, and the hero already falls back to a plain toggle card
           when hero cards are off, so nothing was lost by removing a switch that only
           existed to be disagreed with.
       * Rail counts moved with the settings they count (aimbot +hitbox expander, visuals
       +inventory checker +hide blacklisted -expander, movement -inventory checker),
       because a number that stays behind starts lying about the tab it sits next to.
       Renamed and removed group ids cost nothing in old configs: the closed-group list is
       matched by lookup, so an id that no longer exists is simply inert.
       * Subtext removed where it echoed the button instead of adding to it: "force
       refresh" under RESCAN (the button is a refresh and is greyed when there is no
       list to refresh), "into the sliders" under FILL, "the camera" under STOP (a name
       is the only thing that line carries, so when there is no name there is no line),
       and "both" under RENAME (four letters that explained nothing). What stayed: the
       selected player's name on GO / WATCH / BLACKLIST / WHITELIST, "pick a row" when
       nothing is selected, the waypoint name on PIN, "same session" / "id not listed" on
       REJOIN, the config name on SAVE / LOAD / DELETE, "most room" on HOP, the file count
       on OPEN, and the offset ranges on every debug RUN - those answer a question the
       label does not ask.
       * Verified the way UI is verified here: harness all ok with the bleed check on the
       densest page (visuals gained two toggles and lost one), t98 re-shot to show
       filters/hitbox/bind on one frame, every page PNG re-rendered and encoded,
       menu_ui.cpp parses as a Win32 TU. Behaviour is unchanged - no global moved, no
       key renamed, only which card draws it and what the rail says about it.

HEAD     DEAD CHECK: THE AIMBOT SKIPS CORPSES, AND LETS GO OF ONE IT IS HOLDING.
       * Ask: "add the option to not lock onto dead players". `dead_check` (default
       off, in the Filters group next to team check and wall check, persisted as one
       config key). The predicate is not a guess: `Offsets::Humanoid::HumanoidState`
       and `HumanoidStateID` exist and are refreshed live, but nothing here has ever
       verified which internal state id `Dead` is, and putting an unverified magic
       number into an aimbot filter is how you get a build that will not aim in one
       game and locks onto corpses in another. So it is the Humanoid health read the
       players list already labels with, lifted out of menu.cpp into
       `cache::EspEntity::IsDead()` so "dead" has one definition in the tool, and two
       filter sites in aimbot.cpp (skeleton search + ESP fallback) plus one release site.
       * `max_health > 0 && health <= 0`, and the left half is the reason it is safe to
       aim with: the cache reads health only when it found a Humanoid child, so one bad
       frame leaves both fields at 0 and `health <= 0` alone would call the whole server
       dead - locks released, target list empty, no log line saying why. Unknown is not
       dead. Somebody who actually left the game is already handled by the part
       validation in FindPartForPlayer, and that stays the only thing that ends a lock
       for absence.
       * The half nobody asked for but the same switch has to cover: with sticky aim, a
       target that dies under a held lock keeps its head part perfectly readable, so
       without a release the aimbot happily aims at the body until it despawns. The
       drop that already existed for "they became a teammate" now covers "they died"
       too, and the re-derivation in FindPartForPlayer honours it as well, or the drop
       would be undone by the very next refresh.
       * `tools/dead_check_test.cpp` (14 checks, no Windows, no game) pins the
       predicate - 0/100 dead, -4.5 dead, 0.001 alive, 0/0 unknown, the
       default-constructed entity the cache leaves behind on a missed humanoid is not
       dead, games whose max health is 0.5 or 250 both work, and nothing else about the
       player (name, tool, part count, team) changes the answer. The aimbot page's
       armed shot gained the filters row, so the card, its hint and the rail's count
       are all in one frame; 154 keys written, 154 read. aimbot.cpp/menu.cpp/config.cpp
       /menu_ui.cpp parse under check_win_tu.sh.
       KNOWN: unverified in-game like the other filter, for a different reason than
       wall check - there is no container layout to get wrong here, only whether the
       health read behaves as the players page already assumes it does, which one
       session with a friend standing over a corpse answers either way. Default off
       until someone confirms it.

HEAD     AIMBOT KEY AS A LATCH; THE PLAYERS BLACKLIST IS THE AIMBOT'S.
       * Two asks. `aimbot_hold_mode` (default on, so the key behaves exactly as it
       always did): off makes the bound key flip a latch - one press arms the aimbot,
       the next disarms it. Edge-detected in the feature and not in the menu, because
       the menu is a window that gets closed and a toggle that only works while it is
       open is not a toggle. Same shape as flight's `flight_hold_mode`, on purpose.
       * A latch you cannot see is a footgun, so the state is legible in three places:
       the page header says ARMED (warn) instead of active (ok), the hold card's hint
       says "latched on - press the key again to disarm", and the rail row's live dot
       tracks the latch, so it is readable from any page. The latch lives in aimbot.cpp
       next to the key it is derived from and reaches the UI through
       `Snapshot::aimbot_latched` - the menu never calls into a feature, which is the
       only reason it can be rendered headless at all.
       * Three ways the latch refuses to be clever: disabling the aimbot clears it (a
       latch that survives its own switch-off re-arms silently), rebinding the key or
       flipping hold/toggle clears it ("I rebound the key and it is somehow still on"
       should not be possible), and no key bound means always-on, which the latch never
       overrides.
       * `blacklist_hides_esp` (default off). The Players card was labelled "aimbot +
       esp", and the label was the problem: "do not aim at them" and "do not draw them"
       are two decisions, and a friend you never want to lock onto is still someone you
       may want to see through a wall. The list is the AIMBOT blacklist now - the button
       card and the scroll card both say so - and hiding those names from ESP is its own
       toggle beside them. 5 ESP call sites moved behind the flag; the 3 aimbot ones did
       not move, because that was never their question.
       * A bug the new shot found, and it was in the app: `menuui::SyncTheme()` re-bakes
       the font atlas between frames, and nothing told the DX11 backend. The texture the
       backend uploaded from the OLD bake stays current, and `ImFontAtlas::Clear()`
       zeroes TexID, so every glyph after a ui_scale or font-size change is sampled out
       of an atlas that no longer exists. overlay.cpp now calls the documented pair
       (Invalidate/Create device objects) when SyncTheme returns true. ARCHITECTURE 4.7
       is the rule; the harness had the same bug one level down - re-pulling the atlas
       after Render() is one frame too late - and that is what a page of solid rectangles
       in a PNG actually was.
       * Both settings persist (`aimbot_hold_mode`, `blacklist_hides_esp`). Preview
       gallery gained t98-aimbot-armed and t99-player-lists (the second bleed-checked
       like the layout shots), and every menu frame now re-tags the atlas first. The 5
       touched Win32 TUs parse under tools/check_win_tu.sh; harness all ok.
       KNOWN: none of this ran against a live client. The latch is ~25 lines of keyboard
       logic with no memory access in it, but "the key toggles" is a claim only a real
       session can close - the hold path is the shipped default until someone confirms
       it, and the disarm-on-disable/rebind guards are the reason a wrong answer there
       fails toward "not aiming" rather than "aiming when you did not ask it to".

HEAD     POLISH ON ONE SWITCH; NO CIRCLE BEHIND THE RAIL ICONS.
       * Two reports in one pass: "add an option in theme to turn off the pollish"
       and "when you click on the tabs near the logo there is like a weird circle".
       * `Material::polish` (glass-obsidian/include/obsidian/palette.h) is a master
       multiplier for the ten strengths, folded in ONCE where a palette is installed:
       `SetActivePalette` and `ApplyTheme` call `Material::ApplyPolish()`, which then
       resets `polish` to 1 so a palette cannot be flattened twice by being installed
       twice. No draw site changed, because none had to - every one of them already
       read a strength, and a strength of 0 already meant "do not draw", so polish off
       is flat card paper everywhere at once. `animate` and `speed` are deliberately
       outside the fold: freezing movement and removing shine are two questions, and
       the animate switch is documented as the one that "keeps the polish".
       * App side: `ui_polish` in globals.h, one config key, restored by RESET THEME,
       and a "polish" card at the top of the material group. BuildPalette folds it
       before it returns, not only at install, because the window border is decided
       from `p.mat.edge` inside that same function - a 1px border surviving "polish
       off" would be the one visible layer that ignored the switch. The ten dials keep
       their values, so the look is one you can leave and come back to; and dragging
       any of them while polish is off flips the switch back on, because the only
       reason to reach for a dial you cannot see is to want it, and an inert slider
       reads as a broken menu rather than as a disabled one.
       * The circle: `SidebarNav` drew a filled CIRCLE the radius of the whole icon
       box, plus a ring, behind the icon of the selected or live row. The row's
       selection animation drives it, so it appeared the moment a tab was clicked and
       read as a stray notification blob instead of as state. It is a rounded square
       sized to the glyph now, and only in icons-only mode, where the glyph is the
       entire row and state has to be legible as a SHAPE before it is legible as a
       colour; a labelled row already says "selected" with its lit tile, the sliding
       indicator and accent-tinted text, so a wash behind its icon only put a shape in
       the way of the eye. It inherits `mat.bloom`, so the new switch removes it too.
       * Proof: menu_shots gained t96-polish-off and t97-polish-on - same page, same
       three closed groups, so the pair is an A/B of the material and nothing else -
       plus four checks: the ten installed strengths sum to 0.0000 while polish is
       off; `animate`/`speed` are untouched; the ui_mat_* dials move by 0.0000 while
       it is off; and switching it back on lands 0.0000 away from what was dialled in,
       which is the one a "just zero the globals" implementation would fail.
       `POLISH=<0|1>` pins a whole screenshot session to the switch. The preview
       gallery gained the pair, and t95 (rendered last pass, never referenced).

HEAD     NO RPM/WPM ANYWHERE, ESP STROKES + TRACERS, external.exe.
       * Memory access (ask: "change it not to use wpm/rpm if it does and if
       possible, and another method to be less detected"): it already did not -
       `kernel32!ReadProcessMemory`/`WriteProcessMemory` were never called, every
       read/write goes to NtReadVirtualMemory/NtWriteVirtualMemory by address through
       the mapped-ntdll indirect-syscall layer. What DID invite the mistake was the
       naming: the wrappers were `nt::ReadProcessMemory`/`nt::WriteProcessMemory`,
       comments in mover/noclip/memory/menu said "WriteProcessMemory took ~29us", and
       the Architecture tree called memory.cpp "RPM/WPM wrappers". Renamed to
       nt::ReadVirtualMemory/WriteVirtualMemory (3 call sites in platform_win.cpp,
       1 in nt_syscalls.cpp, comments reworded to name the syscall instead of the API
       it replaced), and `nt_syscalls.h` now poisons both WinAPI names with a
       tripwire #define so reintroducing one is a compile error whose text explains
       itself (ROBLOX_ALLOW_WINAPI_RW opts out). README gained a "Memory access"
       section that states what this is AND what it is not (not kernel read/write, no
       handle-creator spoofing, QUERY_INFORMATION is enumerable) - the honest version
       of "less detected", because the remaining surface is not something a rename
       fixes.
       * ESP revamp. The renderer built lines by stacking 1px AddRectFilled rects, so
       "thicker" was impossible and every element had its own idea of a line: box,
       corners, health bar, skeleton and name label each hardcoded their own. Now
       EspWidth()/StrokeRect()/StrokeCorners/GlowRect are the whole vocabulary and
       three settings reach everything at once: esp_line_width (one weight for box,
       corners, skeleton, bar, tracers), esp_outline (the black backing), esp_glow (a
       dim same-colour halo - the difference between a lit line and a pasted one).
       The rotating fill gradient was 60 lines of hand-unrolled ImVec4 arithmetic in
       both branches of box_esp_type; it is Mix4 + Col4 now, one copy. Corner brackets
       get esp_corner_frac (a fraction of the box's SHORTER side, so a crouched player
       keeps four brackets that match each other). Name labels get esp_name_plate
       (solid or fading downward) because a 1px outline is not enough over a white
       skybox, and all ESP text sizes come from esp_text_size instead of a literal 12.
       * Tracers (ask: "more features such as tracers"): RenderTracer draws from a
       screen anchor (bottom centre / bottom left / bottom right / screen centre) to
       the target's FEET by default - the bottom of the box is where a Roblox
       character stands, and a line to the box centre reads as pointing at a floating
       head; head and centre are options, not the default. Colour follows the
       entity's team/whitelist tint unless you pick esp_tracer_color; the distance cap
       is in studs and hides only the line, not the box, so tracers can stay on as an
       "around the corner" layer; a dot at the anchor keeps several tracers reading as
       a fan of targets instead of one arrowhead. All 13 new esp_* settings round-trip
       through config.cpp, and the visuals page gained two groups (strokes, tracers)
       which the headless harness shoots and bleed-checks.
       KNOWN: the ESP/overlay draw code cannot be exercised here - no game, no GPU, and
       the harness has no stub for the memory it reads. The code parses under
       tools/check_win_tu.sh (all 26 TUs) and the menu side renders, but the pixels
       over a live server are unverified; the README's Features row for tracers says
       so. The projection it reuses (ProjectToOverlay) is the same one every other ESP
       element uses, so a wrong origin/anchor is the only realistic failure, and it
       would be visible as tracers pointing at nothing rather than as a crash.
       * Output binary: `glass-widget.exe` -> `external.exe` (vcxproj TargetName, one
       place as before). README/ARCHITECTURE updated. Window class and tray strings
       stay GlassWidget* - they are in the same single place, and the ask was the
       file name.

HEAD     MENU REVAMP: GLASS MATERIAL, NINE THEMES, AND A LAYOUT YOU CAN
       ACTUALLY CHANGE. User request: "revamp the external ui to be more glassy
       and beautiful with eye candy with a light and dark (very high contrast)
       themes as well as custom themes as well as rainbow. It should have a lot of
       customisability with the colours and what's able to be resized/expanded."
       The reference given was a GPU-shader liquid-glass ImGui fork (desktop
       duplication -> Dual Kawase blur -> HLSL refraction). That is not available
       here: the overlay is a WS_EX_LAYERED window with a per-pixel-alpha swapchain
       and no DXGI duplication, so there is no backdrop to refract, and DWM backdrop
       tricks do not apply to a layered window. The look is therefore rebuilt as
       *stackable CPU primitives* and every one of them got a strength dial, which
       is the same thing the ask wanted twice over ("eye candy" + "a lot of
       customisability").
       * glass-obsidian/palette: Palette gains Material (ten 0..1 strengths +
       animate/speed), `light` (polarity, not inversion - Lift() flips direction on
       it), `contrast`, and the four presets the old menu had no way to express:
       Liquid (obsidian with everything on), Ink (pure black/white, 15.5:1), Frost
       and Paper (authored light ramps, not inverted dark ones). TintSurfaces()
       mixes a colour into the SURFACES only; BoostContrast() sends surfaces to the
       extreme of their polarity, widens the steps between them, hardens hairlines
       and pulls the accent washes back - and never touches opacity, because that is
       a dial the user already set. Both were declared in the header and missing
       from palette.cpp; implemented here.
       * glass-obsidian/glass.cpp: the five new primitives (ChromaRim - 3-sample RGB
       dispersion along a rounded edge; SpecularSweep - travelling band clipped to the
       surface; FrostRing - inner ring that stands in for the blur we cannot do;
       AuroraBlobs - accent blooms drifting in the window; ShimmerBar - highlight
       travelling along a filled track), one animation clock (anim::Clock(), frozen
       when mat.animate is off so "stop moving" is free), and SetMetrics(base) so a
       host's density settings scale with ui_scale instead of fighting it.
       * glass-obsidian/axiom: Card_Hero (a page's master switch as a wide lit
       card), NavStyle (one SidebarNav behind labels / icons / hidden), GroupLabel
       + GroupLabelHeight (collapsible rules with a chevron and a live summary),
       PageTitle's shimmering accent rule, and the card/pill surfaces reading every
       material strength. Fixed two bugs found by the harness: GroupLabelImpl
       pushed an ImGuiID and never popped it (asserts on the first collapsible
       group submitted), and SidebarBegin's declaration had been deleted from
       axiom.h in an earlier edit.
       * roblox_external/ui/menu_ui.cpp: BuildPalette() is the single place the 54
       ui_* globals become a palette, run every frame, and ApplyLayout() the same for
       the metrics; the theme page is nine collapsible groups instead of five cards;
       the window's min/max/title height/corner/shadow are settings pushed per frame,
       with two-way size tracking so the user's drag survives them; hero cards, hints,
       the session card and each rail mode are switches, with the fallbacks drawn out
       (hero off = the same setting as a span-2 row; session card hidden in the icon
       strip because four label/value lines do not fit in 56px). The rainbow became
       five modes and moved OUT of the setting: AccentFromSettings() derives the
       travelling colour from the clock and never writes ui_accent_color, so a config
       saved mid-cycle stores the picked colour and switching off restores itself -
       the old bool cycle overwrote the global and restored a copy, which corrupted a
       saved config if you hit SAVE while it was moving.
       * roblox_external/imgui (vendored fork): BeginCombo painted a hard-coded
       #151515->#1C1C1C gradient, a #333333 border and white +/- text, ignoring the
       style entirely - it computed frame_col and threw it away. Any light theme got a
       black box for every combo, and obsidian's own FrameBg push was overwritten.
       Now it renders with the style's colours (border only when the theme asks for
       one); the library's combo helpers pass NoArrowButton so the themed chevron is
       the only arrow. Found from the headless render, not from Windows: this is
       invisible in a dark-only menu, which is presumably how it survived.
       * config: all 54 ui_* keys round-trip (WriteRGB/ReadRGB for the three-float
       colours, WriteStr/ReadStr for the closed-group list), and the old
       `ui_rainbow=0/1` values still parse - 0 is off, 1 is cycle - so nobody's
       config file goes blank. Found while doing it: `distance` (the ESP distance
       toggle) was written by Save() and never read back, so it could not be
       restored; the read line is added next to distance_color.
       * roblox_external/main.cpp: the fps and media float windows were plain ImGui
       with hard-coded white text and 4px rounding - fine while white was the only
       accent, wrong in every light theme. They now push the active palette through
       menuui::PushFloatWindowStyle(), and the media badge picks its label colour by
       Rec.709 luma like the library's chips do.
       * features/servers: the name-resolution job announced "place name from
       games.roblox.com" through the one-line status slot, which overwrote the
       actually-useful "list is up, your server id is not in it" message. Success now
       says nothing; the failure paths keep their messages.
       * Verification: the headless harness (glass-obsidian/build/menu_shots) gained a
       theme sweep over all nine presets that asserts polarity and legibility in
       numbers - ink on the correct side of its surface, text/panel >= 4.5:1, dim
       label >= 3:1 - plus hi-contrast (>= AAA and a 1.25x step over the same theme at
       default contrast), "the rainbow never writes back into the setting", the
       collapsed-group layout, and the two structural extremes (icons + 1 column at
       150%, rail hidden) against the off-window bleed check. 25 shots; preview/img/menu
       carries the encoded set and the preview page gained a palette section.
       glass-obsidian's own 13 window-state invariants are unchanged and still pass.
       Known limits: no real blur or refraction (see above), so frost and gradient are
       approximations of a backdrop that is not there; the max-width/height sliders are
       pixel caps, not per-monitor arithmetic; and a font-size change re-bakes the
       atlas only between frames (SyncTheme), so the new scale appears one frame later
       than the colour of a slider does.

HEAD     TEAM CHECK RECOLOR + WALL CHECK VS REAL GEOMETRY. User report:
       "team check and wall check don't work." Diagnosis: team check
       DID work - it erased teammates from the snapshots, so their ESP
       just vanished with no way to tell friend from foe. And wall check
       could not work: it only ever raycast against other players' cached
       body parts, because of a stale comment claiming "we don't have the
       world primitives list offset" (World::Primitives = 0x2b0 has been
       live-confirmed for a while - all offsets involved verified against
       the current dump before touching anything).
       * Team check: cache flags teammates (is_teammate on EspEntity/
         SkeletonEntity) instead of erasing them. ESP renders teammates in
         a configurable team_esp_color (box outline, name, skeleton, chams;
         colour picker on the visuals page; whitelist colour still wins;
         config-persisted). Aimbot skips flagged teammates in both target
         search passes and drops a sticky lock if team check is toggled
         mid-lock.
       * Wall check: world_detect.h (pure, header-only) auto-detects the
         physics world's primitive container layout at runtime - inline
         {begin,end} vector vs vector-behind-a-pointer - validating entry
         counts against DataModel::PrimitiveCount, canonical pointers and
         plausible Position/Size floats before trusting it. A dedicated
         slow sweep thread (never the 16ms cache thread) publishes AABBs
         for every world primitive that is not a character part and has
         CanQuery set; LineOfSightClear/LOSClear test world boxes first,
         live player bodies second. Boxes containing the aim point do not
         block (doorway/hugging-wall targets stay aimable). Adaptive
         cadence 1.5s + 2ms/part. On LAYOUT_NONE the wall check degrades
         to the old player-bodies-only behaviour, logs one line, and the
         new world probe (debug page) dumps the raw container so the
         layout can be pinned from real data.
       * world_detect_test.cpp: 13 checks against synthetic memory (both
         layouts, struct-array decoy, count-hint disagreement, NaN
         positions, tiny lists, unreadable world). Writing it caught a
         real robustness bug: detect() required both container qwords up
         front, which killed the indirect layout whenever the neighbouring
         field was unmapped.
       * cache.cpp joined check_win_tu.sh (now 26 TUs) - which immediately
         caught its MSVC-only strncpy_s(dst, max, src, _TRUNCATE),
         replaced with equivalent portable snprintf. (The project's own
         media_tracker.h turned out to be self-contained raw COM and parses
         against the stub Windows.h, so cache.cpp needs no special casing.)
       * Known limits, documented in ARCHITECTURE 4.6: rotated parts get
         their AABB, Terrain voxels are not primitives and do not block,
         sweep latency ~2s, container layout still needs live-game
         confirmation (hence the probe + the README's Unverified row).

HEAD     PLATFORM SEAM: every feature and core TU now compiles against
       platform/platform.h alone - no <Windows.h> outside the Win32 layer.
       Two goals, one seam: a Linux port becomes "implement platform_linux.cpp"
       instead of "rewrite the app", and the import tables of the feature
       code stop carrying GetAsyncKeyState/Sleep/CreateThread strings for
       signature scanners to anchor on.
       * platform.h surface: Init/FindGameProcess/GameBaseAddress/
         OpenGameHandle/SetGamePid/ProcessAlive/CloseHandle/ReadMem/WriteMem/
         AllocMem/FreeMem; NowMs/NowNs/SleepMs; StartThread (fire-and-forget,
         void fn(void*)) + StartJoinableThread/JoinThread + BoostThread/
         YieldThread; key::* (VK values - saved configs depend on them) +
         SampleKey/IsKeyDown/KeyPressed + mouse/cursor/DPI; ExeDir/CurlPath/
         OpenUrl (SE_ERR_* zoo moved into platform_win).
       * 19 feature TUs + memory/process/game/cache/offsets_live/main
         converted; Windows.h survives only where it IS the layer: main.cpp
         (WinMain), overlay/menu (Win32 UI), media_tracker (SMTC COM),
         config (FindFirstFileA), servers_http (WinHTTP+curl), nt_syscalls,
         platform_win.cpp. platform_linux.cpp compiles clean (-Wall -Wextra)
         and is deliberately not in the vcxproj. Known asymmetry, documented:
         AllocMem has no Linux side (skybox stays Windows-only, refuses
         rather than pretending).
       * Detection pass: window class/name/taskbar class -> GlassWidgetWnd/
         GlassWidget/GlassWidgetApp, tray+caption "Glass Widget", exe ->
         glass-widget.exe (vcxproj TargetName), main.cpp log strings
         de-roblox'd - each enumerable name in exactly one place. Indirect
         syscalls + fresh-ntdll unhooking untouched, per standing constraint.
       * check_win_tu.sh: default list extended from 14 to 25 TUs - the
         whole parseable app (all 19 features + menu/config/offsets_live/
         memory/process/game/platform_win). That extension caught what the
         hand-sweep missed: aimbot.cpp still had six DWORD timestamp stamps,
         esp.cpp still called MSVC-only sprintf_s twice (both fixed -
         uint64_t, snprintf). Stub grew the real Win32 names it needed
         (BYTE, _wcsnicmp, WideCharToMultiByte, window-enum/input/DPI
         surface); offsets tests stub ExeDir()/Init(). All green: 25/25
         parse, 47/30/33/16/243 checks, menu_shots renders - the 243-check
         pace test proves the mover's timing is byte-identical through
         the seam.
       * Paid lessons, now standing rules: never name platform constants
         after Win32 macros (key::XBUTTON1 vs #define XBUTTON1 - renamed
         MOUSE_X1/MOUSE_X2; and key::DELETE vs winnt.h's #define DELETE
         access right, which broke the first real MSVC build with C2059
         at platform.h:99 while the macro-less stub stayed green - renamed
         DEL, and the stub now carries the real bare macros so the parse
         check catches that class locally); lock-scope conversions leak
         reads past the closing brace (5 caught in mover.cpp by the parse
         check); grep the stub before adding to it (duplicate IsWindow).

HEAD     OFFSETS round 2, from the vendor's own docs: the service documents
       its flow at offsets.imtheo.lol/docs/cpp-auto-update, and that page
       gave up two things worth wiring in plus one thing worth diagnosing.
       (Our RefreshOffsets() already implements that doc step-for-step -
       version check, compare, fetch, cache. The doc's plain-WinHTTP example
       fails on the user's machine exactly like ours did: 12175 happens
       during the TLS handshake, before any HTTP byte - including the
       User-Agent - is sent.)
       * Pinned per-version endpoint: /{VER}/offsets.json exists (verified
         live), is deterministic (no version-check/dump race), and is now
         tried BEFORE plain /offsets.json as "imtheo-pinned".
       * Documented client UA: imtheo requests now send "OffsetsClient/1.0"
         via a new optional ua parameter on Get/GetViaCurl/GetAny; mirrors
         and Roblox API calls keep the browser UA.
       * 12175 decoded: on a secure-channel failure the error now carries
         WINHTTP_OPTION_SECURITY_FLAGS - "untrusted certificate chain" points
         at a local TLS intercept (AV/corporate proxy/root store), "the
         channel was cut" points at the remote dropping the hello. The bare
         number could not tell those apart; they have opposite fixes.
       * flow test 26 -> 30 checks (pinned tried when blocked, pinned-first
         + plain-unnecessary when imtheo works, doc UA actually sent);
         apply test unchanged at 47; check_win_tu.sh still 14/14.

HEAD     OFFSETS: the every-read-fails storm was Cloudflare, not the offsets
       pipeline. User symptom: attach OK, MZ read OK, then 600+ x
       0x8000000D (STATUS_PARTIAL_COPY) on VisualEngine::Pointer
       (base+0x8351408). That address is the e7d81637-era offset: Roblox had
       updated, and the tool never noticed because the offset DOWNLOAD was
       dead - offsets.imtheo.lol is behind Cloudflare bot protection that
       drops WinHTTP's TLS ClientHello outright (win32 12175 on
       WinHttpSendRequest, before any HTTP). The old main.cpp comment blamed
       the ntdll unhook and ordered startup around it; the fetch already ran
       first and still failed, so that theory is retired in the comment.
       * Multi-source fetch: version from imtheo -> ltseverydayyou/rdd
         (GitHub raw) -> WEAO Pages mirror -> jina reader; offsets from
         imtheo JSON -> Offsetmanager External_Offsets.hpp (GitHub raw,
         version-pinned URL {VER}, new ApplyOffsetsHpp parser - same format
         as offsets.h itself) -> jina reader. All verified live against
         version-c5aecda2245e4fae.
       * Second transport: GetViaCurl() runs System32\curl.exe (Win10 1803+)
         with piped stdout/stderr when WinHTTP fails - different Schannel
         ClientHello, same job. GetAny() = WinHTTP then curl; the Roblox
         server-list calls stay on plain Get().
       * offsets_fallback.json was NEVER SHIPPED: not in the vcxproj, so the
         "bundled fallback" path was dead code and installs fell straight to
         stale compiled defaults. Now a Content item (CopyToOutputDirectory),
         regenerated complete for c5aecda: 72 classes / 393 fields vs the old
         27-class subset, cross-verified imtheo vs cheatoffsets vs
         Offsetmanager (all agree; Humanoid::PlatformStatePointer differs per
         dump - it is a runtime pointer the dumper snapshots, and the tool
         does not use it).
       * offsets.h regenerated for version-c5aecda2245e4fae: 29 values moved
         (VisualEngine::Pointer 0x8351408 -> 0x83f0a08,
         TaskScheduler::Pointer -> 0x8b5cee8, Humanoid::WalkspeedCheck
         0x3bc -> 0x3b4, HumanoidRootPart 0x478 -> 0x470, ...), dump-zeros
         keep compiled defaults exactly like the live apply does, the 20
         tool-specific fields (Primitive motion-block extras, old
         MeshContentProvider chain) untouched.
       * RefreshOffsets() now: version-chain -> cache only when saved for the
         live version -> download chain (applies while validating, caches the
         raw body in either format) -> shipped fallback -> knowingly-stale
         cache with a loud line -> compiled defaults. When the live version is
         known but the dump is for another one it logs "WARNING - applied X
         but the live client is Y" instead of failing silently.
       * Tests: tools/offsets_apply_test.cpp (47 checks - both parsers, the
         sniffer, jina preamble strip, HTML challenge refusal, version
         extraction, and the shipped fallback end-to-end) and
         tools/offsets_flow_test.cpp (26 checks - the whole RefreshOffsets
         decision tree against a canned network, including this exact
         "imtheo blocked, mirrors answer" scenario). Both include the real
         offsets_live.cpp, not a copy.
       * Stub fixes that fell out: win32_stub/Windows.h gained ULONG/USHORT/
         PVOID/PWSTR/SIZE_T (12 TUs were failing the parse on those) and the
         child-process/pipe APIs; winhttp.h gained WINHTTP_OPTION_SECURE_
         PROTOCOLS. check_win_tu.sh now 14/14 ok (was 2/14).

HEAD     overlay: MSVC C3861 fix - install_keyboard_hook/remove_keyboard_hook
       forward-declared before run() (the definitions live further down the
       file next to the hook state; overlay.h parses top-to-bottom and the
       call site preceded them). The first real MSVC build of the hook.

HEAD     CLICK-TP round 1: three silent holes closed. User symptom: "you click
       and nothing happens" (log unchecked). The code logs a reason on every
       REFUSAL path, but three holes were wordless:
       * HOLE 1 (the likely one): WriteRootTo failure only set
         players_status_msg (a UI string on the PLAYERS page the click-tp user
         never looks at) and click-tp returned silently - "Something else is
         driving the character" / "Could not arm the teleport write" were
         invisible. It Refuse()s into the LOG now.
       * HOLE 2: clicks while the menu is open were swallowed wordlessly
         (menu owns the mouse by design, but silence reads as "broken"). One
         line per arm: "the menu is open, clicks belong to it - close the menu
         and click again".
       * HOLE 3 (correctness): the camera CFrame was TWO reads (Position,
         then Rotation); the freecam war proved the module rewrites the field
         EVERY frame, so the ray could catch position from one frame and
         rotation from the next - a bent ray, a teleport that lands off the
         clicked pixel. Now ONE 48-byte read (rot 0xd8 + 36 == pos 0xfc),
         layout-agnostic two-read fallback kept.
       * Claim audit: Teleport=4 is top Owner priority - nothing can block
         the claim; WriteRootTo's real failure modes are body/PushRoot, both
         now visible via hole 1.
       * mover_pace_test 240 -> 243: torn-read model (two reads CAN mix
         frames; one 48B read cannot), no-silent-paths model.
       * NEXT if it still does nothing: the log now names the exact hole -
         read the click-tp lines from one failing click.

HEAD     Freecam round 18: LEAD COMPENSATION REVERTED (user: "i noticed you added
       a weird smoothing i dont like that" + "still get the same flickering";
       round 17 was tried and rolled back via git revert 78f02ed - its HANDOFF
       entry went with it). The user was right on both counts: the design
       wrote the LEAD pose into the echo answers too - every answered frame
       sat AHEAD of the true position (the "weird smoothing" feel), while
       leaked frames still showed the trail (the lead cannot cover
       acceleration and turns). Flavor changed, leak unchanged.
       * STATE NOW = the round-16 state (which the user called "mild
         flickering, not much at all"): proper det=+1 basis, starve (nil
         subject), echo-sync feed mode at 20kHz/500us, clean handback with
         fresh-subject rescue + 600ms field guard, RMB look, pace test 240.
       * THE RESIDUAL FLOOR, stated honestly: ~one faint frame per second
         samples the ~50us detection gap and shows the module's smoothed
         (7.5-stud-trailing) pose. That is the physical floor of fighting a
         per-frame writer through the memory field from OUTSIDE the process.
         Levels below it: (a) render-side (Present hook - a bigger hammer,
         different toolkit), or (b) stopping the module in-process (instance-
         disabling does not stop a running RenderStepped connection; not
         viable externally). Offered as optional; default is freecam DONE at
         the current state.

HEAD     Freecam round 16: THE HANDBACK POISONED THE WELL - three fixes. User:
       "the flickering now is way worse". The log:
       * FR == OURS IDENTICAL in both FIRST FOREIGN MATRIX lines (det=+1
         both) - round 13's prediction held: the module now converges to our
         EXACT rotation; only its position smoothing trails. But the log
         also showed 30ms hold-thread starvation gaps and - decisive -
         "handed back - type=5 subj=0x0": the handback restored a NIL
         subject.
       * BUG A (the big one): cam::LookAtMatrix had the SAME LEFT-HANDED
         basis bug as round 13's Tick (right = world_up x fwd, up = fwd x
         right -> det=-1 MIRRORED). Round 15's handback started calling it:
         every freecam-OFF parked the camera with a MIRRORED rotation while
         the subject was ALSO restored nil - nothing to re-derive from - so
         the mirrored junk STAYED and each on/off cycle poisoned the next
         session. Fixed: right = fwd x world_up, up = right x fwd (det=+1,
         level look gives right=-x up=+y matching freecam's fixed basis;
         straight-up guard intact).
       * BUG B: the nil subject - Take can save subject=0 when the first
         claim races the module still assigning it; the old fallback did not
         RETRY. Stop() now resolves the local humanoid FRESH (character
         certainly exists by then), writes it, logs which one won.
       * BUG C (my regression): round 15's 40kHz/250us rate doubling made
         things WORSE (more syscall+lock load; the 30ms starve gaps).
         REVERTED to the 20kHz/500us state the user called "works
         perfectly". Also: hold-thread snapshots consolidated from 3 lock
         acquisitions/iteration to ONE (priority-inversion windows cut 3x),
         and the contest counters now reset on fresh arm (they were
         app-lifetime cumulative - the log's "41 total" after a restart of
         freecam was cosmetic confusion).
       * mover_pace_test 235 -> 240 (LookAtMatrix det/orientation/degenerate,
         subject resolution, kept/regression docs).

HEAD     Freecam round 15: HANDBACK + restore guard + faster feed sampling.
       User: "getting some minor flickering and when you turn off freecam
       you dont return to normal".
       * NOT RETURNING TO NORMAL: Restore() put type+subject back but the
         POSE stayed wherever freecam left it in the sky, and the default
         module spent the whole session starved (subject nil) - it may not
         snap back on its own. HANDBACK v2 in Stop(): after Restore, the
         camera is placed 11 studs behind the character (+5 up, at the last
         freecam yaw) LOOKING AT the character via cam::WriteLookAt - a sane
         third-person pose for the module to resume from. Then a 600ms
         RESTORE GUARD on the hold thread re-asserts the restored
         type+subject at ~100Hz (mover::GuardCameraFields; NEVER the CFrame
         - the game owns the pose again) in case the starved module writes
         one more frame of its own state. And the handback VERIFIES itself
         in the log every time: "freecam: handed back - type=%d subj=0x..
         pos=x,y,z" (cam::Restore now reports what it wrote via out-params).
       * MINOR FLICKER: feed-mode detector sampling 20kHz -> 40kHz (25us
         gap) and echo-answer cap 500us -> 250us - the residual is the
         detection gap; this shrinks it 2x.
       * mover_pace_test 231 -> 235 (handback placement + aim, guard
         ~60 reasserts/0 CFrame writes/silent unarmed).

HEAD     FREECAM COMPLETE + right-button look. User: "it works perfectly except
       my only problem is i want you to be able to use mouse button 2 to like
       turn it instead of your whole mouse - like how most free cams do".
       * Mouse look is now RIGHT-BUTTON-GATED (the Studio-freecam convention):
       deltas are always CONSUMED (TakeRawMouse is read-and-zero - nothing
         accumulates, no stored jump on press) but only APPLIED while
         VK_RBUTTON is held and the menu is closed. The pointer stays free
         for the game/OS while flying. Menu hint updated to "WASD anytime,
         hold RIGHT MOUSE to look".
       * mover_pace_test 227 -> 231 (gate truth table: not-held = consumed
         not applied; held = turns; release = stops, no jump; menu = menu
         owns the mouse).
       * The 13-round flicker saga is CLOSED: root cause was freecam's
         left-handed rotation basis (round 13); the default PlayerModule.
         CameraModule was re-deriving a proper rotation from our improper
         CFrame every render frame. Echo-sync feed mode + QPC cap + starve
         remain as armor and are harmless when the field is quiet.

HEAD     Freecam flicker, round 13: THE MIRROR WAS US. User: "still some mild
       flickering but see through is fixed" + the hunt's inventory named the
       enemy: StarterPlayer.PlayerModule.CameraModule - the DEFAULT Roblox
       camera stack (no custom game script). And the v2 forensic settled the
       rest: the foreign matrix is det=+1.000 orthonormal while OURS
       ([-1,0,0; 0,.97,.26; 0,-.26,.97]) is det=-1 - FREECAM'S BASIS WAS
       LEFT-HANDED (right=(cy,0,-sy), up=fwd x right -> right x up = +fwd
       instead of -fwd). Every freecam frame ever written was an improper
       (mirrored) CFrame, and the default CameraModule re-derived a proper
       rotation from it EVERY RENDER FRAME - the entire 13-round war was the
       engine CORRECTING us; the "inverted version" frames were our own
       mirrored matrix rendering, and the mirrored-pose culling was the
       see-through. (See-through already fixed by round 12's QPC cap; the
       mild flicker was the last mirrored-vs-proper flip.)
       * FIX: right = fwd x world_up = (-cy,0,sy); up closed-form
         (-sy*sp, cp, -cy*sp). det=+1, exactly orthonormal, look direction
         UNCHANGED (yaw/pitch semantics identical, screen-up still the sky).
         Plus a one-time det guard+log in Tick ("rotation matrix proper
         (det=+1)") so a handedness regression can never hide again.
       * With our pose proper, the CameraModule's per-frame re-derivation
         CONVERGES to our exact rotation - the rotation fight ends at the
         source; only its position smoothing (the 7.5-stud trail) remains,
         and echo-sync already answers that.
       * mover_pace_test 223 -> 227: old basis det=-1 (documents the bug),
         new basis det=+1 + orthonormal + same fwd + no upside-down.

HEAD     Freecam flicker, round 12: THE GRANULARITY BUG + the script hunt. User:
       "moves and works good then occasionally the same flicker; also the map
       and players look PARTLY SEE-THROUGH in freecam".
       * ROOT CAUSE OF BOTH (my bug): the echo-sync answer cap was "2ms" on
         GetTickCount - whose granularity is ~15ms. The cap actually allowed
         one answer per 15ms while the writer writes every ~8ms: EVERY OTHER
         write leaked a whole frame of its MIRRORED pose. Draw catching it =
         the occasional flicker; culling/LOD reading a det<0 mirrored pose =
         see-through map and players. The cap is now 500us ON QPC (real
         100ns resolution), and the detector samples at 20kHz in feed mode.
       * SCRIPT HUNT: on first freecam start, an inventory logs every
         script-class instance (LocalScript/ModuleScript/Script) under
         Players (+ LocalPlayer -> PlayerScripts), Workspace, StarterPlayer,
         StarterGui - depth 2, 25-line cap, once per session, read-only. If
         the mirror writer is a game camera script, the next log NAMES it
         and we disable it by instance; if the inventory is empty, the
         writer is engine-side and the next stop is render-side.
       * FORENSIC v2: first-foreign-matrix log now includes OUR rotation
         too, the foreign determinant and orthonormality error - settling
         mirrored-pose vs torn-read for good (real mirror = orthonormal,
         det<0; torn read = neither).
       * mover_pace_test 219 -> 223: both clock models (the GetTickCount cap
         answers 75/900 writes, the QPC cap 900/900), det classifier, hunt
         filter.

HEAD     Freecam flicker, round 11: FEED MODE REWORKED AS ECHO-SYNC. User: "when
       you turn it on its good but soon as you start moving theres a ton of
       flickering". The log explains it completely:
       * The writer is a SMOOTHER: stationary it converges on our setpoint
         (stable = "good"); MOVING it lags ~0.5s behind (45-stud drift lines),
         and our blind 30Hz setpoints survived in the field ~half a frame each
         -> the renderer coin-flipped fresh-vs-lagging = the "ton of
         flickering". Any blind paced write in feed mode recreates the coin
         flip; they are GONE. (The matrix forensic also landed: the foreign
         rotation is OURS WITH ONE AXIS FLIPPED - a mirrored/reflection pass
         the game runs through this camera mid-frame; that mirroring IS the
         "inverted version".)
       * FEED MODE = ECHO-SYNC now: the 10kHz detector sees each of the
         writer's writes land; we answer IMMEDIATELY after with our pose
         (rate-capped 2ms). The renderer samples the LAST value in the field -
         ours. No blind writes at all; the nil-subject reassert moved to a
         quiet 1s timer. Exposure window shrinks from ~half a frame to the
         <=100us detection gap (~1% of frames).
       * My telemetry bug fixed: "camera hold: write gap 33333us - starved"
         was the 5ms starvation detector firing on feed mode's (former) 30Hz
         period; the detector now only runs in hammer mode, and feed
         engagement resets the paced timestamp so a stale value cannot fake a
         years-long gap later. The >40-stud drift refresh loop (extra blind
         writes!) is gone entirely.
       * ENGAGE LOG now reads: "camera: FEED MODE (echo-sync) - the writer
         re-writes the field every frame (echo trails N studs); our pose now
         lands right after each of its writes, where the renderer samples it".
       * mover_pace_test 218 -> 219: zero blind paced writes in feed mode;
         13-for-13 echo answers vs a 120Hz writer; 2ms rate cap; no fake
         starved lines; lazy writer never engages.

HEAD     Freecam flicker, rounds 9-10: FEED MODE (the spectate doctrine, as the
       user prescribed on day one). The round-10 log settled the physics of
       the fight: foreign is ALWAYS exactly 7.5 studs from our held pose
       (7.48-7.55 on every line, even across 8s of motionless hover),
       rot=0deg on every line (rotation copied exactly), offset direction
       opposite travel and frozen when we stop. The writer ECHOES OUR POSE
       at a fixed 7.5-stud trailing offset - and it was caught ~90x/sec by
       our 10kHz sampler DESPITE our own continuous hot-mode writes: a
       kHz-vs-kHz interleave where the renderer coin-flips each frame.
       "Still flickering an INVERTED version" = the world seen from 7.5
       studs behind the flight path: parallax runs backwards. No write rate
       wins a coin flip.
       * FEED MODE: after 40 sustained contests (each gap < 200ms, i.e. a
         >=5Hz writer; the user's enemy engages in ~0.5s), the hold STOPS
         hammering and writes gentle 30Hz setpoints; the game's writer then
         renders a STABLE view - its echo of our setpoint, a chase-cam
         ~7.5 studs behind the path we fly. No rewrites on contest, no
         CONTEST log spam (the echo is the expected state); one health rule
         - a drift >40 studs off the setpoint refreshes it + re-asserts nil
         subject (rate-limited log). Freecam's 500ms deviation alarm stands
         down in feed mode (the offset is expected); hot mode never opens.
       * Round 9 also added: FIRST FOREIGN MATRIX one-shot forensic per arm
         (classifies TRANSPOSED / NEGATED / ROW|COL-NEGATED / MIRRORED /
         UNCLASSIFIED + logs the full 3x3) for future games; the user's
         build predated it, and rot=0deg answered the question anyway.
       * ENGAGE LOG: "camera: FEED MODE - the field has an unwinnable
         high-rate writer (echoes our pose N studs back); writing 30Hz
         setpoints and letting it render the view".
       * mover_pace_test 206 -> 218: classifier truth table fixed (the
         column-flip case was feeding an UNINITIALIZED matrix to the
         classifier - stack garbage; initialized now), feed engagement /
         no-rewrite / drift-refresh / lazy-writer-never-engages / 30Hz
         cadence checks.
       * IF feed mode renders smooth but LAGGY: that is the design (stable
         chase-cam at the writer's offset). If flicker persists EVEN IN FEED
         MODE: two writers or the renderer reads raw; next stop is the
         Present hook.

HEAD     Freecam flicker, round 8: STARVE HALF-WON + two self-inflicted bugs
       fixed. The user's log: subj stays 0x0 (our nil holds; the game never
       re-points it) yet contests continue ~100-120/sec -> the native writer
       IGNORES the CameraSubject field (computes from internal cached state;
       "branch C" as predicted). BUT d-from-character jumped 12.4-13.1 ->
       164-202: the foreign pose NO LONGER ORBITS THE CHARACTER - it now lands
       NEAR OUR OWN HELD POSE (one line: foreign 0.5 studs from ours). The
       starve broke the orbit; the residual writer now chases OUR pose (interp
       / predictor reading the CFrame we feed it) - hence "still a tiny bit".
       * SELF-INFLICTED BUG 1: the "subject detached" log line fired every
         feature tick (its once-flag was reset per re-arm) - 60 lines/sec
         spam drowned the user's log. BUG 2: every re-arm also did an INLINE
         WRITE from the feature thread, racing the hold thread (two
         generations of our own pose alternating = OUR micro-jitter), and
         wiped the flap/rival counters so the rival set never accumulated.
         FIX: FRESH-ARM GATING - re-arm only updates the desired pose under
         the lock; inline write + subject-nil + counter resets + the session
         log happen only when the hold was inactive or the camera changed.
       * Contest log grew rot=Ndeg (look-vector angle, foreign vs ours):
         rot~0 = the writer re-writes OUR pose (interpolator chasing our
         target); large rot = an independent viewpoint. This picks the next
         escalation.
       * mover_pace_test 200 -> 206 (fresh-arm gating x3, rotation delta x3).
       * NEXT LOG READS: contests with rot~0-5deg = interpolator - endgame
         is feeding it a pose it converges on (or accepting sub-stud jitter);
         rot large = a second view still alive - hunt the writer's source.

HEAD     Freecam flicker, round 7: ENEMY IDENTIFIED + starved. The user's log
       is unambiguous: NO flap lines (pointer never flapped - flap/rival
       theories dead for this game), but CONTEST counts of 118-159/SEC,
       continuous, ~3000 total in one flight. Every foreign pose sits
       d=12.4-13.1 studs from the frozen character = the follow-cam ZOOM
       ORBIT distance; type left at 6 (Scriptable), subj=0x24669A1A880
       stable. That rate is >60Hz - not a Lua script; it is per-RENDER-frame
       native camera code (118-159fps = a high-refresh monitor). It writes
       the orbit CFrame regardless of Scriptable, in the render phase - our
       ~100us re-win still lost a few frames/sec = the remaining flicker.
       (Round 4's sparse 6 contests vs this session's 3000: the writer's
       activity differs per session/game state; irrelevant now.)
       * PRONG 1 - STARVE: the orbit writer computes from CameraSubject;
         the camera hold now writes subject=0 (nil = the legal cutscene
         pattern; nothing to orbit) at EVERY paced write + inline on arm,
         logged once. cam::Take saved the original subject and Restore()
         writes it back (or falls back to our humanoid). Release-before-
         -Restore order already guarantees no fight on handback.
       * PRONG 2 - OUT-RATE: a confirmed contest opens the pacing gate for
         1s (s_cam_contest_hot_until): camera writes go full thread speed
         (burst doctrine), shrinking the writer's render-phase window ~10x.
         Quiet for 1s -> paced again.
       * mover_pace_test 198 -> 200: nil subject starves the orbit writer;
         contest opens the gate (unpaced > paced ceiling).
       * READ THE NEXT LOG: contests gone = starved (flicker should be too);
         contests continuing with subj re-pointed non-zero = the game re-
         asserts the subject (the paced nil re-write fights it); contests
         continuing with subj=0 = the writer does not read the subject field
         (computes from internal state) -> next escalation is finding the
         writer's code path.

HEAD     Freecam flicker, round 6: THE RIVAL SET. User: "theres less flickering
       but still some" - exactly the round-5 residual: the guard wrote our
       pose into a rival camera ONCE on detection, but the user keeps flying,
       so that pose went STALE - frames rendered through the rival flipped
       between FRESH and STALE freecam views (looks better than
       freecam-vs-character, hence "less", but still a flip). And flips
       shorter than the 100us sampling window rendered the rival untouched.
       * FIX: every camera the pointer has ever flapped to is remembered
         (rival set, cap 4, under s_hold_cs) and every PACED write lands in
         the armed camera AND every rival - a flip never even sampled still
         renders the CURRENT pose. Dead rivals (failed write, e.g. respawn)
         prune themselves. First sighting of a rival logs
         "camera: rival camera 0x.. added to the pose set".
       * WriteCamUnit now returns bool (prune signal); pin-back + flap
         counting unchanged; rivals cleared on arm and release.
       * mover_pace_test 195 -> 198: rival carries the pose; pose change
         refreshes the rival - a never-sampled flip renders the fresh pose;
         set dedupes and caps.
       * IF "still some" persists after this: remaining suspects are (a) the
         event-driven orbit writer's occasional single-frame wins (CONTEST
         lines - unavoidable without render-phase hooking) and (b) render-side
         view state. The log's FLAP/CONTEST counts decide which.

HEAD     Freecam flicker, round 5: THE FLAP GUARD. User answers: flicker is
       STILL CONSTANT, and NOT scroll-correlated. That plus the round-4 log
       (CFrame field quiet - 6 contests total - type stable, identity
       "stable") forces the deduction: the rendered view is not always read
       from the camera we hold. The one mechanism that alternates per frame
       INVISIBLY to every instrument we had: the Workspace.CurrentCamera
       POINTER flapping A->B->A inside a single frame (the feature-tick
       watchdog samples at ~60Hz and cannot see a within-frame flap; the
       renderer samples the pointer live). A game rendering a viewmodel or
       scope pass through a second camera does exactly this.
       * THE COUNTER: the hold thread now reads Workspace.CurrentCamera at
         10kHz (own QPC timestamp). On any flap it FIRST writes our full
         type+CFrame unit into the OTHER camera - so even a frame rendered
         through B shows our view - THEN pins the pointer back to ours, and
         logs "camera: CurrentCamera FLAPPED to 0x.. - our pose written into
         it, pointer pinned back (N recent, M total)" (first immediately,
         then 1/sec). Kill shot: both cameras carry our pose.
       * Plumbing: cam::WorkspaceAddr() exposes the cached Workspace address;
         HoldCameraCFrame gained an optional workspace_address param (freecam
         passes it; 0 = guard disarmed). Flap counters reset on arm, cleared
         on release. Workspace::CurrentCamera offset = 0x498.
       * mover_pace_test 192 -> 195: stable pointer = silence; a flap = pose
         into B + pin back; a per-frame flapping game = ~30-60 pins/sec,
         every one counted.
       * IF the flicker persists with ZERO flap lines and quiet contests:
         pointer is stable and the field is quiet - then the view flip lives
         in render-side state (scene view matrix) and the next hunt moves to
         the overlay's Present hook level.

HEAD     Freecam flicker, round 4: FIRST CAPTURE of the enemy. The user's log:
       "freecam: on" -> 4 CONTEST lines (6 contests total: pairs 1 / 2,3 /
       4,5 / 6) -> "freecam: off". Analysis: the foreign poses CLUSTER
       (x 57.7-59.8, y 9.2-12.7 = ground + camera height, z drifting
       -87 -> -95) while OUR held poses roam x 17-67, y 7-28 - the foreign
       pose is an ORBIT pose around the standing character. Crucially the
       count: 6 writes in a whole session = EVENT-DRIVEN (4 events x 2
       writes each), NOT a 60Hz script (that would read recent:60). An
       event-driven orbit writer that fires regardless of Scriptable:
       prime suspects = mouse-wheel ZOOM recompute, respawn/death camera
       recenter, or a game script's occasional camera nudge - and because
       RenderStepped-phase writes land just before the frame is sampled,
       our ~100us re-win can still lose THAT one frame: each surviving
       write = one visible snap to the character and back, which matches
       "still getting flickers".
       * Contest log now CLASSIFIES (reads rate-limited to the logging
         branch only): the CameraType the writer left behind, the
         CameraSubject pointer, our character's live position, and the
         foreign pose's distance FROM the character - an orbit/zoom writer
         sits ~8-14 studs from it; a transition/cutscene writer sits
         elsewhere. Next log names the culprit.
       * mover_pace_test 191 -> 192.

HEAD     Freecam flicker, round 3: the contest detector (10kHz read-back on the
       hold thread). The user's log settled it: type STABLE at 6 with no swap
       and no sustained deviation - a frame-level fight the 500ms alarm is
       structurally blind to. Prime suspect: a game script writing cam.CFrame
       directly in RenderStepped regardless of CameraType (its computed pose
       IS the character orbit = "snaps to your character"). Our 4kHz hammer
       out-writes it 66:1, but a write landing in the last sliver before the
       frame renders wins that frame = one snap, repeat = flicker.
       * The hold thread now READS THE CAMERA BACK at 10kHz (own timestamp,
         only when 48B-contiguous) and classifies memory against the DESIRED
         pose and the LAST POSE WE WROTE (s_cam_written baseline, updated by
         both write paths): desired present -> baseline refresh (feature
         inline write landed); old-written while desired moved on -> the
         pending pose is written NOW unpaced (mouse updates land in ~100us,
         not up to 250us); NEITHER -> suspect, confirmed by an immediate
         second read (torn reads do not repeat), then REWRITTEN unpaced and
         NAMED in the log: "camera: CONTEST - another writer set the pose to
         X (we hold Y); rewrote. N recent, M total" - first one immediately,
         then at most 1/sec. An outside writer now wins ~100us, a fraction of
         one rendered frame, instead of whole frames.
       * Starvation telemetry: any >=5ms gap between camera writes while the
         hold is active logs "camera hold: write gap Nus - the hold thread
         starved" - the self-inflicted flicker would name itself.
       * freecam now logs "freecam: on (camera 0x..)" / "freecam: off" so
         reports correlate with sessions.
       * Also read off the user's float probe: the second CFrame at 0x108
         (identity rot + pos ~12 studs toward the character) is Camera.Focus
         (audio/streaming target), NOT a second view camera; and CameraType
         is an INT field (float-printed as 0.000 in the probe - no
         contradiction with "type = 5").
       * mover_pace_test 184 -> 191: steady hold silent; foreign pose
         surviving the confirming read = contest + our pose re-baselined;
         torn read != contest; inline-write refresh; pending update written
         unpaced; a 60Hz writer counts as ~60/sec.

HEAD     Freecam vs the two-camera question: identity watchdog, swap-follow,
       and a deviation alarm (evidence, not theory).
       * The user's hypothesis - "does it create another camera so there's 2?"
         - is now answered by data: cam::Current caches the WORKSPACE (the
         expensive walk, 2s) and resolves CurrentCamera as ONE pointer read on
         EVERY call, logging every identity change
         ("camera: Workspace.CurrentCamera changed 0x.. -> 0x.."). Freecam
         re-seeds its pose from the new camera the tick it changes - the old
         400ms camera cache could hammer a DEAD camera for 400ms per swap
         while the renderer drew the new one (the game's view): exactly one
         back-and-forth flicker per swap, and a flapping game flickers
         continuously.
       * DEVIATION ALARM: if the camera position reads >2 studs from what we
         wrote on two consecutive ~500ms checks, one line lands in the log
         with both numbers ("another writer is moving the camera - reads X, we
         wrote Y"). The next "still happening" arrives with the culprit named.
       * WHY SPECTATE NEVER FLICKERS (the user's suggestion, investigated):
         spectate never writes the camera at all - it feeds the game's own
         module (CameraSubject + Humanoid::CameraOffset) and the module does
         the positioning every frame. Freecam cannot fully adopt that
         principle (an arbitrary pose is not feedable through a subject), so
         it fights - but now it fights instrumented.
       * mover_pace_test 178 -> 184 checks: swap re-seeds within a tick, the
         dead-camera window is gone, the alarm truth table (isolated deviation
         = no alarm; two consecutive = named), per-tick resolve cost.

HEAD     Freecam flicker fixed: CameraType and the CFrame are re-asserted as
       ONE UNIT on the hold thread.
       * Symptom: "the camera goes back and forth from your character and
         then back to where it was super fast" - two writers in the same
         frames. Cause: the CFrame was hammered, but CameraType was re-asserted
         from the feature loop on a 150ms cadence ("Scriptable sticks, 7Hz is
         plenty"), and the game's camera script re-asserts Custom on its own
         RenderStepped: for up to 150ms the DEFAULT camera module wrote the
         character-orbit pose while our hammer wrote ours. The CameraType
         test's "sticks" was true of the FIELD, not of a live game's script
         fighting back.
       * FIX: mover::HoldCameraCFrame now writes CameraType=Scriptable and the
         CFrame together on every paced iteration (4kHz - the module can hold
         the type for at most a fraction of one 60Hz frame), inline on arm as
         well. cam::Take remains the claim/save/verify, called per tick. The
         camera hold also got its OWN pacing timestamp - it was sharing the
         position hold's, so a concurrent teleport/flight hold halved the
         camera write rate.
       * mover_pace_test 176 -> 178 checks: type+CFrame as one unit (60x+
         re-asserts per camera-script frame), independent pacing timestamps.

HEAD     Freecam fixed: the CFrame hammer moves to the dedicated hold thread.
       * Freecam had the exact disease every movement write was cured of, but
         in miniature: its camera CFrame was hammered from the FEATURE LOOP -
         the one Sleep(1) thread shared with ESP, aimbot and servers - so
         whenever the loop was busy the write rate collapsed, the game's
         camera module re-asserted between our writes, and the view stuttered
         or briefly orbited the character. The CFrame is now re-asserted from
         the DEDICATED HOLD THREAD (new mover::HoldCameraCFrame/ReleaseCamera-
         Hold: 12 floats = rot9+pos3, single 48-byte write at Rotation while
         Rotation+36==Position holds, two-write fallback if an offset update
         ever breaks adjacency; paced with the position holds; re-armed every
         feature tick; released BEFORE cam::Restore on stop so it never fights
         the restore).
       * CameraType re-assert paced to ~7Hz: the CameraType test proved
         Scriptable STICKS (wrote 6, read 6 for 500ms), so 600 writes/s were
         pure waste.
       * KEYBOARD gating: menu-open -> TEXT CAPTURE (the policy infinite jump
         learned): the menu starts open, and "WASD does nothing until you
         close it" reads as freecam being broken - you can fly with the menu
         up; only a text field eating the keyboard freezes movement. The
         MOUSE stays menu-gated (menu open = the mouse is navigating the
         menu). Space reads the shared single poll (g_input_space_down), not
         another GetAsyncKeyState call on the key.
       * Movement page hint updated: "WASD anytime, mouse needs menu closed".
       * mover_pace_test 168 -> 176 checks: camera-hold write shape (48-byte
         adjacency, fallback, race threshold), input gating truth table,
         release-before-restore.

HEAD     Holding space is not spamming space: the auto-repeat phantom, gated.
       * "hold down space and it just keeps going up without having to
         continually press" - the layer-2 GAKS edge-bit supplement was the
         culprit: Windows re-sets the "pressed since the last call" bit for
         as long as a key is held (AUTO-REPEAT), so the ungated OR turned one
         real press into an endless stream of phantom ones - each stacking
         the rise speed and restarting the window. The hook was never the
         leak (it already filters repeats via the previous-event latch).
       * FIX: the edge bit is honoured only on a FRESH DOWN TRANSITION - the
         sampled LEVEL was up at the previous iteration. Repeats can never
         pass (held = the level was down at every prior sample); the stall
         coverage survives (a press+release entirely inside a stall leaves
         the level up on both sides, so the bit still fires); the initial
         press of a hold still fires once (a real transition).
       * mover_pace_test 165 -> 168 checks: hold fires ONCE across 2s of
         auto-repeat; ten hook-caught presses fire ten times; a
         stall-embedded press still fires through the gated bit.

HEAD     Infinite jump: presses STACK - the "spam doesn't register" that was
       not a detection problem at all.
       * The user's spam still missed AFTER the hook + dedicated pump thread +
         queue-OR-GAKS detector, and the real reason was semantics: a press
         inside a live 400ms window merely RESTARTED the clock on a pin
         already at full power. Velocity did not change, nothing could be
         felt, and because spam keeps the window alive indefinitely, total
         height grew with TIME, not PRESSES - 10 fast presses felt like 3
         worked even with all 10 detected. The debug test never showed this
         because its button blocks for the whole 400ms run: you cannot click
         mid-run.
       * FIX: each press during a live window ADDS to the pinned rise speed,
         capped at 3x the slider (press -> power, -> 2x, -> 3x). Every press
         is instantly perceptible; spam = visibly faster climb. A fresh
         window after expiry starts at the slider again.
       * The burst summary line now reports the PRESS COUNT and the peak rise
         speed ("6 presses, ... rise speed peaked 300, ..."), so the next
         "didn't register" report separates detection from effect on its own:
         all presses counted + no extra height = a game-side clamp; presses
         undercounted = input.
       * The user's Roblox-jump-cooldown hypothesis is ruled out structurally:
         nothing goes through Humanoid.Jump - the write is raw
         AssemblyLinearVelocity, which no cooldown gates (and the debug log
         shows bursts 400ms apart mid-air all climbing).
       * mover_pace_test 158 -> 165 checks: the stacking model (slider, +slider
         per press, 3x cap, reset after expiry) and the restart-only
         regression guard.

HEAD     Space spam, layer 2: the hook pumps on its own thread and the
       detector is queue-OR-GAKS-edge.
       * Residual misses after the hook: it was serviced by the RENDER loop's
         PeekMessage pump, and Windows delivers LL-hook events as messages to
         the installing thread, silently dropping them (and eventually the
         hook) when the callback does not return within
         LowLevelHooksTimeout - a render hitch is exactly such a stall. The
         hook now runs on a DEDICATED thread doing nothing but
         GetMessage/DispatchMessage, so callback latency is decoupled from
         anything the renderer does and the queue cannot starve. (Install/
         remove: CreateThread at run() start, PostThreadMessage(WM_QUIT) +
         wait at shutdown.)
       * DEFENCE IN DEPTH in the detector: the LEVEL comes from GAKS (its
         level bit is always truthful about the current instant and immune to
         a hook dying silently - a frozen 'down' would eat every later press
         through the was_down latch); PRESSES = hook queue OR the GAKS edge
         bit. The edge bit alone is unreliable, but OR-ed in it can only ever
         ADD a press the queue missed, never lose one - and a duplicate edge
         restarting a live 400ms window is invisible.
       * infjump pending-latch 150ms -> 250ms: under load the cache can miss
         the player for several passes, and dropping a real press to a slow
         cache pass reads as exactly the spam-miss being fixed.
       * mover_pace_test 153 -> 158 checks: the layered detector's truth
         table (queue caught / GAKS caught / both / neither) and level-from-
         GAKS.

HEAD     Flight: gravity zeroed while flying (the float-down was arithmetic).
       Space spam: presses now arrive by QUEUE (a low-level keyboard hook),
       not by poll.
       * FLOAT-DOWN, THE REAL MECHANISM: the solver applies workspace gravity
         to a pinned vy=0 EVERY step and integrates the result before our
         next write resets it - a constant ~1.6 studs/s sink that no write
         rate can remove, because it is not a race, it is arithmetic. The
         user's suggestion was correct: remove the gravity term. Flight now
         zeroes World::Gravity (via Workspace::World 0x368) for the duration -
         only a slot that READS like gravity (~196.2, scalar or vector form)
         is written; the original is restored on stop ONLY if the slot still
         reads ~0 (a world that changed underneath - place hop - is left
         alone). Unrecognised layout -> logged, flies on the pin alone.
         With the term gone: no keys = a TRUE hover, motion is exactly the
         pinned vector.
       * SPACE SPAM (the third and final attempt): the edge-bit fix failed
         live because GetAsyncKeyState's "pressed since the last call" bit is
         system-wide and DOCUMENTED AS UNRELIABLE - any other process polling
         space (the game, other overlays) consumes it between our calls; the
         level bit misses presses inside loop stalls. So presses now arrive
         by queue: overlay.h installs a WH_KEYBOARD_LL hook (callbacks are
         delivered through the render loop's PeekMessage pump; the callback
         only touches atomics; repeats are filtered by tracking the previous
         event) counting every physical press into
         g_input_space_hook_presses. mover::Tick drains it once per
         iteration into g_input_space_edge. Ten presses inside one stall =
         ten queued events = the burst fires; nothing any other process does
         can consume them first. GAKS level polling remains only as the
         fallback when the hook fails to install (logged).
       * mover_pace_test 143 -> 153 checks: the queue model (level catches
         0/10 in a stall, the hook carries 10/10, repeats don't count), the
         sink arithmetic that justified gravity-zero (~1.64 studs/s at
         default gravity), gravity layout recognition (both signs, zeros and
         small floats rejected), and the verified-restore policy.

HEAD     Spam-proof space, jump power RESTORE, unpaced bursts (flight flies
       again), fly speed max 1000.
       * SPAM MISSES ("press 10 times, only half fire"): the feature loop
         shares a thread with ESP/aimbot/servers and stalls for tens of ms -
         a press+release entirely inside a stall is invisible to a level-only
         poll. Fix: space is now polled ONCE per iteration in mover::Tick
         (g_input_space_down / g_input_space_edge - the "pressed since the
         last call" bit survives a stall by construction), because the edge
         latch is consumed by ANY call on the key and flight's steering poll
         was starving infjump's detection. A press arriving during a cache
         hiccup is LATCHED (150ms) and fired when the body resolves instead
         of being eaten.
       * JUMP POWER RESTORE (the user clarified the wish): toggling infinite
         jump off now writes the humanoid's ORIGINAL JumpPower/UseJumpPower
         back - saved before our first write, re-asserted at 4Hz for 3s
         (replication races a single write), dropped after a respawn (the
         new humanoid arrives with its own). The previous keep-alive was a
         misreading of the report.
       * FLIGHT "bounce around / float down": two self-inflicted wounds on
         the velocity pin. (1) The burst hammer was paced to the position
         hold's 4kHz cap - BELOW the slowest working pin rate (the debug
         test's slowest successful run wrote 2.4/ms), so the humanoid damper
         clawed velocity back between writes: floating down instead of
         hovering, bouncing while steering. Bursts are UNPACED again
         (position holds keep the cap - they are indefinite; bursts are
         400ms-deadline and need everything the thread can give). (2)
         PlatformStand made the character LIMP while velocity-flying - a
         ragdoll dragged by its root part, tumbling off every bump ("you
         just bounce around"). Removed; infinite jump's clean pin runs with
         the controller fully active.
       * Fly speed slider max 250 -> 1000.
       * mover_pace_test 135 -> 143 checks: jump-power restore policy, the
         sticky-edge-vs-stall model (10 presses inside stalls: level-only
         catches 0, the edge bit catches 10), bursts-must-not-be-paced,
         no-PlatformStand-while-flying.
       * win32_stub grew SHORT (the shared poll's return type).

HEAD     Flight: pin velocity, never write position (the rubberband, killed at
       the root). Jump power slider max 500.
       * THE RUBBERBAND PERSISTED through the rotation-echo fix, the pacing
         fix, and the carried-velocity fix - because all of them still wrote
         POSITION 66 times per step. Overwriting the engine's own integration
         that fast invalidates the assembly transforms, render interpolation
         and replication history faster than they settle; every internal
         re-derive is a visible pop. You cannot out-write the engine on its
         own output and stay smooth.
       * THE MECHANISM THAT IS PROVEN SMOOTH: the debug test's hammered
         velocity climbed +25 studs in 375ms, perfectly monotonic - because
         the ENGINE integrates a pinned velocity and therefore owns a
         consistent position. Flight now feeds that exact machinery
         (mover::VelocityBurst, shared with infinite jump): direction x speed,
         re-fed every 25ms, 500ms deadline (rides any cache hiccup un-fed),
         Stop() on toggle-off -> gravity resumes. No keys = velocity 0 = hover
         (the pin counteracts gravity; the classic fly-script feel). The
         position-hold path is now a teleport-only tool.
       * TELEPORT interplay: a teleport outranks flight, and flight stops the
         burst for the duration of the hold (a pinned velocity would fight
         the position writes); flight then resumes from wherever the teleport
         landed you - with the engine owning position there is nothing to
         re-seed, which is what makes the handoff clean.
       * Jump power slider max 150 -> 500 (movement page), matching the
         feature's 500 clamp.
       * mover_pace_test 131 -> 135 checks: the pinned vector's magnitude is
         the fly speed, no axis exceeds it, idle = hover-not-fall, feed
         cadence vs deadline vs hiccup widths, teleport outranks flight.

HEAD     Flight rubberband-then-snap fixed for real; jump power now persists.
       * THE RUBBERBAND: "the player rubberbands a bit and then snaps back to
         how we have our flight". The hold pinned position but ZEROED
         velocity, so the replicator forwarded "standing still" every tick;
         the server's copy of a fast-moving character lagged further and
         further behind ours, and every replication restore yanked the
         character BACK along the path until our next write snapped it
         forward. Worse the faster you fly - a race we could never win by
         writing position harder, because the thing being restored FROM was
         stale, not wrong.
       * FIX: HoldPosition(who, pos, vel) - the hold writes the caller's
         velocity alongside position. Flight passes its exact path velocity
         (direction x speed, the same rate it integrates position with), so
         (a) the engine's own integration between our writes moves the
         character along our path - nothing to snap back from - and (b) the
         replicator forwards a velocity the server dead-reckons with, so the
         restore target tracks us. Teleports still pass nullptr = zeros (no
         motion through skipped space). This is the old "velocity-driven
         flight" insight (velocity is what the replicator forwards) combined
         with the position hammer - each covering the other's failure mode.
       * JUMP POWER PERSISTENCE: "when you turn off infinite jump your jump
         power just revert to normal" - nothing restored it; the SERVER
         replicates the humanoid's original JumpPower back, so the old
         once-per-burst write decayed within a minute. Now re-asserted
         continuously (4Hz) while enabled, and after the toggle goes off the
         power STAYS on the humanoid it was written to until that humanoid is
         gone - a respawn hands out a new one and is the natural reset. The
         per-burst humanoid write is gone (subsumed).
       * mover_pace_test 124 -> 131 checks: hold-velocity == position
         integration rate, no axis exceeds the speed, teleport still zeros;
         jump-power policy (asserts while enabled, kept while off + same
         humanoid, hands off after respawn).

HEAD     Flight camera smoothing + click-tp/waypoints fixed (player tp's path,
       hardened).
       * FLIGHT "bugs your camera a bit" - two causes, both gone:
         1) the position hold wrote the WHOLE motion block, echoing a rotation
            captured once per feature tick; between ticks the hammer re-asserted
            that stale rotation thousands of times per step and fought the
            engine's own rotation updates (auto-rotate while steering). The
            hold now writes the block TAIL only - position + both velocities,
            block+0x24/36 bytes - the exact "never echo an output back" rule
            WriteVelocity already followed, one field over. Rotation during a
            hold belongs to the engine.
         2) the hold thread spun unpaced: 100% of a core for 10-100x the write
            rate the race needs (slowest measured successful hold: ~27
            writes/step), burning CPU the game needed for rendering - read as
            camera stutter. Now paced to one write burst per 250us (~66/step,
            still 2x+ the slowest success); when WPM is naturally slow
            (5.7ms/pair) the pacing no-ops.
       * CLICK-TP + WAYPOINTS "don't work" while player tp does - all three
         share WriteRootTo -> HoldPosition(Teleport); the difference was luck:
         1) the 150ms hold died on the FIRST transient cache miss
            (TickTeleportHold ended it on !Local().valid), releasing mid-settle
            so the server never accepted the position - and with flight on,
            flight re-seeded and pinned the REVERTED spot, so every failure
            read as "nothing happened". Now: 400ms window, misses tolerated
            (the armed hold rides them), only >250ms sustained absence gives
            up.
         2) HoldPosition could silently fail to arm (motion block unreadable
            for an instant) while WriteRootTo reported success - a teleport
            that never wrote a byte. HoldPosition returns bool now and
            WriteRootTo reports "Could not arm the teleport write" instead.
       * mover_pace_test 110 -> 124 checks: tail-only hold writes (rotation
         excluded, tail covers 0x24..0x48), pacing arithmetic (66/step, 2x+
         the slowest success, no-ops when WPM is slow), teleport hold
         lifecycle (16ms miss tolerated, 300ms absence ends it, 400ms window,
         arm-contract honesty).
       * win32_stub grew LONGLONG (mover's pacing counter).

HEAD     Flight fixed: it was collapsing on every cache hiccup. Infinite jump's
       debug-page instrumentation removed (feature confirmed working).
       * FLIGHT'S BUG, same family as infinite jump's: the cache publishes
         "no local player" for one 16ms window whenever any of its ~20 chained
         reads fails a pass (every second or so under load), and the old
         lifecycle hit that in TWO places at once - mover::Tick() killed the
         position hold on the miss, and RunFlight() called Stop() on it. Every
         hiccup was: hammer dies, character falls a moment, flight restarts and
         RE-SEEDS its position from wherever the fall had got to. A staircase
         of drop-lurches - "flight stutters / sinks / bugs me into the ground".
       * mover::Tick(): a transient miss no longer counts as a respawn. Only a
         DIFFERENT VALID primitive tears down holds/owners; a miss keeps the
         previous address and everything armed on it. Features bound their own
         "really gone" tolerances (flight 500ms, infjump 200ms) and release
         then, so genuine detaches are still cleaned up.
       * RunFlight(): a miss is tolerated (steering and the armed hold keep
         running on the last known good address - HoldPosition is simply not
         re-armed until the body resolves); >500ms unresolved stops flight; a
         REAL respawn (different valid primitive) re-seeds onto the new body
         instead of teleporting it to the old coordinates or stopping.
       * Steering gate: menu-open -> text capture (g_menu_text_input), same
         policy infinite jump learned - you can fly with the menu up; typing in
         a keybind box or the config name field does not fly you around.
       * FLIGHT + INFINITE JUMP coexistence: while flying, space is flight-up;
         infinite jump kills any live burst when flight starts and swallows the
         key edge (a pinned vy=100 would fight the position hold's zeroed
         velocity).
       * The stale header claim that flight "ANCHORS the root part" is gone -
         it never called Anchor; and the anchored-survival measurement shows
         the flag is not a position-lock anyway. The hold-thread hammer is the
         mechanism, and the comment now says so.
       * Infinite jump debug REMOVED (user: "remove the debug for infinite
         jump since it is fixed"): Snapshot::infjump rows, the FIRE button,
         fire_infjump action, RequestInfJumpFire/InfJumpDebugInfo, and the
         first-burst timeline/sampling machinery. KEPT: the text-input gate
         (the fix), the rate-limited per-burst summary line, hiccup tolerance,
         and the eaten-press log line.
       * mover_pace_test 103 -> 110 checks: flight lifecycle transcribed and
         pinned (five 16ms misses over 2s must not stop it; 800ms gone stops
         it; a respawn adopts the new body; steering gate is text-capture;
         flight owns space and the burst stands down).

HEAD     Infinite jump: "nothing happens when i press space" - the input gate,
       made visible and fixed. The user was right to suspect binding.
       * THE BUG: space was ignored whenever the menu was OPEN. The burst log
         lives on the DEBUG PAGE, behind the menu - so the feature could not be
         fired from the one place its own diagnostics are read, and every press
         there died silently. The gate is now TEXT CAPTURE ONLY
         (g_menu_text_input, ImGui WantTextInput sampled in render_ui): a
         keybind-capture box or the config name field eats space; a merely open
         menu does not. g_menu_open stays for layout/focus decisions.
       * THE DEBUG PAGE NOW SHOWS THE INPUT PATH LIVE (Snapshot::infjump rows):
         feature on/off, space DOWN/up, menu state ("open - space live" vs
         "open - TEXT FIELD eats space"), burst HAMMERING/idle, burst writes.
         "Is it bound?" is a question the page answers, not one we guess at.
       * A FIRE BUTTON on the debug page requests a burst through the SAME path
         the key takes (features::RequestInfJumpFire -> an atomic flag the
         feature loop consumes exactly like a rising edge). Button works but
         space doesn't = input gate; neither works = machinery; both work but
         no climb = the game. The fault space is now three clicks wide.
       * A press that arrives while the local player is unresolved now SAYS SO
         in the log (rate-limited) instead of vanishing - the last silent
         failure mode in the input path.
       * mover_pace_test 100 -> 103 checks: the gate itself is pinned (menu
         open + no text field = space FIRES; text capture = ignored).

HEAD     Infinite jump: ROOT CAUSE of "test works, feature never does" found and
       removed. The burst is now AUTONOMOUS - it cannot be torn down by the
       cache, the mover, or anything but its own clock.
       * THE ROOT CAUSE, and why four different mechanisms all "never worked":
         every version routed its writes through mover ownership, and
         mover::Tick() demolishes every hold and owner the moment one cache
         pass returns without a local primitive. fetch_local() chains ~20
         remote reads (datamodel name -> Players -> local player -> character
         -> children -> part names -> humanoid -> root-part pointer); ANY one
         failing that pass publishes hrp_primitive = 0 for one 16ms window.
         The cache thread does hundreds of reads per pass every 16ms, so under
         load a bad pass happens every second or so - and a 400ms burst that
         needs ~25 consecutive clean passes died within milliseconds, nearly
         every time. Mechanism-independent kill, mechanism-independent symptom.
         The debug test is immune because it reads the primitive ONCE at click
         and then ignores the world - that immunity, not the write pattern,
         was the missing ingredient.
       * mover::VelocityBurst(prim, v, duration_ms) replaces HoldVelocity /
         ReleaseVelocityHold / VelocityHeld. The hold thread hammers linear
         velocity into BOTH motion blocks (rotation+0x30, rotation+stride+0x30
         - the test's exact addresses) in a tight loop until the deadline
         passes, then stops BY ITSELF. Tick()'s respawn guard no longer touches
         it; no owner is claimed; ExtendVelocityBurst restarts the clock for
         the mid-air re-fire; StopVelocityBurst kills it (feature off, or the
         character is really gone).
       * Policy lives in the feature's own tick: kill on a DIFFERENT VALID
         primitive (a real respawn - the hammered address is a dead
         character's), kill after 200ms unresolved (a detach - not a hiccup,
         which lasts 16-32ms), tolerate everything briefer. A press that lands
         inside a hiccup extends the live burst instead of being eaten.
       * EVERY BURST REPORTS ITSELF (this is how the next failure gets named):
         one summary line per press - writes achieved, duration, pinned
         velocity, y start -> end - and the first burst per process logs the
         test's full 50ms-sample timeline. 0 writes = the hold thread starved;
         writes but no climb = the game changed; no burst lines at all = the
         input/toggle path. Rate-limited to one line per 2s so spam-pressing
         cannot flood the console.
       * mover_pace_test 94 -> 100 checks. The new state machine (feature
         policy + autonomous hold-thread burst) is transcribed whole, and the
         headline regression is the exact bug: three 16ms unresolved windows
         spread across a 400ms burst must not kill it, while a different valid
         primitive kills it instantly and >200ms unresolved stops it. Also:
         tap = full window, hold = one burst, stuck key self-extinguishes,
         taps chain bursts, mid-hiccup press extends, per-click climb +30-37
         studs at power 100.
       * win32_stub grew InterlockedIncrement/InterlockedExchange with the real
         signatures (LONG volatile*, returning LONG) - mover's burst write
         counter needs them and the stub should name what MSVC names.

HEAD     Infinite jump: one hammered-velocity BURST per space press (the user's
       final shape). "hammered velocity actually works, it increase ur
       jumppower and you go up and up everytime you click it without having to
       land, it works perfectly."
       * THE CLICK IS THE UNIT. The debug test (RunTest id 10) is clicked once
         per run, and the session log shows every click climbing whether
         grounded or airborne:
           run 1: y   3.10 ->  37.57 (+34.47)  grounded
           run 2: y  61.17 ->  94.22 (+33.05)  AIRBORNE, never landed
           run 3: y 102.30 -> 131.94 (+29.64)  AIRBORNE
           run 4: y 154.83 -> 189.05 (+34.22)  AIRBORNE
         So the feature is now edge-triggered: a rising edge of space opens a
         400ms window (the test's exact loop duration) in which the hold thread
         hammers velocity.y = power into both motion blocks. HOLD-TO-RISE, the
         previous entry below, was still an interpretation - a lift you steer,
         not a jump you click.
       * The window ends on its own clock, NEVER on the key coming up: the test
         runs its full 400ms after one click, so a quick tap delivers the full
         burst. A new press at any time - mid-air, mid-window - restarts the
         clock, which is the mid-air re-fire and the whole point of "infinite":
         clicks chain bursts into a continuous climb without landing.
       * Holding does not re-arm. One click = one burst, exactly as the test's
         button has to be clicked again for another run.
       * THE 5s FAILSAFE IS GONE because the mechanism is now bounded by
         construction: a stuck key produces ONE rising edge and therefore ONE
         400ms burst; more bursts require real presses, each capped at 400ms.
         The guard existed to bound a hold that never ends on its own - a
         window that expires at 400ms does.
       * Humanoid::JumpPower/UseJumpPower are now written ONCE per burst (not
         every tick) so the engine's own grounded jump agrees with the slider;
         the test itself never touches the humanoid.
       * Default infinite_jump_power 50 -> 100: that is the value the debug
         test pins and the one the user watched climb +30-34 studs per click.
         "Almost exactly how the debug test works" includes the number. Slider
         default in the movement page follows; saved configs keep their value.
       * mover_pace_test 93 -> 94 checks, now transcribing the feature's state
         machine: a tap fires a full 400ms burst (never truncated on key-up),
         a held key gets exactly one burst (not a lift), a stuck key
         self-extinguishes at 400ms, three taps chain three bursts, a
         mid-window press restarts the clock, per-click climb at power 100 is
         +30-37 studs, and the post-burst coast (~20.6 studs at power 100)
         belongs to the engine's gravity.

HEAD     Infinite jump redone as HOLD-TO-RISE, copying the debug test exactly.
       * The user compared the feature to RunTest id 10 and said the TEST is how
         infinite jump should behave. So the feature is now a direct copy of
         that test's behaviour rather than an interpretation of it.
       * WHY THE PREVIOUS VERSION FAILED, and it was not the mechanism. I
         hammered for a fixed 90ms sized to "roughly double the free arc". At
         the default power of 50 that is 4.5 studs of pinned climb plus a
         5.1-stud arc = ~9.6 studs, barely above an ordinary Roblox jump (~7).
         It fired correctly every time and was INVISIBLE. The debug test reaches
         35 studs because it hammers a bigger value for 4x longer. I had tuned
         the feature down to nothing while being satisfied with the mechanism.
       * NEW SHAPE: hammer for as long as SPACE IS HELD, the way the test
         hammers for as long as its loop runs. Hold to rise continuously,
         release and the engine integrates gravity from that speed. Height is
         simply rise_speed x hold_time, so infinite_jump_power is now a direct
         rise-speed control instead of a number feeding an arc formula.
       * mover::PulseVelocity(owner,v,ms) -> mover::HoldVelocity(owner,v) +
         ReleaseVelocityHold(owner). No deadline in the mover; the caller owns
         the duration. The hold thread writes linear velocity into both motion
         blocks in a tight loop - the identical two write_raw calls the debug
         test makes.
       * Horizontal velocity is re-read every tick, so a running jump keeps its
         run and steering still works while rising; only y is ours.
       * A 5s failsafe caps a stuck key. It is a guard, not the mechanism - 55x
         longer than the old window.
       * mover_pace_test 91 -> 93 checks, including the ~9.6-stud invisibility
         regression as an explicit guard.

HEAD     Infinite jump hammers velocity (user's call, and the data agrees).
       * kPulseMs 33 -> kHammerMs 90. The pulse was sized conservatively to
         "just survive a step boundary"; hammering longer is what the user
         wanted and what the measurements support.
       * Why hammering is right here: every hammered run climbed cleanly and
         repeatably (+35.09 and +34.39 studs / 375ms, velocity pinned at
         100.00), while lone writes in the same session were a coin flip
         (-0.01 vs +25.24). The humanoid controller damps vertical velocity
         every step; hammering re-asserts it faster than the damper eats it, so
         the jump fires EVERY time - which is the point of infinite jump.
       * Why the window is still bounded: while hammering, velocity is PINNED
         and the climb is LINEAR (measured ~93.6 studs/s at a written 100, so
         hammering tracks the written value within ~7%). Hammer forever and it
         is a lift, not a jump. Stopping hands the arc to the engine's gravity.
       * Why 90ms specifically, at the default power of 50:
             free arc alone      ~5.1 studs   (a weak hop)
             90ms of hammering   ~4.5 studs
             total               ~9.6 studs   (roughly double - a real jump)
         and it spans ~5 physics steps, so the impulse cannot be lost to a step
         boundary the way a single write was. The power setting still controls
         height, and more than proportionally (the arc term is quadratic).
       * mover_pace_test 87 -> 91 checks: the hammer rate, the window in steps,
         the doubling at default power, the lift shape to avoid, and monotonic
         scaling with the power slider.

HEAD     The velocity pulse: a single impulse was a coin flip.
       * The run that "proved" velocity was broken and the run that proved it
         perfect are BOTH in this session's log, same test, same spot, y=100:
             run 1: vel 100 -> 0.30 in 25ms, climbed  -0.01   (eaten)
             run 2: vel 100 -> 95.09,        climbed +25.24   (perfect arc)
         Meanwhile BOTH hammered runs climbed cleanly (+35.09, +34.39).
         So velocity is not unreliable - a LONE WRITE is. The humanoid
         controller damps vertical velocity every step, and a single write
         survives only if it lands just after a step boundary rather than just
         before one. Roughly a coin flip, which is exactly the reported
         "infinite jump isn't working at all" (it fired occasionally).
       * FIX: mover::PulseVelocity(owner, v, ms) re-asserts the impulse from the
         hold thread for a short window. kPulseMs = 33 covers TWO step
         boundaries (16.7ms each) so it cannot fall in a gap, while adding only
         ~3.3 studs of lift against the free arc's ~20.6 - still a jump. A long
         window would pin velocity into a constant-speed lift, which is what the
         hammered runs measured (+35 studs and still climbing linearly).
       * The pulse deliberately outlives mover::Release: it carries its own
         primitive address and deadline, so the jump completes even though the
         feature hands the body back immediately. Cancelled on respawn.
       * Position side confirmed FIXED by this run: 5529-5839 writes/500ms
         (~190 per step) and the position held flat at the target in all four
         continuous-write runs, with clean gravity after release. The dedicated
         hold thread is doing its job.
       * mover_pace_test 80 -> 87 checks, including the measured coin flip and
         the window bounds.

HEAD     WRITE THROUGHPUT IS THE HIDDEN VARIABLE. Why it "works sometimes".
       * A run arrived that CONTRADICTED the previous one on identical code.
         The contradiction is the finding. Four runs of the same test:
             writes/500ms | per 16.7ms step | held?
                  17127   |      572        | perfectly
                   1248   |       41        | yes (2 glitches)
                    810   |       27        | yes
                     88   |      2.9        | NO - fell most of the run
         Same build, same machine, ~200x spread, purely system load.
         WriteProcessMemory cost ~29us per pair in the fast run and ~5.7ms in
         the slow one. Holding a position is a race against the engine's
         per-step restore, so the achieved RATE decides whether a feature
         works - and nothing in the code controlled it.
       * The failing run also explains the "velocity is broken" reading in the
         same log: vel.y 100 -> 0.33 in 25ms with no climb is not a broken
         field, it is the humanoid damper winning because our single write got
         no follow-up. The earlier run measured a perfect arc from one write.
       * FIX: a DEDICATED HOLD THREAD (mover::HoldPosition/ReleaseHold), above
         normal priority, doing nothing but re-asserting the target position.
         The feature loop is one Sleep(1) thread shared with ESP, aimbot and
         network polling, so a slow pass anywhere steals exactly the writes
         movement needs. Flight and the teleport/waypoint hold now use it.
       * Instrumented: mover tracks writes/ms, warns once in the log when it
         falls under 8 per physics step, and the debug page shows "pos writes
         N/ms (M per step)" in green/amber. The variable is no longer hidden.
       * mover_pace_test 72 -> 80 checks, encoding the four measured runs and
         the threshold between them.
       * Also: this place's gravity is 243, not 196.2 (World::Gravity is
         per-place) - recorded in the previous entry, still true.

HEAD     Infinite jump: one write. Velocity was never broken - I was.
       * Measured at last (RunTest 9), a SINGLE velocity write of y=100:
           t+  0ms vel 100.00 y  3.10      t+200ms vel 50.13 y 22.06
           t+100ms vel  74.66 y 14.31      t+400ms vel  2.72 y 28.35
         A textbook self-decaying jump arc, +25.25 studs, from one write. The
         engine integrates velocity and applies gravity itself. Velocity was
         never the problem; every "fix" I made to this feature was solving a
         problem that did not exist.
       * REMOVED all arc shaping: the decaying re-assertion, kLaunchMs, the
         yield condition, s_launch_at/s_launch_v. Our recomputed arc and the
         engine's integration diverge from the first millisecond (we recompute
         from a start time, it integrates from the current value), so we were
         fighting our own impulse. The feature is now: read velocity, set y to
         the power, write once, release.
       * SECOND, INDEPENDENT BUG - "jumppower doesn't work either". The grounded
         path set Humanoid::Jump and let the engine jump us, but the engine
         jumps at ITS power, so the slider did nothing for the first jump of
         every sequence. Both paths now use the velocity impulse: one mechanism,
         one power, identical grounded or airborne.
       * THIS PLACE'S GRAVITY IS 243, NOT 196.2. The measured decay is
         (100.00-2.72)/0.4 = 243.2 studs/s^2; World::Gravity is per-place. Every
         hardcoded 196.2 in the old arc code was computing the wrong curve. Do
         not reintroduce arc modelling without reading World::Gravity.
       * And the real arc (+25.25 studs) does not even match the textbook one
         for that gravity (+20.54) - there is a humanoid controller and a damper
         in the loop. Another reason to let the engine own the shape.
       * Hammering velocity was also measured: it PINS vel at 100 and produces a
         constant-speed lift (+30.45 in 375ms, linear). Correct for flight-like
         motion, wrong shape for a jump - so position-hammering stays for flight
         and single-impulse stays for jump.
       * mover_pace_test 67 -> 72 checks, all asserting the measured numbers.

HEAD     POSITION MUST BE HAMMERED. The measurement that explains all four bugs.
       * The continuous-write test settled it: hammering held the character at
         +25 studs for 500ms (1248 writes); a single write to the same address
         reverted within 50ms. The engine restores position from a master copy
         every step, so holding a position is a contest you must keep winning.
       * PACING POSITION BEHIND PhysicsStep() WAS THE ROOT CAUSE of flight
         sinking into the ground and of waypoints needing to be spammed. We
         wrote once per 16ms; the engine restored us the other 15. Removed from
         flight, freecam and the teleport hold. Documented in mover.h so it is
         not reintroduced.
       * THE ANCHOR DOES NOTHING - removed. The anchored survival test held the
         bit set (flags 0x3A) for 500ms and position still reverted to the exact
         start value at vel 0.00, an identical timeline to unanchored. My
         previous commit's central claim was wrong; a016e68 was right to remove
         it, for the wrong reason. Noclip keeps masking the bit defensively.
       * INFINITE JUMP WAS CANCELLING ITSELF. ApplyLaunch yielded when
         `v <= engine_vel_y`, but that velocity is read back from OUR OWN write
         microseconds earlier, so it still held the launch speed while v had
         decayed by a few ms of gravity. The condition fired on iteration two
         and the jump lasted ~2ms - invisible, which is exactly "not working at
         all". Now it yields only when the engine raises velocity ABOVE our last
         write (something genuinely lifting us), or at apex.
       * FREECAM: Scriptable was never the problem - it sticks for 500ms across
         5 runs and hammering type+CFrame moved the camera exactly +30 studs.
         The pacing was the problem, same as flight.
       * mover_pace_test 61 -> 67 checks: the self-cancelling yield condition as
         an explicit regression, and the hammer-vs-pace write-rate contest.
       * Four wrong diagnoses in a row preceded this one, every one of them
         reasoned from code rather than measured. The tell was in the very first
         survival log: reverting to the EXACT prior value at zero velocity was
         never consistent with physics.

HEAD     THE ROOT CAUSE, finally measured: the root part must be ANCHORED.
       * The survival test gave a definitive, repeatable answer (5 identical
         runs) that overturned my own first commit. An unanchored position
         write is fully reverted within 50ms at v=0.07 - recomputed by the
         solver, not fallen. See section 4.0a for the full trace and the
         revert-timing decision table.
       * a016e68 REMOVED the Anchored flag from the teleport path, blaming it
         for rubber-banding. That was reasoning, not measurement, and it was
         backwards: the anchor was the only reason teleport worked. Its
         changelog entry below is wrong on that point.
       * mover::Anchor(owner, bool) / mover::IsAnchored() - saves the original
         Flags byte once, ORs PrimitiveFlags::Anchored, and restores verbatim.
         Restore runs LAST and UNCONDITIONALLY on release: a character left
         anchored cannot move at all, which is worse than any feature failing.
       * Teleport: anchor before the first PushRoot and re-assert each tick of
         the hold; EndHold releases it first. kHoldMs 100 -> 150 - anchored,
         the hold only has to outlast replication, not win a race.
       * FLIGHT REWRITTEN on the mechanism the test actually validated. It no
         longer injects velocity: standing still, velocity damps to zero within
         ~100ms (0.07 -> 0.01 -> 0.00), so the old version was pushing against
         a damper every step - that is the sink-and-snap. Flight now anchors,
         seeds from the current position ONCE (re-reading each frame folds the
         engine's corrections back in), and integrates position itself with the
         real elapsed clock. Writes stay paced at 60Hz; velocity is zeroed with
         the position so nothing is stored up to fling the body on landing.
       * Infinite jump keeps using velocity, and that is deliberate: a jump is
         one impulse the solver integrates, which is exactly what velocity is
         for. Re-asserting the decaying arc each iteration outpaces the damper,
         and it still yields the moment the engine's own velocity catches up.
       * Noclip and the anchor share the Flags byte at 0x1b6. Noclip now masks
         the anchor bit out of its saved value (or it would remember
         "anchored" as a part's normal state if it first saved during a
         teleport) and preserves the live bit on restore (or it would drop a
         flying character out of the sky). Full interleave covered by tests.
       * mover_pace_test 53 -> 61 checks, covering anchor save/restore,
         repeated sets, double release, and the noclip interleave.
       * METHODOLOGICAL LESSON, now proven three times: an immediate read-back
         only proves WriteProcessMemory returned true. All three wrong
         diagnoses rested on that blind spot. Run the survival test FIRST.

HEAD     Two self-inflicted bugs from the previous two attempts, and the test that
       should have existed from the start.
       * I WAS WRONG TWICE, in opposite directions. Recording both so the next
         person does not re-derive them:
           1. WriteVelocity read the WHOLE 0x48 block, swapped the velocity, and
              wrote the whole block back - carrying a position read up to 16ms
              earlier. Flight writes velocity every step, so every step it
              re-asserted a stale position over the one the engine had just
              integrated. A 60Hz position snap-back, in the function whose own
              comment claimed to avoid exactly that. Velocity writes now touch
              ONLY the velocity bytes (two 12/24-byte writes, no position echoed).
           2. I paced velocity behind PhysicsStep(). That was an over-correction.
              POSITION is an engine OUTPUT - writing it is a fight, and that one
              must stay paced. VELOCITY is an engine INPUT: the solver reads it,
              integrates it, applies gravity to it. Correcting it only every 16ms
              let gravity accumulate between writes (a sawtooth sink) and delayed
              the jump impulse by up to 16ms, sometimes dropping it. Flight and
              the jump arc are now unpaced; the teleport hold stays paced.
       * INFINITE JUMP: one write is a twitch (the solver eats it within a step);
         a constant held velocity is a lift, not a jump, and the server rejects
         the path - that was the original 180ms bug. A jump is one impulse that
         DECAYS under gravity, so the launch speed is now re-asserted with gravity
         subtracted ourselves and STOPS as soon as the engine's own velocity has
         caught up. Horizontal is preserved, so a running jump carries.
       * The 120ms window I first gave that arc was wrong and the new test caught
         it before it shipped: a launch at power P takes P/g seconds just to reach
         apex (50 studs/s -> ~255ms), so 120ms truncated every jump mid-flight.
         The cap is 600ms and is the pathological-case bound, not the normal exit.
       * NEW DEBUG BUTTON - "survival: does a write stick?". Every previous test
         read back IMMEDIATELY, which only proves WriteProcessMemory returned
         true; it cannot tell "the engine accepted this" from "the engine
         overwrote it 3ms later", and BOTH of my wrong diagnoses were built on
         that blind spot. This writes once, samples for 500ms, and prints the
         timeline. WHEN it reverts is the diagnosis: instantly = the solver owns
         that field; ~100-300ms = the SERVER corrected us (replication, which no
         local write rate can beat); never = the write path is fine and the bug is
         in feature logic. Run this first from now on.
       * mover_pace_test is 53 checks (was 45): the jump arc shape, that the
         window outlasts the arc, and regression guards for both the constant-hold
         and the fixed-window mistakes.

HEAD     The primitive has TWO motion blocks, and block B's VELOCITY is live.
       * The pacing fix was necessary but not sufficient: movement was still
         broken, and the user's probe said why. The primitive holds two identical
         0x48 blocks - rotation, position, linear velocity, angular velocity - at
         0xC8 and 0x110, stride exactly 0x48 = 36+12+12+12. They are not "two
         CFrames"; the second carries velocity too.
       * THE EVIDENCE: block B's linear velocity at 0x140 held (1.42, 0, -15.94)
         at the instant of the probe. Magnitude 16.00 - the character's exact
         walkspeed. Live, not padding. The code wrote both POSITIONS but only
         block A's VELOCITY, so the engine re-synced from B's stale copy and
         integrated our write away every step.
       * The write test was reporting this the whole time and it was misread:
         "dY 10.00 / dY 0.00" alternating is a RACE, not a bad offset. A bad
         offset fails every sample; this one lost about half - which is what a
         60Hz engine sync racing an unpaced writer looks like.
       * A motion write is now ONE 0x48 buffer with all four quantities, written
         to BOTH blocks, B last (it is the copy the engine re-syncs from).
         WriteCFrame zeroes both velocities with the position - a teleport has no
         motion through the space it skipped - while WriteVelocity leaves position
         exactly as read, so it does not fight the engine's own integration.
         grep confirms zero raw motion-field writes remain outside mover.cpp.
       * The geometry is DERIVED from Primitive::Rotation and CFrame2 (the two the
         live offset service refreshes) instead of standalone constants, which
         would silently desync on a client update. LayoutSane() checks the whole
         shape once and DISABLES movement with an explanatory log line if it does
         not hold: writing 0x48 bytes at a wrong stride corrupts neighbouring
         fields rather than just failing, so refusing is the right answer.
         AssemblyLinear/AngularVelocity2 are registered with the offset service.
       * The debug write tests were themselves single-block, so they could not
         have caught this - they now write both blocks whole and report each one
         separately ("A reads .., B reads .."). Added a CAMERA probe button
         (0xC0..0x160) that also states whether Rotation+36 == Position still
         holds, since freecam's single 48-byte camera write depends on that
         adjacency and the client version changed under us.
       * mover_pace_test keeps the raw probe dump VERBATIM and re-derives the
         layout from it - block shape, stride, both blocks agreeing, the
         walkspeed-magnitude check, and that the rotation rows are unit length.
         45 checks. The layout is evidence in the repo, not folklore in a comment.

HEAD     The rubber-band was one bug in four features: the write RATE.
       * The report named it: "tp to player works fine" but waypoints do not, and
         both call WriteRootTo. Same destination code, different distance - so the
         destination was never wrong. FeatureLoop is Sleep(1) (~600 iterations/sec)
         and Roblox steps at 60. Flight, the teleport hold, infinite jump and noclip
         each wrote the character on EVERY iteration; the engine consumes one update
         per step and discards the rest, most of them landing mid-solve, and the
         replicator ends up with a position history the server corrects. A single
         write sails through - which is exactly why tp-to-player felt fine.
       * NEW features/movement/mover.{h,cpp}: one owner for the local character.
         Real QPC dt (clamped to 100ms so a stall is not one giant step), a 60Hz
         PhysicsStep() gate every physics write goes behind, a priority
         (Freecam<Jump<Flight<Teleport) so two features cannot drive the body at
         once, and save/restore keyed to the address it was saved from - so a
         respawn drops it instead of writing PlatformStand=false into a dead
         Humanoid. WriteCFrame is ONE 48-byte write per CFrame copy, never a read
         between the two, so the solver cannot see a torn frame.
       * FLIGHT drives AssemblyLinearVelocity instead of position (an input to the
         solver, not an output it overwrites, and the thing the replicator actually
         forwards). It no longer touches World::Gravity or collision flags. No dt in
         the feature at all now - a velocity is already studs/second, which is what
         made the speed slider meaningless before.
       * INFINITE JUMP is one impulse per press again. Grounded it sets
         Humanoid::Jump and lets the engine run the real jump; airborne it is a
         single velocity write preserving horizontal motion. The 180ms/~150-write
         re-assert and the EvaluateStateMachine freeze are gone (the freeze stopped
         the humanoid controller and stayed on if the restore path was missed).
         Humanoid::FloorMaterial reads Enum.Material.Air = 1792 when airborne, NOT
         0 - an enum property cannot hold an empty value - so the grounded test is
         against Offsets::MaterialEnum::Air. Testing != 0 calls every real floor
         airborne.
       * TELEPORT / WAYPOINTS stop anchoring the root part (anchoring is what stops
         the client replicating a state the server can follow) and hold for 100ms of
         paced steps instead of 280ms of flooding. GotoWaypoint calls WriteRootTo,
         not TeleportToWorld: a waypoint already IS a root position, so the
         tp_land_height lift was adding 2 studs of drift per round trip.
       * WAYPOINT MARKERS use features::ProjectToOverlay (esp.h), the app's one
         projection. The private copy here skipped the client->desktop transform, so
         markers were off by the Roblox window origin and the DPI scale unless the
         game was borderless at desktop resolution.
       * FREECAM had four bugs, none of them the CFrame write. (1) Mouselook read
         GetCursorPos deltas, but Roblox locks the cursor to its window centre every
         frame - so it measured the game's re-centring, not the hand: frozen, then
         flinging. Now WM_INPUT raw input (new input_mouse.h), RIDEV_INPUTSINK so it
         arrives unfocused, NOLEGACY deliberately off so ImGui keeps its mouse
         messages, absolute-mode devices differenced, and no cursor-delta fallback -
         if registration fails it says so and does not turn. (2) ~600 writes/sec with
         the fake dt. (3) LookAtMatrix builds right = worldUp x fwd, which is
         zero-length looking straight up/down - a degenerate basis is a garbage view;
         the basis now comes from yaw/pitch directly. (4) It left the body standing
         in the world taking WASD, so you flew the camera off and your character
         walked away; it claims the body through mover now.
       * NEW glass-obsidian/tools/mover_pace_test.cpp - 20 checks, no Windows, no
         Roblox: step rate 62+/-2 per second at loop rates 100Hz..2000Hz, at most one
         step per millisecond, a 500ms stall resyncs to ONE step (not a burst), the
         dt clamp, real-dt integration travelling the requested studs/second at any
         loop rate, and the freecam basis orthonormal + right-handed over 289
         yaw/pitch combinations. Three checks are regression guards asserting the OLD
         behaviour was wrong (fixed-dt overshoots ~10x; worldUp x fwd collapses at
         the pole) so it cannot be quietly reintroduced.
       * check_win_tu.sh now parses all 14 host TUs (the seven movement/camera ones
         were never checked before); the stub grew QueryPerformanceCounter/Frequency
         and the VK_SPACE/VK_LSHIFT/VK_LCONTROL codes. 14/14 parse, tp_math 16/16,
         servers_json 33/33, mover_pace 20/20. No UI file changed, so the menu shots
         are the same pixels.

HEAD     std::thread is unusable in this project, and the reason is not in the error:
       * The build after the WinHTTP fixes still failed with C2039/C3861
         `_beginthreadex` at include/thread:79. Not a lean-and-mean problem (that guess
         is retracted): the project's own `process.h` is on the include path via
         $(ProjectDir) and SHADOWS the CRT's `<process.h>`, which is where MSVC's
         `<thread>` gets that declaration. Nothing in the error text can point at it,
         and g++ off-Windows has a real <thread> and no shadowing, so no local check
         could see it either.
       * `features/servers` now starts its two jobs with `CreateThread` - what
         `main.cpp` and `cache.cpp` already do - with a one-job-in-flight static
         context instead of thread arguments, and it says why at the top of the block.
         `std::mutex`/`std::atomic`/`std::chrono` stay: only <thread> reaches for the
         shadowed header.
       * The check now greps for `#include <thread>`, `std::thread` and `std::async` in
         the app and fails with the explanation, so the next person gets a sentence
         instead of two errors inside a Microsoft header. Verified both ways: clean on
         this tree, fires on a copy with the include put back. §9 has the rename that
         would remove the landmine (6 TUs + vcxproj + filters) rather than dodge it.

HEAD     The five things the first MSVC build of the new TU found, and a stub so they
       come here instead of to a user's machine:
       * `WINHTTP_URL_COMPONENTS` does not exist (it is `URL_COMPONENTSW`), which then
         cascaded into "comp: undeclared identifier" seven times and "WinHttpConnect
         does not take 3 arguments" - all one typo.
       * `WinHttpSetOption` takes `LPVOID`, so the timeout loop's `const DWORD ms` was
         a C2664. Real bug, hidden by a stub that took `void*` loosely; the stub now
         takes LPVOID so it stays hidden by nothing.
       * `SE_ERR_FNMF` is not a ShellExecute code (`SE_ERR_FNF` is). The comment about
         "codes in the same range as the success handles" is right; the name was not.
       * `err = host + " answered "` built a std::string out of a std::wstring -
         C2676. The status message now takes the authority out of the URL we already
         have, in the encoding the message is in.
       * `WinHttpOpenRequest` has no version argument: `(connect, verb, object,
         referer, context, certUsage, securityFlags)`. My call had an extra nullptr,
         which put `WINHTTP_NO_REFERER` in a `DWORD` slot - MSVC would have made that
         another C2664 and it is now fixed, in the app and in the stub's signature.
       * A `#define WIN32_LEAN_AND_MEAN` at the top of the TU was also blamed for the
         C3861/C2039 pair and removed [WRONG: removing it fixed nothing - <thread>
         cannot find _beginthreadex here with or without it, because of the process.h
         shadowing in the entry above; the define stays out only for consistency with
         the other TUs].
       All six host TUs now parse off-Windows through the stubs (`sh
       tools/check_win_tu.sh`), `servers_json_test` is 30/30, `tp_math_test` 16/16,
       the library's invariants pass, and no UI file changed, so the shots are the
       same pixels.

HEAD     Server hop, rejoin, and why the rail said "Ugc":
       * WORLD PAGE, new SERVERS block. `features/servers` reads the place id from
         the datamodel, GETs games.roblox.com's public `servers/Public` and
         `multiget-place-details` on a worker thread (no cookie, no token, no
         account data - both endpoints are the ones the website calls anonymously),
         and lists what it found: `#3  b7c2d9..4e10   3/40`, the row that is yours in
         accent, full ones dimmed. Every row is a button: clicking one opens
         `roblox.com/games/<placeId>?gameInstanceId=<that guid>` in the browser, which
         is what the browser hands the client, so the game decides how you arrive.
         HOP takes the emptiest listed server that is not this one; REJOIN takes your
         own id; `close this client first` (a config entry) posts WM_CLOSE and waits
         for the process to go before opening the link, because Roblox runs one client
         per account - nothing here ever kills the game, and if the client is still up
         after the wait the link opens anyway with that said out loud.
       * WHY THERE IS NO MEMORY WRITE FOR THIS: entering a server is
         `TeleportService`'s job, a Lua call, and this project does not inject. The
         join link is not the lazy version, it is the only version where Roblox's own
         rules apply - which is also why a hop can be reported as "the site said 429"
         instead of pretending to have teleported you.
       * THE EXPERIENCE NAME IS NOT IN MEMORY (see §4.5): the datamodel instance's
         name is the DataModel kind, which is the "Ugc" that was in the rail. It now
         comes from place-details, asked once per place as soon as the menu sees a
         place id, and the GAME row is simply absent until it answers.
       * `Refresh()`/`Tick()` never block the render thread: one request in flight,
         results published by the feature loop, and every path - parse failure,
         HTTP status, shell refusal, "the id moved" - writes a sentence into the
         card. `servers_json.h` got its own host test (30 checks, §6) because a
         mis-split server entry is exactly the kind of bug that produces a confident
         wrong join link.

0be0d89  The proof shots now have a home. `tools/encode_shots.py` replaces a script
       that lived in /tmp: the sandbox was recreated, /tmp was wiped, and `out/` came
       back holding a stale render (identical to the pre-remake shots, so `git status`
       called it clean). The new script re-packs a raw render losslessly, the 24
       committed shots were regenerated with it and re-compared against the renders
       (`compare -metric RMSE` -> `0 (0)` on every one), and the headless suites still
       pass from a from-scratch build of the current sources: `ALL INVARIANTS PASSED`,
       `menu_shots` all ok, `tp_math_test` 16/16, four app TUs + win32 glue parse.
       No UI or feature code changed here - what you see in `out/` and
       `preview/img/menu/` is what the code draws at 7b40a45, which is what the rail,
       the players page and the bind list were asked to look like.

7b40a45  Spectate the way Roblox does it, a shorter rail, mouse binds, no explainer
       boxes:
       * SPECTATE stopped writing the camera. Parking CameraType to Scriptable and
         writing Camera::Position/Rotation every frame loses to the game's own camera
         script whenever it is still running - it re-derives the frame from
         `Camera::CameraSubject`, so its write landed after ours and the view stayed
         on your character, which is the report. Now the feature does the one thing the
         script cannot ignore: it repoints `Camera::CameraSubject` at their Humanoid
         (found by class name under their character, once per attach/respawn, not per
         frame) and lets the game frame the shot. STOP hands back OUR humanoid,
         re-resolved at that moment - restoring the value saved at attach time would
         hand the camera to a corpse if they died mid-watch. `look from behind` /
         `look from above` are no longer camera writes either: they are the subject's
         `Humanoid::CameraOffset`, which is how a game expects to be nudged, restored
         to zero on the way out, and harmlessly ignorable.
       * THE RAIL LOST ITS HEAD. The in-game "IN GAME / <place name>" block (and with
         it the whole brand slot: the title bar already carries the wordmark and the
         attach chip) is gone, so the nav starts at the top of the tab. The experience
         name moved where it is useful - a fourth row in the pinned session card,
         between PROCESS and SYSTEM - and RailStat fits it to the card, so a 40-char
         title ellipsizes instead of walking over its own label. The "HOME toggles the
         menu" line under the card is gone too, along with the `toggle_hint` field
         that fed it.
       * MOUSE BUTTONS ARE BINDABLE. `PollKey` had `if (vk == VK_LBUTTON || vk ==
         VK_RBUTTON) continue;` - the two buttons a click feature most wants were
         simply excluded from the scan, while only the side buttons were reachable via
         a separate branch. The reason was real (capture begins with a click on the
         card) so the fix is about the hand, not the key: `TickKeybinds` now ignores
         input for six frames after a slot starts capturing, and PollKey scans keys
         and all five buttons after that. Escape is deliberately left out of the
         accepted set - the UI owns it as cancel - and the log says `bound mouse 1`
         rather than the old `bind captured`, so the question is answerable from the
         log tab.
       * The explainer note cards are gone: "how the binds work", "what spectate
         does", "how this works" / "listening", and debug's "not in an experience".
         The two that were worth keeping say themselves now - the keybinds subtitle
         carries "Escape cancels" - and the cards that report live state (the
         renderer's skybox string, the last write test) stay. Pages got shorter, and
         the whole bind list fits the page without scrolling.

df1874f  Bind cards that light, and the wrap bug behind every clipped note:
       * `obsidian::KeyCard` takes a `live` flag and the app passes each bind's own
         feature switch to it, so the card on the keybinds (and movement) page wears
         the accent edge exactly when that bind is armed. A bind list that cannot tell
         you what is running is a list you cross-reference against five other pages.
         `waiting` keeps its own accent-filled keycap, so "capturing" still looks
         different from "on".
       * While checking whether the movement page's note was clipped, the real cause
         turned out to be in the library rather than in the copy:
         `ImGui::PushTextWrapPos` is window-relative (ImGui adds `window->Pos.x -
         Scroll.x` to its argument), and `CardText` was passing `cursor_x + avail` -
         a wrap a whole window-width right of the card, so every wrapped note and hint
         body in the shell rendered as one long line clipped at the card's edge.
         `CardText` now pushes `0.0f` ("this window's content edge") and `NoteCard`
         measures its height at the width that yields, instead of assuming two insets
         on a one-inset card. Notes wrap; nothing runs out of its box.
         (Proof both ways: `tools/menu_shots.cpp` still reports every command's
         clip-intersected extents inside the window rect, and the 03/06 preview shots
         are the before/after.)

2c58cde  Six answers on the new shell, and the reason the last one took a render
       harness to find:
       * CLICK-TO-TELEPORT only ever fired by accident. The feature polled
         VK_LBUTTON while the keybinds page let you bind something else, so a bind
         changed nothing; the click that opened or focused the menu left the latch set,
         so the jump landed wherever the cursor happened to be at the time; and the
         reads it needs ran on every frame the button was held, any one of which could
         fail silently. It now detects the press edge first (with GetAsyncKeyState's
         "pressed since last call" bit as a second path, since a quick mouse click can
         fit between two polls of this loop), reads memory only on that press, and says
         why in the log when it refuses - the symptom was silence and there are ten
         causes of it. The destination is solved in order: a player's box under the
         cursor (from the cached roots, no reads), else the standing plane about 2.6
         studs below the root (the root plane was the real bug - in first person that
         plane is eye level, so the solve collapsed onto the camera), else the range cap,
         which is the honest reading of a click at the sky. Syncing the latch to the key
         while the menu is open, instead of clearing it, also removes the jump that fired
         the instant the menu closed. The geometry moved into tp_math.h and is pinned to
         esp.cpp's projection by tools/tp_math_test.cpp; click_teleport.cpp no longer
         includes overlay.h (it reads a mirrored g_menu_open atomic from globals.h),
         which is what lets check_win_tu.sh parse the feature at all - and it caught a
         stale identifier in this very file before MSVC ever saw it.
       * PLAYERS PAGE (page 5). Everyone in the server except you, straight off the
         ESP snapshot: name, hp, studs, weapon; dead rows dimmed, the one you are
         watching in the accent colour. Click picks, double-click jumps, and GO /
         WATCH / STOP act on the pick. The three numbers that decide whether spectate
         feels good (camera distance, camera height, land-above-by) are live sliders,
         so you can dial them in on a moving target. Spectate parks Roblox's camera
         script (CameraType -> Scriptable), writes the camera onto their root every
         frame, and hands it back on STOP, when they leave the server, or when the
         game closes - the release runs in an ungated pass, so switching the flag off
         mid-watch cannot strand a parked camera. [SUPERSEDED: that park-and-write
         half lost the race with the game's camera script and is replaced in the HEAD
         entry below; the players page, the caching and the release-on-leave stay.] Of the four new settings only the
         three numbers go into the config file: never who you were watching.
       * Rail rows count what they mean. The KEYBINDS row lights when a bound feature
         is armed, PLAYERS when spectate is running, and every hint says what it
         counts ("5 on", "6 here", "3 bound") instead of three rows of identical
         numbers meaning three different things.
       * TRANSPARENCY fades surfaces, never letters: Palette::text_stays_opaque plus
         TextAlphaScale(), so a 90% see-through menu is still readable over a bright
         skybox. preview/img/menu/00-faded.png is that proof, and the shooter asserts
         the two alphas rather than trusting the eye.
       * Out of game the rail is the attach chip alone - no wordmark, no eyebrow,
         nothing to fill in while there is nothing to say - and in game the footer
         card reports the OS from the registry (ProductName plus build, because
         ProductName still says "Windows 10" on 11, so the build is what decides).
       * THE HALO WAS IMGUI'S OWN. The menu painted no bloom and no shadow, and a
         grey ring stayed: style.WindowShadowSize defaults to 100px with
         ImGuiCol_WindowShadow at 8% grey / 35% alpha, so the context draws a stock
         window shadow around every window. ApplyTheme zeroes it. Finding that meant
         making the shooter's bleed check stop lying about it - it now compares each
         command's bounds INTERSECTED WITH ITS CLIP against the window rect, with a
         2px margin, so an intentionally clipped glow cannot fail the run and a real
         escape cannot hide in a generous margin. The check also gained two shots no
         page loop reaches: 00-detached (not attached: rail, empty list, no OS) and
         the faded one above.
       * Three layout bugs fell out of that check, all in the library, all the same
         mistake of trusting a number instead of measuring: RailStat right-aligned
         its value with no idea how wide its own key was (the OS string walked over
         "SYSTEM"), PageTitle let a long subtitle run under the status chip, and
         NoteCard measured its body at cell_w*2, which is only the card's width in a
         two-column grid - in a one-column grid the note's last lines fell off the
         bottom of its own card. All three now fit the string to the space that is
         actually left and ellipsize with "...".
       * A fourth, bigger one came out of looking at why the note still would not
         wrap: `ImGui::PushTextWrapPos` is NOT an absolute screen x - ImGui adds
         `window->Pos.x - Scroll.x` to it - so axiom's CardText had been pushing
         `cursor_x + avail`, landing the wrap a full window-width to the right of the
         card. Every wrapped hint and note body in the shell was therefore one long
         line clipped at the card's edge, which is exactly what "the text is cut off
         mid-word" looked like. CardText now pushes 0.0f (ImGui's own "use this
         window's content right edge"), and NoteCard measures its height at the width
         that produces instead of guessing at two insets on a one-inset card.

496b23f  Fixed the C2664 the VS build hit (snprintf writing into Snapshot's
       `const char*` skybox_note: the field is a char[256] buffer now, like every
       other string the app formats per frame), and added a pre-flight so the next
       one is not found by Visual Studio: glass-obsidian/tools/check_win_tu.sh
       `-fsyntax-only`s menu.cpp (and memory/game/cache/config) against a stub
       <Windows.h> in tools/win32_stub with the project's real include dirs, so
       shared-struct/format mistakes in the Win32 half surface on any machine.
       Library invariants 13/13, menu shooter 8/8, win32 glue parses clean.
582ad41  Menu remade in the "Axiom" visual language (top-level tab strip replaced):
       a 220px nav rail (wordmark + place name, icon rows with a live dot and an
       "N on" count, accent selection bar, pinned session card + toggle hint) beside
       a scrolling page of setting *cards* in a 2-column grid, on a near-black
       Axiom palette (PaletteAxiom). The window paints nothing outside its own
       rect (no outer bloom, no drop shadow) - see the follow-up note below.
       Every setting is one card (toggle / slider / combo / keybind /
       colour / button / stat / note / scroll), so a control that appears when its
       feature is on cannot make the layout ragged. All 8 pages and every existing
       setting kept; nothing re-aliased.
       New library layer: glass-obsidian include/obsidian/axiom.h + src/axiom.cpp
       (cards are child windows: exact cell size, own clip, hover/live bloom on the
       container list). WindowConfig gained title_accent (the header wordmark's
       second half) and subtitle_chip; a glow/bloom triple was added here and has
       since been removed again (see below). Palette gained
       PaletteAxiom() + ApplyAccent()/AutoContrast, and "Axiom" as a named
       palette. 18 new stroke icons (globe, folder, terminal, keyboard, ...).
       menu.cpp is now the Win32 half only (KeyName, GetAsyncKeyState capture,
       snapshot reads, config file IO, the debug write tests); the whole menu lives
       in roblox_external/ui/menu_ui.{h,cpp}, which reads the settings globals
       directly and takes the platform calls as std::function callbacks. That split
       is what makes the menu renderable off-Windows:
       glass-obsidian/tools/menu_shots.cpp drives the SAME menu_ui.cpp against stub
       callbacks and rasterises all 8 pages to PNG (plus an invariant that no card
       surface may be painted outside its own clip, which caught two real bugs:
       card backgrounds drawn in window-local space, i.e. invisible, and the rail
       footer clipping its session card). All 13 library invariants still pass.
(wip)  Tab bar fixed: tabs could not be switched by clicking. TabBarImpl
       sampled IsItemActive() AFTER ImGui::Button(), but ButtonBehavior()
       calls ClearActiveID() while processing the release - so the
       act+IsMouseReleased test was never true. Now uses Button's own return
       value (verified headlessly: x-sweep across the tab strip selects every
       tab correctly). Red highlight that wiggled while dragging the menu
       was ImGui nav-highlight (themed red) - keyboard/gamepad nav is now
       left OFF (the theme default); text inputs are click-to-focus anyway.
       Username added next to the pid: LocalPlayerData carries the account
       name now (title-bar subtitle, footer, debug page).
(wip)  GUI swapped to Glass Obsidian (../glass-obsidian): the hand-rolled ui::
       toolkit, custom title bar, tab pill and window-sizing preamble are gone.
       menu.cpp now drives one obsidian::ObsidianWindow (collapse-safe resize,
       animated collapse, its own drag/resize) + the obsidian widget set
       (Toggle/Slider/Combo/TextInput/TabBarIcons/KeyValue/panels). The ui page
       still controls transparency / rounded corners / accent / rainbow - they
       are folded into a live palette every frame - plus new accent presets.
       Attach status moved to the title-bar subtitle; footer kept. Verified
       headlessly: all 8 pages render + collapse/expand double-click through
       real ImGui frames via the library's software rasteriser, zero asserts.
       skybox names/count became inline header variables (const at namespace
       scope has internal linkage - menu.cpp referencing them would not link).
b8d2577  Two fixes after the first screenshot pass, both of them about not looking
       half-finished:
       * NO OUTER HALO. The window used to breathe an accent bloom (2 AddShadowRect
         passes) and sit in a SoftShadow. Both are stacks of axis-aligned blur
         quads, so at every rounded corner a squarer edge poked past the rounding -
         the "shadow around the UI" in the feedback. DrawChrome now paints inside the
         rect only (body gradient, top sheen, inner edge glow, accent rule, 1px
         edge), WindowConfig lost glow/glow_pulse/glow_tint, and draw_shadow defaults
         false (kept as a knob for desktop tools). The theme page's "window bloom"
         slider and the ui_glow global went with it; cards keep their hover/live
         accent, which was never the complaint.
       * TYPOGRAPHY. Fully lowercase labels read as sloppy. Micro-caps (card titles,
         group rules, page titles, nav, chips, keycaps) already shout inside the
         widgets, so DrawChip/ChipWidth do too and no caller has to remember. The
         remaining authored text was swept: buttons say SAVE / LOAD / DELETE / RENAME
         / OPEN / REFRESH / RUN / RESET / CLEAR / COPY, the hero card's feature name is
         tracked caps (S(14.5) at S(1.30) tracking, mixed 0.22 toward the accent when
         on), combo values are Title Case ("Mouse position", "Upper torso",
         "Full box"), the header wordmark is ROBLOX + EXTERNAL, and CardKeyValue
         shouts its key so the debug ledgers match the rail's. Prose (note bodies,
         hints, page subtitles) is deliberately sentence case with real acronyms -
         FOV, ESP, PID, Escape, Roblox - because a paragraph in caps is unreadable.
         Anything the app snprintfs into a Snapshot buffer follows the same rule.
       Verified the same way as the remake: 13/13 headless invariants, the 8 pages
       rendered by tools/menu_shots.cpp into glass-obsidian/preview/img/menu/, and
       tools/check_win_tu.sh parsing the Win32 half. Accent swatch captions now render
       (the preset card was 10px too short and clipped them) and sit in micro-caps like
       every other label; obs::SliderCard/ToggleCard/ButtonCard/... gained a trailing
       `span` so the theme page's lone corner-radius and reset cards fill their rows
       instead of leaving a hole where the bloom card was.
(wip)  UI restyled to the reference's exact glass recipe (Rose): animated
       accent backdrop + translucent Header.Fade->Surface.Fade gradient + quiet
       outline/shadow, flat Knob-colour controls (no glow/spark/catch-light);
       tabs stay at the TOP. Decoded the float probe: Position/Rotation/Size
       confirmed, 2nd CFrame at 0x110/0x134; flight + click teleport now write
       both positions. Second probe re-confirmed the layout; velocity write
       confirmed working; infinite jump rebuilt as edge-triggered velocity
       impulses (per-tap, mid-air chainable) instead of hold-to-ascend.
(wip)  Keybinds now optional: leave a bind as 'none' to keep the feature
       always-on (aimbot/noclip/walkspeed/flight/inventory checker);
       click teleport fires on left-click (no keybind, ignored while menu
       open); removed the inaccurate FPS counter (overlay chip + footer);
       aimbot FOV circle fixed to the screen centre instead of the mouse.
(wip)  Infinite jump impulse re-asserted ~18ms/tap so it can't be raced;
       flight rewritten as PlatformStand + velocity (position writes dropped);
       aimbot humanizer option (reaction delay + eased/jittery aim, strength
       slider); ESP projection rebuilt from the camera instead of ViewMatrix;
       skybox now verifies its face writes by reading them back.
(wip)  Fixed humanizer build error (helpers now forward-declared); collapse
       button actually shrinks the window to the title bar and restores it;
       team check exposed on the aimbot page (shared global, already filtered
       in cache); wall check (line-of-sight vs other players) added to aimbot
       (no lock through a player) and ESP (hide occluded players), persisted.
(wip)  UI restyled to a modern web-app / "CSS-style" dark theme: neutral
       slate surfaces + 1px borders + soft shadows + your red accent, solid
       accent segmented tabs, uppercase section labels, accent page-title bar.
       Kept ImGui (no browser/CSS engine): user asked about CSS, chose to
       restyle ImGui instead. All pages/features/log copy+clear unchanged.
87837e4 Liquid-glass UI remake + robust ESP box + offset probe
9c56489 Fix rainbow accent not resetting; red accent restored; compact + glassier
       UI; canonical ESP body-part sizes; velocity-driven flight + engine-driven
       infinite jump; skybox periodic reapply; delete uncompiled ImGui extras
5897afd Glassmorphic UI overhaul: frosted panels, soft shadows, indigo accent,
       new title bar + status chip + footer; rename namespace -> overlay; fold
       overlay.h; regenerate .vcxproj.filters; fix KeyName dangling pointer
5897afd Remove Clipper2 vendor tree (C++/C#/Delphi/DLL) + dead DrawMergedPoly,
       dead esp.cpp helpers and unused globals; prune junk offsets
79a5dba Remove orphaned mesh cham backend and all references
c24c828 Revert mesh backend enablement (headers incompatible), drop /GS- sdl conflict
2a24d66 Enable mesh cham implementations, menu opens on start, accent on separators, part-size guard
a4fd54c Zero-copy cache snapshots, shrink EspEntity, full release optimizations, fix write test target
ecafe69 Even nav grid, transparency across all backgrounds, add position write test
a008b4b Fix FOV radians bug, rewrite flight + infinite jump, live accent, uniform UI metrics
bf15a63 Condense tabs, UI config page + FOV changer, taskbar window, HRP via humanoid, skybox invalidation
0d07619 PHETAMINE-style UI, tray icon + in-app exit, toggle flight, impulse infinite jump
544490a Add infinite jump + inventory checker, remove korblox, click teleport client coords
6ae48aa Position-based flight, click teleport 3rd person, modern GUI, remove 3d esp preview, consolidate keybinds
2d985a2 Remove console for in-gui log, keybinds tab, optimize memory reads
b22c49a Fix ESP box rotation accuracy, improve flight, add click teleport
8906f70 Fix Instance name reads (NameContainer), auto-reconnect, click-through outside menu
a5c179a Restore focus to Roblox when menu closes, flag not-in-game state
dfff981 Remove game-specific features, glassy red theme, debug tab
a18a7f1 Replace Discord overlay hook with standalone transparent overlay window
bf8e898 Change menu toggle key from INSERT to HOME
015cc98 Update offsets to client version-f5a60436d48947d3
```

---

