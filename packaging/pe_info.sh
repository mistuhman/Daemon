#!/usr/bin/env bash
# -----------------------------------------------------------------------------
#  packaging/pe_info.sh -- dependency-free PE/COFF inspector.
#
#  Reads a Windows executable with od(1) alone, so it works identically in Git
#  Bash on the Windows runner and in a Linux container. This is what turns
#  "the build said it succeeded" into "the file is a 64-bit Windows GUI image
#  that imports the libraries this project links against".
#
#    packaging/pe_info.sh glass-widget.exe
#    packaging/pe_info.sh glass-widget.exe --expect-machine x64 \
#                                          --expect-subsystem gui \
#                                          --expect-import d3d11.dll \
#                                          --expect-import winhttp.dll
#
#  Prints key=value lines on stdout; expectation failures go to stderr.
#  Exit: 0 ok, 1 not a PE or an expectation failed, 64 usage error.
# -----------------------------------------------------------------------------
set -uo pipefail

FILE=""
EXPECT_MACHINE=""
EXPECT_SUBSYSTEM=""
EXPECT_IMPORTS=()
MIN_SIZE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --expect-machine)   EXPECT_MACHINE="${2:?}";   shift 2 ;;
    --expect-subsystem) EXPECT_SUBSYSTEM="${2:?}"; shift 2 ;;
    --expect-import)    EXPECT_IMPORTS+=("${2:?}"); shift 2 ;;
    --min-size)         MIN_SIZE="${2:?}";         shift 2 ;;
    -h|--help)          sed -n '2,18p' "${BASH_SOURCE[0]}"; exit 0 ;;
    -*)                 echo "pe_info: unknown option $1" >&2; exit 64 ;;
    *)                  FILE="$1"; shift ;;
  esac
done

[[ -n "$FILE" ]] || { echo "pe_info: usage: pe_info.sh <file.exe> [--expect-...]" >&2; exit 64; }
[[ -f "$FILE" ]] || { echo "pe_info: no such file: $FILE" >&2; exit 1; }

fail_count=0
bad() { printf 'pe_info: FAIL %s\n' "$*" >&2; fail_count=$((fail_count + 1)); }

# Little-endian unsigned integer of $2 bytes at offset $1. Byte-at-a-time so it
# does not depend on the host's endianness or on od's wider output formats.
le() {
  local off=$1 n=$2 i=0 v=0 b
  for b in $(od -An -v -tu1 -j "$off" -N "$n" -- "$FILE" | tr -s ' ' '\n' | sed '/^$/d'); do
    v=$(( v + (b << (8 * i)) ))
    i=$((i + 1))
  done
  printf '%d\n' "$v"
}

size=$(wc -c < "$FILE" | tr -d ' ')
printf 'file=%s\n' "$FILE"
printf 'size=%s\n' "$size"

if command -v sha256sum >/dev/null 2>&1; then
  printf 'sha256=%s\n' "$(sha256sum "$FILE" | cut -d' ' -f1)"
elif command -v shasum >/dev/null 2>&1; then
  printf 'sha256=%s\n' "$(shasum -a 256 "$FILE" | cut -d' ' -f1)"
fi

# ------------------------------------------------------------------ DOS hdr --
if [[ "$(head -c 2 -- "$FILE")" != "MZ" ]]; then
  printf 'format=unknown\n'
  bad "no MZ header - not a PE image"
  exit 1
fi

e_lfanew=$(le 60 4)
if (( e_lfanew <= 0 || e_lfanew + 24 > size )); then
  printf 'format=unknown\n'
  bad "bogus e_lfanew ($e_lfanew) for a $size byte file"
  exit 1
fi

if (( $(le "$e_lfanew" 4) != 17744 )); then   # 0x00004550 = "PE\0\0"
  printf 'format=unknown\n'
  bad "no PE signature at e_lfanew=$e_lfanew"
  exit 1
fi

# ----------------------------------------------------------------- COFF hdr --
coff=$((e_lfanew + 4))
machine=$(le "$coff" 2)
sections=$(le $((coff + 2)) 2)
timestamp=$(le $((coff + 4)) 4)
opt_size=$(le $((coff + 16)) 2)
characteristics=$(le $((coff + 18)) 2)

case "$machine" in
  34404) machine_name=x64 ;;     # 0x8664
  332)   machine_name=x86 ;;     # 0x014c
  43620) machine_name=arm64 ;;   # 0xaa64
  *)     machine_name=$(printf '0x%04x' "$machine") ;;
esac

# ------------------------------------------------------- optional header -----
opt=$((coff + 20))
magic=$(le "$opt" 2)
case "$magic" in
  523) format=PE32+ ;;           # 0x20b
  267) format=PE32  ;;           # 0x10b
  *)   format=$(printf 'unknown(0x%03x)' "$magic") ;;
esac

# Subsystem, DllCharacteristics and SizeOfImage sit at the same offset in both
# PE32 and PE32+ (the wider ImageBase is cancelled out by the missing BaseOfData).
entry=$(le $((opt + 16)) 4)
size_of_image=$(le $((opt + 56)) 4)
subsystem=$(le $((opt + 68)) 2)
dll_chars=$(le $((opt + 70)) 2)

case "$subsystem" in
  2) subsystem_name=gui ;;
  3) subsystem_name=console ;;
  *) subsystem_name="$subsystem" ;;
esac

flags=""
(( characteristics & 0x0002 )) && flags="$flags,executable"
(( characteristics & 0x0020 )) && flags="$flags,large-address-aware"
(( characteristics & 0x2000 )) && flags="$flags,dll"
(( dll_chars      & 0x0020 )) && flags="$flags,high-entropy-va"
(( dll_chars      & 0x0040 )) && flags="$flags,dynamic-base"
(( dll_chars      & 0x0100 )) && flags="$flags,nx-compat"
flags="${flags#,}"

printf 'format=%s\n'        "$format"
printf 'machine=%s\n'       "$machine_name"
printf 'subsystem=%s\n'     "$subsystem_name"
printf 'sections=%s\n'      "$sections"
printf 'entry_point=0x%x\n' "$entry"
printf 'size_of_image=%s\n' "$size_of_image"
printf 'opt_header=%s\n'    "$opt_size"
printf 'flags=%s\n'         "$flags"
if command -v date >/dev/null 2>&1; then
  printf 'linked=%s\n' "$(date -u -d "@$timestamp" '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo "$timestamp")"
fi

# Import-table DLL names are stored as plain ASCII in the image, so a byte scan
# finds them without walking the data directories.
imports=$(LC_ALL=C grep -aoiE '[A-Za-z0-9_.+-]+\.dll' -- "$FILE" 2>/dev/null \
          | tr 'A-Z' 'a-z' | sort -u | tr '\n' ' ')
printf 'dll_refs=%s\n' "${imports% }"

# ------------------------------------------------------------ expectations --
if [[ -n "$EXPECT_MACHINE" && "$machine_name" != "$EXPECT_MACHINE" ]]; then
  bad "machine is $machine_name, expected $EXPECT_MACHINE"
fi
if [[ -n "$EXPECT_SUBSYSTEM" && "$subsystem_name" != "$EXPECT_SUBSYSTEM" ]]; then
  bad "subsystem is $subsystem_name, expected $EXPECT_SUBSYSTEM"
fi
if (( MIN_SIZE > 0 && size < MIN_SIZE )); then
  bad "size $size is below the expected minimum $MIN_SIZE"
fi
if (( ${#EXPECT_IMPORTS[@]} > 0 )); then
  for want in "${EXPECT_IMPORTS[@]}"; do
    if ! printf ' %s ' "$imports" | grep -qi -- " $(printf '%s' "$want" | tr 'A-Z' 'a-z') "; then
      bad "no reference to $want in the image"
    fi
  done
fi

(( fail_count == 0 )) || exit 1
exit 0
