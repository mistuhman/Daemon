#!/bin/sh
# Remove old extracted test iterations only after the new iteration built and
# installed successfully. Refuse broad/glob-only deletion: a candidate needs
# Daemon's payload fingerprint, and marker-based cleanup is used going forward.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PARENT=$(dirname "$ROOT")
CURRENT=$(basename "$ROOT")

case "$CURRENT" in
  daemon-[0-9][0-9][0-9]) ;;
  *) echo "[cleanup] skip: current directory is not daemon-NNN ($ROOT)"; exit 0 ;;
esac

removed=0
for old in "$PARENT"/daemon-[0-9][0-9][0-9]; do
  [ -d "$old" ] || continue
  [ "$old" != "$ROOT" ] || continue
  base=$(basename "$old")

  safe=0
  if [ -f "$old/.daemon-iteration" ]; then
    safe=1
  elif [ "$base" = daemon-009 ] || [ "$base" = daemon-010 ]; then
    # Legacy payloads predate markers. Require the exact shipped tree shape.
    [ -f "$old/CMakeLists.txt" ] && [ -f "$old/setup.sh" ] && \
      [ -f "$old/assets/daemon.desktop" ] && safe=1
  fi
  if [ "$safe" != 1 ]; then
    echo "[cleanup] keep unmarked/non-Daemon candidate: $old"
    continue
  fi

  rm -rf -- "$old"
  echo "[cleanup] removed previous iteration: $old"
  removed=$((removed + 1))
done
printf '%s\n' "[cleanup] previous iterations removed=$removed; current=$ROOT"
