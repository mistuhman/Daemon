#!/bin/sh
# Remove old extracted test iterations after the new iteration built.
# Safety: a candidate must look like a Daemon payload, not any random
# daemon-NNN folder the user created. Marker-based going forward;
# legacy payloads (009/010 predate markers) are recognized by shipped tree shape.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PARENT=$(dirname "$ROOT")
CURRENT=$(basename "$ROOT")

case "$CURRENT" in
  daemon-[0-9][0-9][0-9]) ;;
  *) echo "[cleanup] skip: current directory is not daemon-NNN ($ROOT)"; exit 0 ;;
esac

# Guard: only run cleanup if current iteration is marked — proves the new
# build succeeded and was marked before we delete predecessors.
if [ ! -f "$ROOT/.daemon-iteration" ]; then
  echo "[cleanup] skip: current has no .daemon-iteration marker (build not marked yet)"; exit 0
fi

removed=0
for old in "$PARENT"/daemon-[0-9][0-9][0-9]; do
  [ -d "$old" ] || continue
  [ "$old" != "$ROOT" ] || continue
  base=$(basename "$old")

  safe=0
  reason=""
  if [ -f "$old/.daemon-iteration" ]; then
    safe=1
    reason="marker $(cat "$old/.daemon-iteration" 2>/dev/null | head -n1)"
  elif [ -f "$old/CMakeLists.txt" ] && { [ -f "$old/setup.sh" ] || [ -d "$old/build" ] || [ -f "$old/assets/daemon.desktop" ]; }; then
    # Legacy payload shape — pre-marker iterations (009/010 and any future
    # without marker but with Daemon tree). Require CMakeLists + one more
    # payload file to avoid deleting a user folder that happens to be named
    # daemon-123.
    safe=1
    reason="legacy payload shape"
  fi
  if [ "$safe" != 1 ]; then
    echo "[cleanup] keep unmarked/non-Daemon candidate: $old"
    continue
  fi

  # Extra guard: never delete a directory that looks actively used (recently
  # modified within 5 minutes) — operator might be mid-extraction.
  # Best-effort, not fatal.
  if [ -n "$(find "$old" -maxdepth 0 -mmin -5 2>/dev/null)" ]; then
    echo "[cleanup] keep recently modified (within 5m, may be active): $old"
    continue
  fi

  echo "[cleanup] removing previous iteration ($reason): $old"
  rm -rf -- "$old"
  echo "[cleanup] removed previous iteration: $old"
  removed=$((removed + 1))
done
printf '%s\n' "[cleanup] previous iterations removed=$removed; current=$ROOT ($(cat "$ROOT/.daemon-iteration" 2>/dev/null | head -n1))"
