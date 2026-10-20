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

OUT="${TMPDIR:-/tmp}/daemon-checks/link-seam"
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
            out.append(p.replace('${IMGUI_DIR}', 'daemon/imgui')
                        .replace('${GLASS_DIR}', 'glass-obsidian'))
    return out
srcs = []
for n in ('IMGUI_SOURCES', 'IMGUI_BACKEND_SOURCES', 'GLASS_SOURCES',
          'PLATFORM_SOURCES', 'CORE_SOURCES'):
    srcs += grab(n)
for s in srcs:
    print(s)
