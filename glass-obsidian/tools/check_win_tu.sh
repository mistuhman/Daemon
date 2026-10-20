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
#  The default list is the 38 parseable TUs of the host app (features/ -
#  every folder with a .cpp - plus menu, config, offsets_live, memory, process, game, cache and
#  platform/platform_win); main.cpp, overlay.cpp and media_tracker.cpp need
#  the D3D/COM stubs this folder does not carry. (cache.cpp includes the
#  project's own media_tracker.h, which is self-contained raw COM and parses
#  against the stub <Windows.h> just fine.) The stub also carries
#  winhttp.h
#  and shellapi.h, because that is where MSVC-only names hide: the first real
#  build of features/servers/servers_http.cpp failed on WINHTTP_URL_COMPONENTS
#  (not a type), a `const DWORD*` handed to an LPVOID parameter, SE_ERR_FNMF
#  (not a code) and a std::string assembled from a std::wstring - all four
#  catchable here.
#
#  tools/check_win_tu.sh                          # menu.cpp
#  tools/check_win_tu.sh menu.cpp memory.cpp       # or any list of app-relative paths
#  OBSIDIAN_APP_DIR=../daemon tools/check_win_tu.sh
# -----------------------------------------------------------------------------
set -u

lib=$(cd "$(dirname "$0")/.." && pwd)                 # glass-obsidian/
app=${OBSIDIAN_APP_DIR:-\"$lib/../daemon\"}
cc=${CXX:-g++}
# menu.cpp is the default because it is the one TU that is *only* Win32 glue plus
# the shared Snapshot struct. Everything else under features/ plus the core TUs
# parse against the stub too, so the default list is the whole app minus the
# D3D/COM half (overlay.cpp, media_tracker.cpp) and main.cpp (WinMain is checked
# by hand on the Windows side).
files=${*:-menu.cpp features/players/players.cpp features/config/config.cpp
       features/click_teleport/click_teleport.cpp features/servers/servers.cpp
       features/servers/servers_http.cpp offsets_live.cpp
       memory.cpp roblox_process.cpp game.cpp cache.cpp platform/platform_win.cpp
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

if [ ! -f "$app/menu.cpp" ]; then
    echo "host app not found at: $app"
    echo "(this check is only useful next to the daemon checkout;"
    echo " set OBSIDIAN_APP_DIR to point at it)"
    exit 2
fi

log=$(mktemp) || exit 2
fails=0

# The include-path trap this cannot otherwise see (comment lines are excluded, so a
# TU explaining why it does NOT use std::thread is not a hit): the project puts $(ProjectDir) on
#  the include path, so daemon/process.h SHADOWS the CRT <process.h>, and
# MSVC's <thread> includes <process.h> for _beginthreadex. The result is two errors
# inside VC/tools/.../include/thread that name no project file at all. g++ here has a
# real <thread> and no shadowing, so it cannot catch it -- hence a grep.
if grep -rn "^#include <thread>\|std::thread\|std::async\|#include <future>" \
        --include=*.cpp --include=*.h --include=*.hpp "$app" 2>/dev/null | \
        grep -v imgui | grep -vE ':[0-9]+: *(//|/\*|\*)' ; then
    echo "  ^^ those need the project's own thread start (CreateThread, as main.cpp and"
    echo "     cache.cpp do) or process.h has to stop shadowing the CRT header - see"
    echo "     HANDOFF section 9. std::thread does not compile in this project."
    fails=$((fails + 1))
fi
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
