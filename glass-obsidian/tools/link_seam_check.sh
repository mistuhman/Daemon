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
#  defines when the real headers are reachable), relocatable-links them into
#  one object, and asserts that no still-undefined symbol demangles into one of
#  the app's own namespaces. External C symbols (SDL_*, gl*, X*, pthread) and
#  libstdc++ are expected to remain undefined here — our own namespaces are
#  not. One FAIL line names every seam break, verbatim, like the box would.
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

OUT="${TMPDIR:-/tmp}/roblox-external-checks/link-seam"
rm -rf "$OUT"
mkdir -p "$OUT/objs"

command -v g++ >/dev/null 2>&1 || { echo "link-seam: FAIL (no g++)"; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "link-seam: FAIL (no python3)"; exit 2; }

# ---- sources: exactly what CMakeLists builds (pinned to it by parse, not copy)
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
            out.append(p.replace('${IMGUI_DIR}', 'roblox_external/imgui')
                        .replace('${GLASS_DIR}', 'glass-obsidian'))
    return out
srcs = []
for n in ('IMGUI_SOURCES', 'IMGUI_BACKEND_SOURCES', 'GLASS_SOURCES',
          'PLATFORM_SOURCES', 'CORE_SOURCES'):
    srcs += grab(n)
for s in srcs:
    print(s)
EOF

# ---- flags: real headers where they exist; the SDL/X11 paths are the point
CXXFLAGS="-std=c++17 -w -c -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DHAS_SDL_OVERLAY=1"
INCLUDES="-I roblox_external -I roblox_external/features -I glass-obsidian/include -I roblox_external/imgui"
if command -v sdl2-config >/dev/null 2>&1; then
    INCLUDES="$INCLUDES $(sdl2-config --cflags)"
fi
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists x11 2>/dev/null; then
    INCLUDES="$INCLUDES $(pkg-config --cflags x11 xfixes 2>/dev/null)"
    CXXFLAGS="$CXXFLAGS -DHAS_X11=1"
    pkg-config --exists xfixes 2>/dev/null && CXXFLAGS="$CXXFLAGS -DHAS_XFIXES=1"
fi
[ -n "${EXTRA_CXXFLAGS:-}" ] && INCLUDES="$INCLUDES $EXTRA_CXXFLAGS"

# Preflight: a missing include dir reads as "header not found" three TUs later
# and wastes a debugging cycle (this is exactly what a /tmp-cleaned sandbox did
# once). Name the dead dirs up front. Handles both -Idir and "-I dir" forms.
prev_i=0
for d in $INCLUDES; do
    if [ "$prev_i" = "1" ]; then
        [ -d "$d" ] || echo "link-seam: WARN include dir missing: $d"
        prev_i=0
        continue
    fi
    case "$d" in
        -I) prev_i=1 ;;
        -I*)
            p="${d#-I}"
            [ -n "$p" ] && [ ! -d "$p" ] && echo "link-seam: WARN include dir missing: $p"
            ;;
    esac
done

# ---- compile every TU
N=0
FAILS=0
while IFS= read -r src; do
    [ -f "$src" ] || { echo "link-seam: FAIL (missing source $src)"; FAILS=$((FAILS+1)); continue; }
    obj="$OUT/objs/$(echo "$src" | tr / _).o"
    if ! g++ $CXXFLAGS $INCLUDES "$src" -o "$obj" 2> "$OUT/objs/err.txt"; then
        echo "link-seam: FAIL (compile $src)"
        sed -n '1,6p' "$OUT/objs/err.txt"
        FAILS=$((FAILS+1))
    fi
    N=$((N+1))
done < "$SRC_LIST"

# ---- one relocatable link: every app-internal reference must resolve now
if ! ld -r "$OUT"/objs/*.o -o "$OUT/combined.o" 2> "$OUT/ld-err.txt"; then
    echo "link-seam: FAIL (relocatable link)"
    sed -n '1,10p' "$OUT/ld-err.txt"
    exit 2
fi

# ---- anything undefined that belongs to us is a seam break.
# nm prints "U <symbol>"; take the last field FIRST (demangling the whole line
# would drop the namespace off the tail), then demangle, then grep namespaces.
SEAM='^(platform|x11overlay|overlay|menuui|menu|cache|game|features|mem|process|offsets|offsets_dynamic|offsets_live|sober|portwatch|media|config|servers|obsidian|ImGui|input|mover|worldenv|worldesp)::'
BRK="$OUT/breaks.txt"
nm -u "$OUT/combined.o" | awk '{print $NF}' | c++filt | grep -E "$SEAM" > "$BRK" || true

if [ -s "$BRK" ] || [ "$FAILS" != "0" ]; then
    [ -s "$BRK" ] && { echo "link-seam: UNDEFINED IN OUR OWN NAMESPACES:"; cat "$BRK"; }
    echo "link-seam: FAIL ($N TUs compiled, $FAILS compile errors, $(wc -l < "$BRK") seam breaks)"
    exit 2
fi
echo "link-seam: PASS ($N TUs, combined relocatable link, zero app-namespace seam breaks)"
exit 0
