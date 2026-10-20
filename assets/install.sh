#!/bin/sh
# install Roblox External as a proper desktop app (no custom icon — placeholder)
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/external"
if [ ! -x "$BIN" ]; then BIN="$ROOT/external"; fi
if [ ! -x "$BIN" ]; then echo "build/external not found — run setup.sh first"; exit 1; fi
DST_BIN="$HOME/.local/bin/roblox-external"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications"
cp -f "$BIN" "$DST_BIN"
chmod +x "$DST_BIN"
# desktop entry uses Icon=application-x-executable (placeholder, no bundled icon)
cp -f "$ROOT/assets/roblox-external.desktop" "$HOME/.local/share/applications/roblox-external.desktop"
# Ensure Exec points to installed bin
sed -i "s|^Exec=.*|Exec=$DST_BIN|" "$HOME/.local/share/applications/roblox-external.desktop" 2>/dev/null || true
# Update caches if tools exist
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
echo "installed $DST_BIN"
echo "installed $HOME/.local/share/applications/roblox-external.desktop (StartupWMClass=roblox-external, Icon=placeholder)"
echo "XFCE: appears in Whisker Menu > Game. Ulauncher: type 'roblox'."
echo "Run: roblox-external  or  $DST_BIN"
