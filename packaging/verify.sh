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
#
#  Environment:
#    EXPECT_MACHINE   PE machine the exe must report (default x64)
# -----------------------------------------------------------------------------
set -uo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$REPO_ROOT/dist"
PE_INFO="$REPO_ROOT/packaging/pe_info.sh"
EXPECT_MACHINE="${EXPECT_MACHINE:-x64}"
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

# On CI the raw log needs a download to read; an annotation does not. Every
# failure therefore also lands on the run page, which is the whole reason a
# broken Windows build used to be undiagnosable without pulling the zip.
annotate() {
  [[ -n "${GITHUB_ACTIONS:-}" ]] || return 0
  printf '::%s title=verify::%s\n' "$1" "$(printf '%s' "$2" | tr '\n' ' ' | sed 's/::/:/g')"
}

ok()   { CHECKS=$((CHECKS+1)); printf '  \033[32mok\033[0m    %s\n' "$*"; }
fail() { CHECKS=$((CHECKS+1)); FAILED=$((FAILED+1)); printf '  \033[31mFAIL\033[0m  %s\n' "$*"; annotate error "$*"; }
skip() { printf '  \033[33mskip\033[0m  %s\n' "$*"; }
# Not a gate: something worth knowing that should not block a release.
warn() { printf '  \033[33mwarn\033[0m  %s\n' "$*"; annotate warning "$*"; }
check(){ if [[ -e "$2" ]]; then ok "$1"; else fail "$1 (missing: ${2#"$REPO_ROOT"/})"; fi; }

# python3 on Linux, python on the Windows runner - resolved up front so a
# missing interpreter is reported as a skip rather than silently dropping the
# check.
PY_BIN=""
for c in python3 python; do
  if command -v "$c" >/dev/null 2>&1; then PY_BIN="$c"; break; fi
done

# Runs pe_info.sh, echoes its findings indented, and turns its verdict into one
# pass/fail line. This is what makes "we produced an .exe" an assertion rather
# than a hope.
inspect_pe() {
  local label="$1" path="$2"; shift 2
  if [[ ! -f "$PE_INFO" ]]; then
    skip "$label: packaging/pe_info.sh is missing"
    return
  fi
  local out rc
  out="$(bash "$PE_INFO" "$path" "$@" 2>&1)"; rc=$?
  printf '%s\n' "$out" | sed 's/^/        /'
  # The facts travel to the run page too, so "what did we actually build" is
  # answerable without the artifact.
  annotate notice "$label: $(printf '%s' "$out" | grep -E '^(format|machine|subsystem|size|sha256|dll_refs)=' | tr '\n' ' ')"
  if [[ $rc -eq 0 ]]; then
    ok "$label is a valid $EXPECT_MACHINE Windows image"
  else
    fail "$label failed PE inspection"
    printf '%s\n' "$out" | grep -E '^pe_info: FAIL' | while IFS= read -r l; do
      annotate error "$label: ${l#pe_info: FAIL }"
    done
  fi
}

echo "verify: $DIST"

# ------------------------------------------------------------------ windows --
if [[ $WANT_WIN -eq 1 ]]; then
  STAGE="$DIST/windows/glass-widget"
  if [[ -d "$STAGE" ]]; then
    echo "windows drop:"
    check "glass-widget.exe present"        "$STAGE/glass-widget.exe"
    check "offsets_fallback.json shipped"   "$STAGE/offsets_fallback.json"
    check "README.txt shipped"              "$STAGE/README.txt"
    check "build-info.txt written"          "$DIST/windows/build-info.txt"

    if [[ -f "$STAGE/glass-widget.exe" ]]; then
      inspect_pe "glass-widget.exe" "$STAGE/glass-widget.exe" \
        --expect-machine "$EXPECT_MACHINE" \
        --expect-subsystem gui \
        --min-size 65536 \
        --expect-import d3d11.dll \
        --expect-import dwmapi.dll \
        --expect-import winhttp.dll \
        --expect-import kernel32.dll
    fi

    # A "portable" drop that needs the Visual C++ redistributable is not
    # portable: on a clean machine the loader kills it with 0xC0000135 before
    # a line of our code runs.
    if [[ -f "$STAGE/glass-widget.exe" ]]; then
      refs="$(bash "$PE_INFO" "$STAGE/glass-widget.exe" 2>/dev/null | sed -n 's/^dll_refs=//p')"
      if printf '%s' "$refs" | grep -qiE 'vcruntime140|msvcp140'; then
        warn "exe imports the dynamic CRT (vcruntime140/msvcp140): target machines need the VC++ redistributable installed. <RuntimeLibrary>MultiThreaded</RuntimeLibrary> in the Release configs would make the drop self-contained."
      else
        ok "exe does not depend on the VC++ redistributable"
      fi
    fi

    if [[ -f "$STAGE/offsets_fallback.json" ]]; then
      if [[ -z "$PY_BIN" ]]; then
        skip "offsets_fallback.json JSON parse (no python on PATH)"
      elif "$PY_BIN" -c "import json,sys; json.load(open(sys.argv[1]))" "$STAGE/offsets_fallback.json" 2>/dev/null; then
        ok "offsets_fallback.json parses"
      else
        fail "offsets_fallback.json is not valid JSON"
      fi
      if cmp -s "$STAGE/offsets_fallback.json" "$REPO_ROOT/roblox_external/offsets_fallback.json"; then
        ok "offsets_fallback.json matches the repo copy"
      else
        fail "offsets_fallback.json differs from roblox_external/offsets_fallback.json"
      fi
    fi

    shopt -s nullglob
    zips=("$DIST/windows/"*.zip)
    shopt -u nullglob
    if [[ ${#zips[@]} -eq 0 ]]; then
      fail "no portable zip in dist/windows"
    else
      for z in "${zips[@]}"; do
        if [[ "$(head -c2 "$z")" == "PK" ]]; then ok "$(basename "$z") is a zip archive"
        else fail "$(basename "$z") is not a zip archive"; fi
      done
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

    if [[ -f "$APPDIR/usr/bin/glass-widget.exe" ]]; then
      ok "payload present in usr/bin (Wine)"
      # The exe crossed a job boundary to get here; prove it survived intact.
      inspect_pe "AppDir payload" "$APPDIR/usr/bin/glass-widget.exe" \
        --expect-machine "$EXPECT_MACHINE" --expect-subsystem gui --min-size 65536
    elif [[ -x "$APPDIR/usr/bin/glass-widget" ]]; then
      ok "payload present in usr/bin (native)"
    else
      fail "no payload in $APPDIR/usr/bin"
    fi

    if command -v desktop-file-validate >/dev/null 2>&1; then
      if desktop-file-validate "$APPDIR/usr/share/applications/glass-widget.desktop" >/dev/null 2>&1; then
        ok "desktop entry validates"
      else
        fail "desktop-file-validate rejected the .desktop"
        desktop-file-validate "$APPDIR/usr/share/applications/glass-widget.desktop" 2>&1 | sed 's/^/        /'
      fi
    else
      skip "desktop-file-validate not installed"
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
      base="$(basename "$img")"
      if [[ -x "$img" ]]; then ok "$base is executable"
      else fail "$base is not executable"; fi
      if [[ "$(head -c4 "$img" | tail -c3)" == "ELF" ]]; then ok "$base is an ELF image"
      else fail "$base has no ELF header"; fi
      sz=$(wc -c < "$img" | tr -d ' ')
      if [[ "$sz" -gt 131072 ]]; then ok "$base size sane ($sz bytes)"
      else fail "$base suspiciously small ($sz bytes)"; fi
      # byte 8 of an AppImage is 'A' 'I' 0x02 - the type-2 magic
      if [[ "$(od -An -tx1 -j8 -N3 -- "$img" | tr -d ' \n')" == "414902" ]]; then
        ok "$base carries the type-2 AppImage magic"
      else
        fail "$base is missing the AppImage magic bytes (41 49 02 at offset 8)"
      fi
    done
  elif [[ $WANT_WIN -eq 0 && -d "$APPDIR" ]]; then
    skip "no .AppImage in dist/linux (AppDir-only build)"
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
