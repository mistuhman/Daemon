#!/bin/sh
# Small diagnostic-only payload for the existing mirror courier.
set -eu
cd "$(dirname "$0")/.."
OUT=${1:?usage: pack_attach_probe.sh mirror/files/name.txt}
SOURCE=$(mktemp)
trap 'rm -f "$SOURCE"' EXIT HUP INT TERM
# Inline the shared inspector so the small payload remains standalone.
awk '/^#include "[.][.]\/roblox_external\/platform\/linux_elf.h"/ {
 while ((getline line < "roblox_external/platform/linux_elf.h") > 0)
   if (line != "#pragma once") print line
 next
} { print }' tools/sober_attach.cpp > "$SOURCE"
SHA=$(sha256sum "$SOURCE" | cut -d' ' -f1)
{
cat <<'HEADER'
#!/bin/sh
# Independent read-only Linux ELF probe. No installer, offsets or writes.
set -eu
WORK=$(mktemp -d "${TMPDIR:-/tmp}/sober-elf.XXXXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
cat > "$WORK/probe.cpp" <<'SOBER_ELF_SOURCE_END'
HEADER
cat "$SOURCE"
cat <<EOF
SOBER_ELF_SOURCE_END
printf '%s  %s\n' '$SHA' "\$WORK/probe.cpp" | sha256sum -c
EOF
cat <<'FOOTER'
"${CXX:-g++}" -std=c++17 -O2 "$WORK/probe.cpp" -ldl -o "$WORK/probe"
if [ "$#" -eq 0 ]; then set -- --images; fi
"$WORK/probe" "$@"
FOOTER
} > "$OUT"
sha256sum "$OUT"
