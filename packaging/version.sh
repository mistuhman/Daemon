#!/usr/bin/env bash
# Canonical version string for every artifact this repo ships.
#
#   tag on HEAD        -> 1.2.3
#   commits since tag  -> 1.2.3+4.g<sha>
#   no tags at all     -> 0.0.0+<YYYYMMDD>.g<sha>
#   not a git checkout  -> 0.0.0+<YYYYMMDD>
#
# Override everything with VERSION=... in the environment (CI releases do).
# Mirrored by packaging/version.ps1 - keep the two in sync.
set -euo pipefail

if [[ -n "${VERSION:-}" ]]; then printf '%s\n' "$VERSION"; exit 0; fi

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
stamp="$(date -u +%Y%m%d)"

if ! git -C "$root" rev-parse --git-dir >/dev/null 2>&1; then
  printf '0.0.0+%s\n' "$stamp"; exit 0
fi

sha="$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo unknown)"
dirty=""
git -C "$root" diff --quiet HEAD -- 2>/dev/null || dirty=".dirty"

if tag="$(git -C "$root" describe --tags --abbrev=0 2>/dev/null)"; then
  count="$(git -C "$root" rev-list "$tag"..HEAD --count 2>/dev/null || echo 0)"
  ver="${tag#v}"
  if [[ "$count" == "0" ]]; then printf '%s%s\n' "$ver" "$dirty"
  else printf '%s+%s.g%s%s\n' "$ver" "$count" "$sha" "$dirty"; fi
else
  printf '0.0.0+%s.g%s%s\n' "$stamp" "$sha" "$dirty"
fi
