#!/usr/bin/env bash
# -----------------------------------------------------------------------------
#  packaging/verify.sh -- the gate every artifact has to pass.
#
#  Checks whatever exists under dist/ (so it is useful on both the Windows and
#  the Linux CI job) and fails loudly on anything incomplete. Exit code = number
#  of failed checks. With nothing built at all it fails too: "no artifacts" is
#  not a pass.
#
#    packaging/verify.sh            check dist/
#    packaging/verify.sh --windows  only the Windows drop
#    packaging/verify.sh --linux    only the Linux AppDir/AppImage
# -----------------------------------------------------------------------------
set -uo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$REPO_ROOT/dist"
WANT_WIN=1
WANT_LIN=1
FAILED=0
CHECKS=0

case "${1:-}" in
  --windows) WANT_LIN=0 ;;
  --linux)   WANT_WIN=0 ;;
  "")        ;;
  *)         echo "verify: unknown argument $1" >&2; exit 64 ;;
esac

ok()   { CHECKS=$((CHECKS+1)); printf '  \033[32mok\033[0m    %s\n' "$*"; }
fail() { CHECKS=$((CHECKS+1)); FAILED=$((FAILED+1)); printf '  \033[31mFAIL\033[0m  %s\n' "$*"; }
check(){ if [[ -e "$2" ]]; then ok "$1"; else fail "$1 (missing: ${2#$REPO_ROOT/})"; fi; }

echo "verify: $DIST"

# ------------------------------------------------------------------ windows --
if [[ $WANT_WIN -eq 1 ]]; then
  STAGE="$DIST/windows/glass-widget"
  if [[ -d "$STAGE" ]]; then
    echo "windows drop:"
    check "glass-widget.exe present"        "$STAGE/glass-widget.exe"
    check "offsets_fallback.json shipped"   "$STAGE/offsets_fallback.json"
    check "README.txt shipped"              "$STAGE/README.txt"
    if [[ -f "$STAGE/glass-widget.exe" ]]; then
      if [[ "$(head -c2 "$STAGE/glass-widget.exe")" == "MZ" ]]; then
        ok "glass-widget.exe is a PE image"
      else
        fail "glass-widget.exe is not a PE image (no MZ header)"
      fi
      sz=$(wc -c < "$STAGE/glass-widget.exe")
      if [[ "$sz" -gt 65536 ]]; then ok "glass-widget.exe size sane ($sz bytes)"
      else fail "glass-widget.exe suspiciously small ($sz bytes)"; fi
    fi
    if [[ -f "$STAGE/offsets_fallback.json" ]]; then
      if command -v python3 >/dev/null 2>&1; then
        if python3 -c "import json,sys; json.load(open(sys.argv[1]))" "$STAGE/offsets_fallback.json" 2>/dev/null; then
          ok "offsets_fallback.json parses"
        else
          fail "offsets_fallback.json is not valid JSON"
        fi
      fi
      if cmp -s "$STAGE/offsets_fallback.json" "$REPO_ROOT/roblox_external/offsets_fallback.json"; then
        ok "offsets_fallback.json matches the repo copy"
      else
        fail "offsets_fallback.json differs from roblox_external/offsets_fallback.json"
      fi
    fi
  elif [[ $WANT_LIN -eq 0 ]]; then
    fail "no Windows drop at dist/windows/glass-widget"
  fi
fi

# -------------------------------------------------------------------- linux --
if [[ $WANT_LIN -eq 1 ]]; then
  APPDIR="$DIST/linux/glass-widget.AppDir"
  if [[ -d "$APPDIR" ]]; then
    echo "linux AppDir:"
    check "AppRun present"      "$APPDIR/AppRun"
    check "top-level .desktop"  "$APPDIR/glass-widget.desktop"
    check "top-level icon"      "$APPDIR/glass-widget.png"
    check "hicolor icon"        "$APPDIR/usr/share/icons/hicolor/256x256/apps/glass-widget.png"
    check "offsets_fallback.json shipped" "$APPDIR/usr/share/glass-widget/offsets_fallback.json"

    if [[ -x "$APPDIR/AppRun" ]]; then ok "AppRun is executable"
    else fail "AppRun is not executable"; fi
    if [[ -f "$APPDIR/AppRun" ]] && bash -n "$APPDIR/AppRun" 2>/dev/null; then ok "AppRun parses"
    else fail "AppRun is not valid bash"; fi
    if ! grep -q '@[A-Z_]*@' "$APPDIR/AppRun" "$APPDIR/glass-widget.desktop" 2>/dev/null; then
      ok "no unsubstituted @TEMPLATE@ tokens"
    else
      fail "unsubstituted @TEMPLATE@ tokens left in AppRun/.desktop"
    fi
    if [[ -e "$APPDIR/usr/bin/glass-widget.exe" || -x "$APPDIR/usr/bin/glass-widget" ]]; then
      ok "payload present in usr/bin"
    else
      fail "no payload in $APPDIR/usr/bin"
    fi
    if command -v desktop-file-validate >/dev/null 2>&1; then
      if desktop-file-validate "$APPDIR/usr/share/applications/glass-widget.desktop" >/dev/null 2>&1; then
        ok "desktop entry validates"
      else
        fail "desktop-file-validate rejected the .desktop"
      fi
    fi
  elif [[ $WANT_WIN -eq 0 ]]; then
    fail "no AppDir at dist/linux/glass-widget.AppDir"
  fi

  shopt -s nullglob
  images=("$DIST/linux/"*.AppImage)
  shopt -u nullglob
  if [[ ${#images[@]} -gt 0 ]]; then
    echo "linux AppImage:"
    for img in "${images[@]}"; do
      if [[ -x "$img" ]]; then ok "$(basename "$img") is executable"
      else fail "$(basename "$img") is not executable"; fi
      if [[ "$(head -c4 "$img" | tail -c3)" == "ELF" ]]; then ok "$(basename "$img") is an ELF image"
      else fail "$(basename "$img") has no ELF header"; fi
    done
  fi
fi

if [[ $CHECKS -eq 0 ]]; then
  printf '\033[31mverify: nothing to check - no artifacts under dist/\033[0m\n' >&2
  exit 1
fi

echo
if [[ $FAILED -eq 0 ]]; then
  printf '\033[32mverify: %d/%d checks passed\033[0m\n' "$CHECKS" "$CHECKS"
else
  printf '\033[31mverify: %d of %d checks FAILED\033[0m\n' "$FAILED" "$CHECKS" >&2
fi
exit "$FAILED"
