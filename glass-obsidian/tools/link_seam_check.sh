#!/bin/sh
# ----------------------------------------------------------------------------
#  link_seam_check.sh — catch "undefined reference to our own code" off Windows
# ----------------------------------------------------------------------------
#  Why this exists: payload 012 shipped a build that failed on the operator's
#  box with `undefined reference to platform::HasGraphicalDisplay()` — a
#  function deleted from platform_linux.cpp by an edit that still passed every
#  syntax-only check (the reference lives in the SDL path; -fsyntax-only never
#  links). Syntax checks prove spellings; only a link proves seams.
#
#  What it does: compiles every TU the CMake Linux build uses (SDL + X11
#  defines when the real headers are reachable), relocatable-links each
#  executable's object set (daemon and daemon-boot share TUs but are separate
#  programs - two link groups, so their two mains never collide), and asserts
#  that no still-undefined symbol is ours. Classification is on the MANGLED
#  name: a C symbol (printf, pthread_*, SDL_*, X*) may remain undefined here -
#  external libs resolve those at the real link - while a _Z C++ symbol that is
#  not libstdc++/crt demangles into OUR code. One FAIL line names every seam
#  break, verbatim, like the box would.
#
#  Usage:
#    glass-obsidian/tools/link_seam_check.sh
#    EXTRA_CXXFLAGS="-I/tmp/sdl/... -I/tmp/gl/..." glass-obsidian/tools/link_seam_check.sh
#  On a box with SDL2/X11 devel installed the flags come from sdl2-config and
#  pkg-config automatically; EXTRA_CXXFLAGS is the sandbox path (real headers
#  fetched via codeload, never stubs). Exit 0 clean, 2 on any seam break.
# ----------------------------------------------------------------------------
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

OUT="${TMPDIR:-/tmp}/daemon-checks/link-seam"
rm -rf "$OUT"
mkdir -p "$OUT/objs"

command -v g++ >/dev/null 2>&1 || { echo "link-seam: FAIL (no g++)"; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "link-seam: FAIL (no python3)" ; exit 2; }

# ---- sources: exactly what CMakeLists builds (pinned to it by parse, not copy)
#  Emitted as "A <path>" (daemon target) / "B <path>" (daemon-boot target);
#  shared TUs appear in both groups and are compiled once.
SRC_LIST="$OUT/sources.txt"
python3 - <<'EOF' > "$SRC_LIST"
import re
txt = open('CMakeLists.txt').read()
def grab(name):
    m = re.search(r'set\(' + name + r'(.*?)\)', txt, re.S)
    if not m:
        return []
    out = []
    for p in m.group(1).split():
        p = p.strip().strip('"')
        if p.endswith('.cpp'):
            out.append(p.replace('${IMGUI_DIR}', 'daemon/imgui')
                        .replace('${GLASS_DIR}', 'glass-obsidian'))
    return out
app = []
for n in ('IMGUI_SOURCES', 'IMGUI_BACKEND_SOURCES', 'GLASS_SOURCES',
          'PLATFORM_SOURCES', 'CORE_SOURCES'):
    app += grab(n)
for s in app:
    print('A', s)
m = re.search(r'add_executable\(daemon-boot(.*?)\)', txt, re.S)
if m:
    for p in m.group(1).split():
        p = p.strip().strip('"')
        if p.endswith('.cpp'):
            print('B', p)
EOF

# ---- compile every TU (SDL + X11 defines when the real headers are reachable)
CXXFLAGS="-std=c++17 -O0 -w -Idaemon -Idaemon/features -Idaemon/imgui -Iglass-obsidian/include -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS"
HAVE_SDL=0
if command -v sdl2-config >/dev/null 2>&1 && { [ -f /usr/include/GL/gl.h ] || [ -f /usr/include/GL/glcorearb.h ]; }; then
    CXXFLAGS="$CXXFLAGS $(sdl2-config --cflags) -DHAS_SDL_OVERLAY=1"
    HAVE_SDL=1
fi
if [ -f /usr/include/X11/Xlib.h ]; then
    CXXFLAGS="$CXXFLAGS -DHAS_X11=1"
    [ -f /usr/include/X11/extensions/Xfixes.h ] && CXXFLAGS="$CXXFLAGS -DHAS_XFIXES=1"
fi
EXTRA_CXXFLAGS=${EXTRA_CXXFLAGS:-}

app_objs=""
boot_objs=""
compiled=0
fails=0
while read -r grp src; do
    [ -n "${src:-}" ] || continue
    if [ ! -f "$src" ]; then
        echo "  FAIL  missing source $src (named in CMakeLists)"
        fails=$((fails + 1))
        continue
    fi
    case "$src" in
        *imgui_impl_*) [ "$HAVE_SDL" = 1 ] || continue ;;  # CMake builds backends only with SDL2+GL
    esac
    obj="$OUT/objs/$(echo "$src" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    if [ ! -f "$obj" ]; then
        if g++ $CXXFLAGS $EXTRA_CXXFLAGS -c "$src" -o "$obj" 2> "$OUT/compile.log"; then
            compiled=$((compiled + 1))
        else
            echo "  FAIL  compile $src"
            grep -m3 'error:' "$OUT/compile.log" | sed 's/^/            /'
            fails=$((fails + 1))
            continue
        fi
    fi
    case "$grp" in
        A) app_objs="$app_objs $obj" ;;
        B) boot_objs="$boot_objs $obj" ;;
    esac
done < "$SRC_LIST"

if [ "$fails" -ne 0 ]; then
    echo "link-seam: FAIL ($fails compile/missing failures)"
    exit 2
fi

# ---- relocatable-link each executable's set; seams stay undefined ----------
classify() {  # $1 = linked object, $2 = label
    nm -u "$1" | sed 's/^[[:space:]]*U[[:space:]]*//' | sort -u > "$OUT/undef.txt"
    python3 - "$OUT" "$2" <<'EOF'
import os, re, subprocess, sys
out, label = sys.argv[1], sys.argv[2]
mangled = [l.strip() for l in open(os.path.join(out, "undef.txt")) if l.strip()]
if not mangled:
    print("  FAIL  %s: no undefined symbols at all - nm/ld -r stage is broken" % label)
    sys.exit(2)
demangled = {}
p = subprocess.run(["c++filt"] + mangled, capture_output=True, text=True)
for m, d in zip(mangled, p.stdout.splitlines()):
    demangled[m] = d
ALLOW = (
    "std::", "__gnu_cxx::", "__cxxabiv1::", "__cxa_", "__gxx_", "__gthread_",
    "operator ", "typeinfo for std::", "vtable for std::", "typeinfo name for std::",
    "VTT for std::", "construction vtable for std::", "guard variable for std::",
    "non-virtual thunk to std::", "virtual thunk to std::", "covariant return thunk to std::",
    "typeinfo for __", "vtable for __", "typeinfo name for __", "guard variable for __",
    "VTT for __", "thread-local wrapper routine for", "DW.ref.",
)
BUILTINS = re.compile(
    r"^(const |volatile |unsigned |signed |long |short )*[a-z_][a-z0-9_]*"
    r"( const)?( \*+| &+)*$")
def allowed(m, d):
    if not m.startswith("_Z"):
        return True                      # C / OS / external libs
    if any(d.startswith(p) for p in ALLOW):
        return True
    # "vtable for X" / "typeinfo for X" / "guard variable for X" family
    mfor = re.match(r"^(?:typeinfo for|typeinfo name for|vtable for|VTT for|"
                    r"construction vtable for .*? in |guard variable for) (.+)$", d)
    if mfor:
        t = mfor.group(1)
        return t.startswith(("std::", "__")) or BUILTINS.match(t)
    return False
seams = [(m, demangled[m]) for m in mangled if not allowed(m, demangled[m])]
if seams:
    for m, d in seams:
        print("  FAIL  undefined reference to `%s'" % d)
    print("link-seam: FAIL (%s: %d seam break(s) - the box would die at link)"
          % (label, len(seams)))
    sys.exit(2)
print("link-seam ok · %s · 0 seams" % label)
sys.exit(0)
EOF
}

seam_rc=0
if ! ld -r $app_objs -o "$OUT/all_app.o" 2> "$OUT/ld.log"; then
    echo "  FAIL  ld -r (daemon target)"
    sed -n '1,8p' "$OUT/ld.log" | sed 's/^/            /'
    seam_rc=2
else
    classify "$OUT/all_app.o" "daemon target" || seam_rc=2
fi
if [ "$(echo $boot_objs | wc -w)" -gt 0 ]; then
    if ! ld -r $boot_objs -o "$OUT/all_boot.o" 2> "$OUT/ld.log"; then
        echo "  FAIL  ld -r (daemon-boot target)"
        sed -n '1,8p' "$OUT/ld.log" | sed 's/^/            /'
        seam_rc=2
    else
        classify "$OUT/all_boot.o" "daemon-boot target" || seam_rc=2
    fi
fi

if [ "$seam_rc" -eq 0 ]; then
    echo "link-seam: PASS ($compiled TUs)"
    exit 0
fi
echo "link-seam: FAIL (seam breaks above)"
exit 2
