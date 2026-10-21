# NEXT — handoff state (after PR #6, payloads 013–016)

Read this first. The operator's priorities, verbatim: "actually getting the
overlay keys to work (f8 does nothing), and for the feature overlay to
actually pop up, be adjustable and able to move around, and actually adjust
features."

## 1. F8 does nothing over the game — root cause found in code (audit of ti-17)

The menu has NEVER appeared over Roblox. It was only ever seen in the
WINDOWED SMOKE TEST (a standalone run where the menu starts open — see
overlay_linux.cpp:82 `menu_open = true` and :555
`if (s_locked_to_sober) g_state.menu_open = false;`). Over the game the menu
starts CLOSED, and the ONLY entry point is the HOME/F8 root grab, because the
input shape is empty while the menu is closed (clicks pass through to the
game). The grab is the whole game.

The grab is broken and its failure is INVISIBLE (platform_linux.cpp:639-664,
HotkeyThread):

- `XSetErrorHandler(IgnoreXError)` is installed, then `XGrabKey(...)` for
  HOME and F8 with AnyModifier. If ANY other client already holds either key
  (a WM shortcut, another tool), the server answers BadAccess — which we
  swallow. XGrabKey errors are asynchronous; nothing verifies the grabs took.
- `g_hotkeys` is set true merely because the THREAD WAS CREATED
  (StartToggleHotkeys, platform_linux.cpp:843-854). The status line then
  prints `hotkeys=on` — a lying indicator. Receipts showed `hotkeys=on` while
  F8 was already dead.
- The smoke test could never catch this: the menu starts open there, so the
  grab is never needed.

Everything PAST the grab is wired and live: the toggle flag is consumed even
while the window is unmapped (idle path does SDL_PumpEvents + 50ms,
overlay_linux.cpp:677-681), menu_open drives draw_anything, unhide, input
shape, and render_ui(). Fix the grab (or route around it) and the menu should
appear.

### Fix plan (next payload)

1. VERIFY the grabs: install a recording error handler around the two
   XGrabKey calls + XSync; on error log `hotkeys=GRAB FAILED (key <k>)`
   loudly and set the status field to the truth (`hotkeys=verified` only
   after a clean sync). Keep a fallback key list (F8, HOME, INSERT, F9?) and
   grab the first that verifies.
2. SECOND entry point that needs no grab at all — so the menu is reachable
   even when grabs fail: a `SIGUSR1` handler in the overlay process
   (`pkill -USR1 -f 'daemon-0'` or a bootstrapper button "Open menu") that
   sets the same s_toggle_requested flag. Also makes the menu testable in one
   command from a terminal.
3. Menu UX per operator: pops up over the game, DRAGGABLE (ImGui title-bar
   drag works once the input shape covers the menu rects — it does,
   s_menu_rects), RESIZABLE (ensure the ImGui resize grip is inside the
   reported input rects), and its feature toggles must actually flip the
   features (verify the Linux menu's toggle -> globals wiring; every feature
   is additionally gated on attach + LayoutSane, so test after offsets load).
4. Then re-test: game up, daemon loaded, F8 (or SIGUSR1) -> menu over the
   game, drag it, resize it, flip a feature, flip it back.

## 2. The Sober crash — status: UNKNOWN, both theories dead, fresh log unread

- Daemon-side writes: ELIMINATED (audit: all toggles default false, mover
  writes gated by s_body.valid && LayoutSane, offsets scanner-validated only,
  port_watcher/sober_attach read-only).
- Fleasion: ELIMINATED by operator test — "it crashes independent of
  fleasion."
- The one log ever read (rblxlog.txt, ti-16 session) showed a HEALTHY client
  (139fps average, Vulkan 1.4) ending in a CLEAN Android teardown
  (nativeAppBridgeV2LeaveGame -> destroyLuaApp -> setStage: None) 16.9s after
  join — an external close request, no RBXCRASH. That log may have been from
  a session where fleasion did the closing; the post-fleasion-off crashes
  have NOT been logged yet.
- FIRST ACTION next chat: get the fresh log. `sober-doctor` (payload 016+)
  now enumerates the logs dirs and reads the NEWEST file whatever it is
  called — the ti-16 miss was a hardcoded `latest.log` while the operator's
  install names it differently in the same flatpak dir
  (~/.var/app/org.vinegarhq.Sober/data/sober/appData/logs/). Ask the operator
  to run `sober-doctor` right after the next crash and paste the output.
- Also still open: what the crash dialog says (operator screenshot showed a
  small white dialog; text never captured) and whether the crash timing is
  fixed (~17s), instant, or tied to menu/F8 interaction.
- gpu_memMB=64 in telemetry = known Sober VRAM-reporting clamp
  (vinegarhq/sober #2208, #2165), considered a red herring at 139fps — keep
  in mind only if the fresh log shows OOM.

## 3. Operator constraints (standing)

- No guesswork: claims about crash causes need code audit + sources; the
  operator explicitly rejected anti-cheat theories (Sober = Android client,
  no Hyperion) and rejected band-aid device-specific patches ("youre
  patching for what is assumed to be every linux device out there, with my
  machine just being the live testbench").
- Void Linux; payload flow = mirror paste fetch rows, one command per line,
  no curl, sha256sum gate; every screenshot must be examined (Pillow
  structural analysis is the accepted no-OCR fallback).
- Test flow used by operator: browser-launched Roblox game + daemon via
  ulauncher (daemon-boot, then Load).

## 4. Where things are

- Branch arena/2c231ec3-daemon, PR #6 (payloads 013-016) merged.
- Mirror doctrine: live URL only from current state.json or head-commit
  status; packer = tools/pack_linux_payload.sh.
- Sober log dir on operator box (flatpak):
  ~/.var/app/org.vinegarhq.Sober/data/sober/appData/logs/ — file NOT named
  latest.log; exact name still unknown, ask for `ls -lt` of that dir.
- GPU: RTX 3080 (GA102), 20-core Intel, 31GB RAM. xfwm4 (compositing=false
  in config but a compositor owns _NET_WM_CM_S0 at runtime, cm=1), NVIDIA.
