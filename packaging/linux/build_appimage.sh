#!/usr/bin/env bash
# -----------------------------------------------------------------------------
#  packaging/linux/build_appimage.sh
#
#  Packages glass-widget as a self-contained Linux AppImage.
#
#  There is no native Linux binary yet (overlay.cpp / menu.cpp / platform_win.cpp
#  are still Win32-only - see README "Platform rule"), so the AppImage ships the
#  Windows build and launches it through Wine, which is how Roblox itself runs on
#  Linux. The AppRun keeps its own 64-bit WINEPREFIX under
#  $XDG_DATA_HOME/glass-widget/wineprefix so it never touches ~/.wine.
#
#  When a native ELF exists, pass it with --native and the same AppDir layout is
#  reused with a direct exec - no other change needed.
#
#  Usage:
#     packaging/linux/build_appimage.sh                      # auto-find the exe
#     packaging/linux/build_appimage.sh --exe path/to.exe
#     packaging/linux/build_appimage.sh --native path/to/elf
#     packaging/linux/build_appimage.sh --appdir-only        # skip appimagetool
#     packaging/linux/build_appimage.sh --version 2026.10.03
# -----------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
ASSETS="$REPO_ROOT/packaging/assets"
DIST="$REPO_ROOT/dist/linux"
APPDIR="$DIST/glass-widget.AppDir"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/glass-widget-packaging"
APP_NAME="glass-widget"

EXE=""
NATIVE=""
VERSION=""
APPDIR_ONLY=0
ARCH="${ARCH:-$(uname -m)}"

info() { printf '\033[36m[appimage]\033[0m %s\n' "$*" >&2; }
warn() { printf '\033[33m[appimage]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[31m[appimage] ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --exe)         EXE="${2:?}"; shift 2 ;;
    --native)      NATIVE="${2:?}"; shift 2 ;;
    --version)     VERSION="${2:?}"; shift 2 ;;
    --appdir-only) APPDIR_ONLY=1; shift ;;
    -h|--help)     sed -n '2,25p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *)             die "unknown argument: $1" ;;
  esac
done

# ------------------------------------------------------------------ payload --
if [[ -z "$NATIVE" && -z "$EXE" ]]; then
  for cand in \
    "$REPO_ROOT/dist/windows/glass-widget/glass-widget.exe" \
    "$REPO_ROOT/x64/Release/glass-widget.exe" \
    "$REPO_ROOT/x64/Debug/glass-widget.exe" \
    "$REPO_ROOT/x86/Release/glass-widget.exe"
  do
    [[ -f "$cand" ]] && { EXE="$cand"; break; }
  done
fi

if [[ -n "$NATIVE" ]]; then
  [[ -f "$NATIVE" ]] || die "native binary not found: $NATIVE"
  info "payload: native ELF $NATIVE"
elif [[ -n "$EXE" ]]; then
  [[ -f "$EXE" ]] || die "exe not found: $EXE"
  # The exe usually arrives as a CI artifact from another job. A truncated or
  # wrong-file download should fail here, not three steps later inside a
  # squashfs image that looks perfectly well-formed.
  if [[ "$(head -c2 -- "$EXE")" != "MZ" ]]; then
    die "payload is not a Windows executable (no MZ header): $EXE"
  fi
  if [[ -f "$REPO_ROOT/packaging/pe_info.sh" ]]; then
    bash "$REPO_ROOT/packaging/pe_info.sh" "$EXE" --expect-machine x64 --expect-subsystem gui \
      --min-size 65536 >/dev/null || die "payload failed PE inspection: $EXE"
  fi
  info "payload: Windows exe $EXE (Wine launcher)"
else
  die "no payload found. Build on Windows first (packaging/windows/build_exe.ps1),
       copy the exe over, then re-run with --exe <path>, or pass --native <elf>."
fi

if [[ -z "$VERSION" ]]; then
  VERSION="$("$REPO_ROOT/packaging/version.sh")"
fi
info "version: $VERSION"

# ------------------------------------------------------------------- AppDir --
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
         "$APPDIR/usr/share/glass-widget"

if [[ -n "$NATIVE" ]]; then
  install -m 0755 "$NATIVE" "$APPDIR/usr/bin/glass-widget"
  PAYLOAD_KIND=native
else
  install -m 0644 "$EXE" "$APPDIR/usr/bin/glass-widget.exe"
  PAYLOAD_KIND=wine
fi

install -m 0644 "$REPO_ROOT/roblox_external/offsets_fallback.json" \
                "$APPDIR/usr/share/glass-widget/offsets_fallback.json"
install -m 0644 "$ASSETS/$APP_NAME.png" \
                "$APPDIR/usr/share/icons/hicolor/256x256/apps/$APP_NAME.png"
cp "$ASSETS/$APP_NAME.png" "$APPDIR/$APP_NAME.png"

DESKTOP="$APPDIR/usr/share/applications/$APP_NAME.desktop"
sed -e "s/@VERSION@/$VERSION/g" "$SCRIPT_DIR/glass-widget.desktop.in" > "$DESKTOP"
ln -sf "usr/share/applications/$APP_NAME.desktop" "$APPDIR/$APP_NAME.desktop"

sed -e "s/@PAYLOAD_KIND@/$PAYLOAD_KIND/g" -e "s/@VERSION@/$VERSION/g" \
    "$SCRIPT_DIR/AppRun.in" > "$APPDIR/AppRun"
chmod 0755 "$APPDIR/AppRun"

info "AppDir staged -> $APPDIR"
[[ $APPDIR_ONLY -eq 1 ]] && { info "--appdir-only: stopping here"; exit 0; }

# ------------------------------------------------------------ appimagetool --
find_appimagetool() {
  if [[ -n "${APPIMAGETOOL:-}" ]]; then
    [[ -x "$APPIMAGETOOL" ]] || { warn "APPIMAGETOOL=$APPIMAGETOOL is not executable"; return 1; }
    echo "$APPIMAGETOOL"; return
  fi
  if command -v appimagetool >/dev/null 2>&1; then command -v appimagetool; return; fi
  local local_tool="$CACHE/appimagetool-$ARCH.AppImage"
  if [[ -x "$local_tool" ]]; then echo "$local_tool"; return; fi

  mkdir -p "$CACHE"
  local url="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage"
  info "downloading appimagetool ($ARCH)"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" -o "$local_tool" || { rm -f "$local_tool"; return 1; }
  elif command -v wget >/dev/null 2>&1; then
    wget -q "$url" -O "$local_tool" || { rm -f "$local_tool"; return 1; }
  else
    return 1
  fi
  chmod +x "$local_tool"
  [[ -s "$local_tool" ]] || { rm -f "$local_tool"; return 1; }
  echo "$local_tool"
}

TOOL="$(find_appimagetool || true)"
if [[ -z "$TOOL" ]]; then
  warn "appimagetool unavailable (no copy installed and the download failed)."
  warn "The AppDir is complete: run it with \"$APPDIR/AppRun\", or install"
  warn "appimagetool and re-run to produce the single-file AppImage."
  exit 2
fi

OUT="$DIST/$APP_NAME-$VERSION-$ARCH.AppImage"
rm -f "$OUT"
info "appimagetool: $TOOL"

# --appimage-extract-and-run keeps this working on machines without FUSE
# (containers, CI runners).
if ! ARCH="$ARCH" "$TOOL" --appimage-extract-and-run "$APPDIR" "$OUT" 2>"$CACHE/appimagetool.log"; then
  if ! ARCH="$ARCH" "$TOOL" "$APPDIR" "$OUT" 2>>"$CACHE/appimagetool.log"; then
    tail -n 30 "$CACHE/appimagetool.log" >&2 || true
    die "appimagetool failed (log: $CACHE/appimagetool.log)"
  fi
fi

chmod +x "$OUT"
info "done -> $OUT ($(du -h "$OUT" | cut -f1))"
