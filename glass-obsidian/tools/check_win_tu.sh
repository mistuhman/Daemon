#!/bin/sh
# -----------------------------------------------------------------------------
#  tools/check_win_tu.sh  --  parse the host app's Win32 glue, off Windows
# -----------------------------------------------------------------------------
#  The app's UI half (daemon/ui/menu_ui.cpp) is platform-free and is
#  fully exercised by tools/menu_shots.cpp. The other half -- menu.cpp and
#  friends -- is the part nobody can build here, and it is where a change to a
#  shared struct quietly breaks: snprintf writing into a `const char*` field
#  (MSVC C2664), a format specifier that no longer matches its argument, a
#  struct member that changed type under a caller.
#
#  This runs `-fsyntax-only` with the real include paths and a stub <Windows.h>
#  (tools/win32_stub), so those are caught in seconds instead of on the machine
#  with the SDK. It is a parse, not a link: nothing here can be built or run.
#  The default list is the parseable TUs of the Linux app (features/ -
#  every folder with a .cpp - plus menu_linux, ui/menu_ui, config, offsets_live,
#  offsets_dynamic, memory, media_tracker, game, cache and the platform seam).
#  The stub <Windows.h> is kept first on the include path as a tripwire: no TU
#  may include it, and if one ever does, the parse continues far enough to name
#  the offender instead of dying on the missing header. (The stub also still
#  carries winhttp.h and shellapi.h from the Windows era - harmless, and they
#  keep old tooling invocations working.)
#
#  tools/check_win_tu.sh                          # menu_linux.cpp
#  tools/check_win_tu.sh menu_linux.cpp memory.cpp # or any list of app-relative paths
#  OBSIDIAN_APP_DIR=../daemon tools/check_win_tu.sh
# -----------------------------------------------------------------------------
set -u

lib=$(cd "$(dirname "$0")/.." && pwd)                 # glass-obsidian/
app=${OBSIDIAN_APP_DIR:-"$lib/../daemon"}
cc=${CXX:-g++}
# menu_linux.cpp is the default because it is the menu's platform glue - the TU
# most likely to drift from the shared snapshot/config structs. Everything else
# under features/ plus the core TUs and the platform seam parses too, so the
# default list is the whole app.
files=${*:-menu_linux.cpp ui/menu_ui.cpp features/players/players.cpp
       features/config/config.cpp
       features/click_teleport/click_teleport.cpp features/servers/servers.cpp
       offsets_live.cpp offsets_dynamic.cpp
       memory.cpp media_tracker.cpp game.cpp cache.cpp
       platform/platform_linux.cpp platform/sober_scanner.cpp
       platform/port_watcher.cpp platform/overlay_linux.cpp
       platform/main_linux.cpp platform/servers_http_linux.cpp
       platform/streamproof_watcher.cpp
       features/movement/mover.cpp features/flight/flight.cpp
       features/infinite_jump/infinite_jump.cpp features/noclip/noclip.cpp
       features/freecam/freecam.cpp features/camera/camera_drive.cpp
       features/waypoints/waypoints.cpp features/aimbot/aimbot.cpp
       features/esp/esp.cpp features/fov_changer/fov_changer.cpp
       features/walkspeed/walkspeed.cpp
       features/hitbox_expander/hitbox_expander.cpp
       features/inventory_checker/inventory_checker.cpp
       features/skybox_changer/skybox_changer.cpp
       features/triggerbot/triggerbot.cpp features/raycast/world_ray.cpp
       features/antiafk/antiafk.cpp features/gravity/gravity.cpp
       features/hipheight/hipheight.cpp features/nofall/nofall.cpp
       features/fpscap/fps_cap.cpp
       features/jumppower/jumppower.cpp features/hitsound/hitsound.cpp
       features/world/env.cpp features/world/world_esp.cpp
       features/players/friends.cpp features/explorer/explorer.cpp features/fling/fling.cpp}

if [ ! -f "$app/menu_linux.cpp" ]; then
    echo "host app not found at: $app"
    echo "(this check is only useful next to the daemon checkout;"
    echo " set OBSIDIAN_APP_DIR to point at it)"
    exit 2
fi

log=$(mktemp) || exit 2
fails=0

# (The Windows-era std::thread/CreateThread tripwire was removed with the Linux
# port: it guarded the MSVC include-shadowing trap ($(ProjectDir) made
# daemon/process.h shadow the CRT <process.h>, which broke <thread> on Windows).
# Linux g++ has real <thread> and the app links Threads::Threads, so a grep that
# fails the check on std::thread would now be a false constraint.)
for f in $files; do
    if [ ! -f "$app/$f" ]; then
        echo "  SKIP    $f (no such file under $app)"
        continue
    fi
    if "$cc" -std=c++17 -fsyntax-only -Wall -Wextra -Wformat=2 \
            -I"$lib/tools/win32_stub" -I"$app" -I"$app/features" -I"$app/imgui" \
            -I"$lib/include" "$app/$f" > "$log" 2>&1; then
        echo "  ok      $f"
    else
        echo "  FAIL    $f"
        grep -E "error:" "$log" | head -8 | sed 's/^/            /'
        [ -s "$log" ] || echo "            (no error line captured)"
        fails=$((fails + 1))
    fi
done
rm -f "$log"

if [ "$fails" -eq 0 ]; then
    echo "win32 glue parses"
else
    echo "$fails translation unit(s) failed"
fi
exit "$fails"
