#!/bin/sh
# Daemon Void setup — current testing iteration ti-13
#
# The 004 build failed with make's Error 2 hidden by `| tail -n 20`: the real
# compiler error scrolled out of the photographed output. This version keeps
# the FULL build log on disk and prints every error block on failure, then
# hands back the exact serial rebuild command that lists errors in order.
set -u
ROOT="$(cd "$(dirname "$0")" && pwd)"
echo "=== Daemon Void setup (payload $(basename "$0")) ==="
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
  for pkg in base-devel cmake curl pkg-config SDL2 SDL2-devel mesa libglvnd-devel libX11-devel libXtst-devel gtk+3 flatpak xtools; do
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
    echo "[setup] overlay windowed (WM_CLASS=daemon via SDL_VIDEO_X11_WMCLASS)"
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

ls -lh "$ROOT/build/daemon" && echo "BUILD_DONE=YES" || exit 1
ls -lh "$ROOT/build/daemon-boot" >/dev/null 2>&1 && echo "BOOT_DONE=YES" || echo "[setup] WARN: daemon-boot missing (build log above)"

# ---- doctor: the walls a fresh operator box puts up (docs/BOOTSTRAPPER.md) --
echo "[doctor] ---- readiness scan (daemon-boot --check) ----"
"$ROOT/build/daemon-boot" --check 2>&1 || true
if [ -r /proc/sys/kernel/yama/ptrace_scope ]; then
    Y=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo "?")
    if [ "$Y" != "0" ]; then
        echo "[doctor] yama ptrace_scope=$Y blocks reading Sober's memory."
        echo "[doctor]   symptom: 'Sober found — not ready' / BASE 0x0 forever"
        echo "[doctor]   applying once-per-boot fix (sudo sysctl)..."
        if [ -n "${SUDO:-}" ]; then
            $SUDO sysctl kernel.yama.ptrace_scope=0 2>&1 | tail -n 2 || \
                echo "[doctor] sudo failed — run manually:  sudo sysctl kernel.yama.ptrace_scope=0"
        else
            sysctl kernel.yama.ptrace_scope=0 2>&1 | tail -n 2 || true
        fi
        Y2=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo "?")
        echo "[doctor] yama ptrace_scope now=$Y2"
    fi
fi

printf '%s\n' "ti-13" > "$ROOT/.daemon-iteration"
echo "[setup] marked current iteration ti-13 ($ROOT/.daemon-iteration)"

echo "[setup] install desktop entry: sh $ROOT/assets/install.sh"
if sh "$ROOT/assets/install.sh" 2>&1 | tail -n 10; then
    echo "[setup] desktop entry installed"
else
    echo "[setup] WARN: desktop install failed (continuing — build is still usable)"
fi

# Cleanup previous iterations regardless of desktop install outcome — only
# requires a successful build + marker (the build half is already proven by
# BUILD_DONE=YES above). This is what removes daemon-009/010 after ti-13.
if sh "$ROOT/assets/cleanup-previous-iterations.sh" 2>&1; then
    echo "[setup] cleanup checked"
else
    echo "[setup] WARN: cleanup script failed (preserving previous iterations)"
fi
# Headless smoke tests: two seconds of startup logs prove the binaries run.
timeout 2 "$ROOT/build/daemon" 2>&1 | head -n 30 || true
timeout 3 "$ROOT/build/daemon-boot" --console --exit-after 2 2>&1 | head -n 20 || true
echo "[setup] overlay log after Load will be: $ROOT/overlay.log  (or /tmp/daemon-overlay.log if that is not writable)"
echo "[setup] previous iterations (daemon-009, daemon-010, ...) were checked for cleanup above — look for '[cleanup] removed' lines"
echo "RUN: $ROOT/build/daemon-boot   (the manager — click Load once Sober is up)"
echo "     or ~/.local/bin/daemon-boot (ulauncher: type 'daemon')"
echo "     terminal/no-GUI fallback: $ROOT/build/daemon-boot --console"