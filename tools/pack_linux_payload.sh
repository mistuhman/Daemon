#!/bin/bash
# ----------------------------------------------------------------------------
# pack_linux_payload.sh — build a self-extracting mirror/files payload
# ----------------------------------------------------------------------------
# Reproduces the roblox-external-linux-NNN.txt courier format used by the
# Void/Sober delivery chain (see mirror/files/roblox-external-linux-004.txt):
# a sha-gated, fail-closed POSIX sh stub + base64(gzip(tar)) body. The stub
# verifies the archive sha before writing, refuses an existing target dir,
# and re-hashes the extracted file-list (PAYLOAD_SHA256) before reporting
# ROBLOX_EXTERNAL_LINUX_NNN_DONE=YES.
#
# Usage: tools/pack_linux_payload.sh NNN [out-dir]
#   NNN     three-digit payload number, e.g. 005
#   out-dir defaults to mirror/files
#
# Selection mirrors linux-004: the whole app tree, the build subset of
# glass-obsidian (include/src/vendored imgui — no gallery, apps or tests),
# CMakeLists.txt, boot/ (the bootstrapper — docs/BOOTSTRAPPER.md), assets/,
# tools/sober_probe.cpp and setup.sh (sourced from
# assets/setup-linux.sh so the setup logic is tracked in the repo).
# ----------------------------------------------------------------------------
set -eu

NNN="${1:?usage: $0 NNN [out-dir]}"
OUT_DIR="${2:-mirror/files}"
case "$NNN" in [0-9][0-9][0-9]) ;; *) echo "NNN must be three digits, got: $NNN" >&2; exit 2;; esac

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
for c in base64 tar gzip sha256sum mktemp find; do
    command -v "$c" >/dev/null 2>&1 || { echo "missing tool: $c" >&2; exit 2; }
done

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

# ---- assemble the same tree shape linux-004 carried ----------------------
cp -a roblox_external "$STAGE/"
mkdir -p "$STAGE/glass-obsidian"
cp -a glass-obsidian/include "$STAGE/glass-obsidian/"
cp -a glass-obsidian/src "$STAGE/glass-obsidian/"
mkdir -p "$STAGE/glass-obsidian/third_party"
cp -a glass-obsidian/third_party/imgui "$STAGE/glass-obsidian/third_party/"
cp -a CMakeLists.txt "$STAGE/"
cp -a boot "$STAGE/"
cp -a assets "$STAGE/"
mkdir -p "$STAGE/tools"
cp -a tools/sober_probe.cpp tools/sober_attach.cpp tools/sober-attach "$STAGE/tools/"
cp -a assets/setup-linux.sh "$STAGE/setup.sh"
chmod +x "$STAGE/setup.sh"

FILES="$(cd "$STAGE" && find . -type f | wc -l | tr -d ' ')"
# PAYLOAD_SHA256 hashes the sorted file PATH list, exactly as the stub does.
PAYLOAD_SHA256="$(cd "$STAGE" && find . -type f | sort | sha256sum | cut -d' ' -f1)"

# Write the archive OUTSIDE the stage: archiving into $STAGE itself makes tar
# read a directory that grows under it ("file changed as we read it", rc=1).
TARBALL="$(mktemp --suffix=.tar.gz)"
tar -C "$STAGE" -czf "$TARBALL" .
ARCHIVE_SHA256="$(sha256sum "$TARBALL" | cut -d' ' -f1)"
B64="$(base64 -w 0 "$TARBALL")"
rm -f "$TARBALL"
SOURCE_COMMIT="$(git rev-parse --short HEAD)"
OUT="$OUT_DIR/roblox-external-linux-$NNN.txt"
mkdir -p "$OUT_DIR"

{
cat <<EOF
#!/bin/sh
# roblox-external-linux-$NNN — self-extracting Void/Sober payload
# Source commit: $SOURCE_COMMIT
# Packed by tools/pack_linux_payload.sh — do not hand-edit; repack instead.
set -u
UPDATE=$NNN
SOURCE_COMMIT="$SOURCE_COMMIT"
ARCHIVE_SHA256=$ARCHIVE_SHA256
FILES=$FILES
PAYLOAD_SHA256=$PAYLOAD_SHA256
[ -f "\$0" ] || { echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=run-from-file"; exit 1; }
[ \$# -eq 1 ] || { echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=usage: sh /path/to/roblox-external-linux-$NNN.txt /absolute/dir"; exit 1; }
DEST=\$1
case "\$DEST" in /*|[A-Za-z]:/*) ;; *) echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=absolute required"; exit 1 ;; esac
if [ -e "\$DEST" ]; then echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=target-already-exists:\$DEST — run: rm -rf \$DEST"; exit 1; fi
for c in base64 tar sha256sum mktemp; do command -v "\$c" >/dev/null 2>&1 || { echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=missing:\$c"; exit 1; }; done
TMP=\$(mktemp -d) || { echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=mktemp"; exit 1; }
b64d() { base64 -d "\$1" 2>/dev/null || base64 -D "\$1" 2>/dev/null; }
sed -n '/^__ROBLOX_EXTERNAL_LINUX_${NNN}_PAYLOAD__\$/,\$p' "\$0" | tail -n +2 > "\$TMP/b64.txt"
b64d "\$TMP/b64.txt" > "\$TMP/src.tar.gz" 2>/dev/null || { echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=base64"; rm -rf "\$TMP"; exit 1; }
GOT=\$(sha256sum "\$TMP/src.tar.gz" | cut -d' ' -f1)
echo "ARCHIVE_SHA256_GOT=\$GOT"
if [ "\$GOT" != "\$ARCHIVE_SHA256" ]; then echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=archive-sha want=\$ARCHIVE_SHA256 got=\$GOT"; rm -rf "\$TMP"; exit 1; fi
mkdir -p "\$DEST" || { echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=mkdir"; rm -rf "\$TMP"; exit 1; }
tar -xzf "\$TMP/src.tar.gz" -C "\$DEST" || { echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=tar"; rm -rf "\$TMP"; exit 1; }
rm -rf "\$TMP"
GOT2=\$(cd "\$DEST" && find . -type f | sort | sha256sum | cut -d' ' -f1)
echo "PAYLOAD_SHA256_GOT=\$GOT2"
if [ "\$GOT2" != "\$PAYLOAD_SHA256" ]; then echo "ROBLOX_EXTERNAL_LINUX_${NNN}_FAIL=payload-sha want=\$PAYLOAD_SHA256 got=\$GOT2"; exit 1; fi
echo "ROBLOX_EXTERNAL_LINUX_${NNN}_DONE=YES"
ls -lh "\$DEST/assets/roblox-external.desktop" 2>&1 | head -n 5
echo "Next: sh \$DEST/setup.sh"
echo "--- inverse ---"
echo "ARCHIVE_SHA256_WANT=\$ARCHIVE_SHA256 ARCHIVE_SHA256_GOT=\$GOT PAYLOAD_SHA256_WANT=\$PAYLOAD_SHA256 PAYLOAD_SHA256_GOT=\$GOT2 FILES=\$FILES DEST=\$DEST"
exit 0
__ROBLOX_EXTERNAL_LINUX_${NNN}_PAYLOAD__
EOF
printf '%s\n' "$B64"
} > "$OUT"

echo "packed $OUT"
echo "  FILES=$FILES"
echo "  ARCHIVE_SHA256=$ARCHIVE_SHA256"
echo "  PAYLOAD_SHA256=$PAYLOAD_SHA256"

# ---- round-trip proof: extract the file we just wrote --------------------
# The stub refuses an existing target dir, so give it a fresh path under a
# temp parent rather than the mktemp dir itself.
VERIFY_PARENT="$(mktemp -d)"
VERIFY="$VERIFY_PARENT/extract"
if sh "$OUT" "$VERIFY" >/tmp/pack_verify.log 2>&1; then
    echo "  round-trip: OK ($(cd "$VERIFY" && find . -type f | wc -l | tr -d ' ') files extracted, DONE line present)"
else
    cat /tmp/pack_verify.log
    echo "  round-trip: FAILED" >&2
    exit 1
fi
rm -rf "$VERIFY_PARENT"
