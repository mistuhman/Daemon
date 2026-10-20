#!/bin/sh
# boot/test/smoke.sh — bootstrapper smoke, runnable with no GTK, no X11, no
# Sober (the sandbox conditions). Proves: build of both binaries, readiness
# scan honesty, console loop, GTK flow against the fake libgtk-3.so.0 (labels,
# timers, Load/Unload click plumbing), and overlay supervision (spawn
# --locked, SIGTERM on exit).
#
# Run from the repo root after a build (build/daemon-boot present):
#   sh boot/test/smoke.sh
# Or let it build the two binaries itself:
#   sh boot/test/smoke.sh --build
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build-smoke"
WORK="$(mktemp -d /tmp/boot-smoke.XXXXXX)"
FAILS=0

ok()   { echo "SMOKE_OK   $1"; }
bad()  { echo "SMOKE_FAIL $1"; FAILS=$((FAILS+1)); }

# ---- 0. optional build (headless config: no SDL/GL needed) -----------------
if [ "${1:-}" = "--build" ]; then
    FLAGS="-O1 -std=c++17 -I$ROOT -I$ROOT/daemon -DPLATFORM_LINUX -pthread"
    mkdir -p "$BUILD"
    for t in daemon/platform/platform_linux.cpp \
             daemon/platform/sober_scanner.cpp \
             daemon/platform/port_watcher.cpp; do
        g++ $FLAGS -c "$ROOT/$t" -o "$BUILD/$(basename "$t" .cpp).o" || exit 1
    done
    for t in boot/bootstrap_core.cpp boot/gtk3_loader.cpp boot/main_boot.cpp; do
        g++ $FLAGS -c "$ROOT/$t" -o "$BUILD/$(basename "$t" .cpp).o" || exit 1
    done
    g++ $FLAGS -o "$BUILD/daemon-boot" "$BUILD"/*.o -ldl -lpthread || exit 1
    # tiny overlay stand-in so supervision has something to spawn
    printf '#!/bin/sh\necho fake-overlay "$@"\nsleep 60\n' > "$BUILD/daemon"
    chmod +x "$BUILD/daemon"
fi
BOOT="${SMOKE_BOOT:-$BUILD/daemon-boot}"
if [ ! -x "$BOOT" ]; then
    BOOT="$ROOT/build/daemon-boot"
fi
if [ ! -x "$BOOT" ]; then
    echo "no daemon-boot found — run 'sh boot/test/smoke.sh --build' or build first"
    exit 1
fi
echo "using bootstrapper: $BOOT"

# ---- 1. --check (readiness scan prints and exits) --------------------------
if "$BOOT" --check > "$WORK/check.out" 2>&1; then
    grep -q '^sober=' "$WORK/check.out" && ok "--check prints readiness" || bad "--check output shape"
else
    bad "--check exit code"
fi

# ---- 2. console loop: status + auto-load supervision (in a sandbox dir with
#         a fake overlay binary, so a dev box never spawns the real app) ----
BOOTDIR="$WORK/bootdir"
mkdir -p "$BOOTDIR"
cp "$BOOT" "$BOOTDIR/"
printf '#!/bin/sh\necho fake-overlay "$@"\nsleep 60\n' > "$BOOTDIR/daemon"
chmod +x "$BOOTDIR/daemon"

if timeout 10 "$BOOTDIR/daemon-boot" --console --auto-load --force-load \
        --exit-after 2 > "$WORK/console.out" 2>&1; then
    grep -q 'looking for Sober' "$WORK/console.out" && ok "console watches for Sober" \
        || bad "console output missing status"
    grep -q 'auto-load: overlay launched' "$WORK/console.out" \
        && ok "auto-load launches the overlay (--locked)" || bad "auto-load did not launch"
else
    bad "console run (exit-after) failed"
fi

# ---- 3. fake-GTK flow: labels + timers + Load/Unload clicks ---------------
FAKE="$WORK/fakegtk"
mkdir -p "$FAKE"
if cc -shared -fPIC -o "$FAKE/libgtk-3.so.0" "$ROOT/boot/test/fake_gtk3.c" 2>"$WORK/cc.err"; then
    if (cd "$BOOTDIR" && LD_LIBRARY_PATH="$FAKE" timeout 15 ./daemon-boot \
            > "$WORK/gtk.out" 2>&1); then
        grep -q 'label=looking for Sober' "$WORK/gtk.out" && ok "GTK status label updates" \
            || bad "GTK status label never set"
        grep -q 'Load: overlay launched' "$WORK/gtk.out" && ok "Load click launches overlay (--locked)" \
            || bad "Load click did not launch"
        grep -q 'Unload: SIGTERM sent' "$WORK/gtk.out" && ok "Unload click stops the overlay" \
            || bad "Unload click did not stop"
    else
        bad "fake-GTK run failed"
    fi
else
    bad "fake gtk3 stub failed to compile"
fi

# ---- 4. supervision: nothing named fake-overlay may outlive the manager ----
sleep 0.5
if pgrep -f "$BOOTDIR/daemon" > /dev/null 2>&1; then
    bad "overlay process outlived the bootstrapper"
    pkill -f "$BOOTDIR/daemon" || true
else
    ok "overlay lifecycle owned by the bootstrapper"
fi

echo "----"
if [ "$FAILS" -eq 0 ]; then echo "SMOKE_PASS"; else echo "SMOKE_FAILS=$FAILS"; exit 1; fi
