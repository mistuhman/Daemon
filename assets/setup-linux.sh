#!/bin/sh
# roblox-external Void setup — payload linux-005+
#
# The 004 build failed with make's Error 2 hidden by `| tail -n 20`: the real
# compiler error scrolled out of the photographed output. This version keeps
# the FULL build log on disk and prints every error block on failure, then
# hands back the exact serial rebuild command that lists errors in order.
set -u
ROOT="$(cd "$(dirname "$0")" && pwd)"
echo "=== roblox-external Void setup (payload $(basename "$0")) ==="
if [ -r /proc/sys/kernel/yama/ptrace_scope ]; then
    Y=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo "?")
    echo "[setup] yama ptrace_scope=$Y"
fi
if [ "$(id -u)" = "0" ]; then SUDO=""; else SUDO="sudo"; fi
if command -v xbps-install >/dev/null 2>&1; then
  # Repository sync and each package are best-effort: a broken unrelated shlib
  # in one transaction must not abort the setup (004 hit libopenshot/libjsoncpp
  # and libxfce4windowing/libdisplay-info this way — neither is needed here).
  $SUDO xbps-install -Sy 2>&1 | tail -n 5 || true
  for pkg in base-devel cmake curl pkg-config SDL2 SDL2-devel mesa libglvnd-devel libX11-devel libXtst-devel flatpak xtools; do
    echo "[setup] try $pkg..."
    $SUDO xbps-install -y "$pkg" 2>&1 | tail -n 3 || echo "[setup] WARN $pkg (continuing)"
  done
  $SUDO xbps-install -y vulkan-loader 2>&1 | tail -n 3 || true
fi
echo "[setup] $(g++ --version 2>/dev/null | head -n1 || echo 'g++ NOT FOUND')"

LOG="$ROOT/build.log"
rm -f "$LOG"
echo "[setup] cmake configure (log: $LOG)"
if ! cmake -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release -S "$ROOT" >>"$LOG" 2>&1; then
    echo "[setup] CMAKE CONFIGURE FAILED — full log:"
    cat "$LOG"
    exit 1
fi
if grep -q "SDL2+GL found" "$LOG"; then
    echo "[setup] overlay windowed (WM_CLASS=roblox-external via SDL_VIDEO_X11_WMCLASS)"
else
    echo "[setup] overlay headless (SDL2/GL missing — ESP/aim still run via logs)"
fi

# Parallel build for speed, but the WHOLE output goes to the log instead of a
# 20-line tail, so a failure can never hide the error that caused it.
echo "[setup] build (full log: $LOG)"
if cmake --build "$ROOT/build" -j"$(nproc 2>/dev/null || echo 4)" >>"$LOG" 2>&1; then
    echo "[setup] build OK"
    tail -n 5 "$LOG"
else
    rc=$?
    echo "[setup] ################################################################"
    echo "[setup] BUILD FAILED — every error block in $LOG :"
    echo "[setup] ################################################################"
    grep -n -B3 -A6 -E "error:|fatal error:|Error [0-9]+|undefined reference" "$LOG" || true
    echo
    echo "[setup] if the parallel output above is interleaved, the serial build"
    echo "[setup] prints one TU at a time — run this and photograph its output:"
    echo "        cmake --build \"$ROOT/build\" -j1"
    exit "$rc"
fi

ls -lh "$ROOT/build/external" && echo "BUILD_DONE=YES" || exit 1
echo "[setup] install desktop entry: sh $ROOT/assets/install.sh"
sh "$ROOT/assets/install.sh" 2>&1 | tail -n 10 || true
# Headless smoke test: two seconds of startup logs prove the binary runs.
timeout 2 "$ROOT/build/external" 2>&1 | head -n 30 || true
echo "RUN: $ROOT/build/external  or  ~/.local/bin/roblox-external (ulauncher: type 'roblox')"
