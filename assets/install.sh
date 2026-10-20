#!/bin/sh
# install Daemon as a proper desktop app (no custom icon — placeholder)
# Two binaries (docs/BOOTSTRAPPER.md): daemon-boot is the manager the
# desktop launches; daemon is the overlay it spawns on Load. Both must
# sit in the same directory — the bootstrapper resolves its overlay as a sibling.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/daemon"
if [ ! -x "$BIN" ]; then BIN="$ROOT/daemon"; fi
if [ ! -x "$BIN" ]; then echo "build/daemon not found — run setup.sh first"; exit 1; fi
BOOT="$ROOT/build/daemon-boot"
if [ ! -x "$BOOT" ]; then BOOT="$ROOT/daemon-boot"; fi
if [ ! -x "$BOOT" ]; then echo "build/daemon-boot not found — run setup.sh first"; exit 1; fi
DST_BIN="$HOME/.local/bin/daemon"
DST_BOOT="$HOME/.local/bin/daemon-boot"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications"
cp -f "$BIN" "$DST_BIN"
cp -f "$BOOT" "$DST_BOOT"
chmod +x "$DST_BIN" "$DST_BOOT"
# desktop entry uses Icon=application-x-executable (placeholder, no bundled icon)
cp -f "$ROOT/assets/daemon.desktop" "$HOME/.local/share/applications/daemon.desktop"
# Ensure Exec points at the bootstrapper (the manager), not the raw overlay
sed -i "s|^Exec=.*|Exec=$DST_BOOT|" "$HOME/.local/share/applications/daemon.desktop" 2>/dev/null || true
# Update caches if tools exist
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
echo "installed $DST_BOOT (manager) + $DST_BIN (overlay)"
echo "installed $HOME/.local/share/applications/daemon.desktop (StartupWMClass=daemon, Icon=placeholder)"
echo "XFCE: appears in Whisker Menu > Game. Ulauncher: type 'daemon'."
echo "Run: daemon-boot  or  $DST_BOOT   (click Load once Sober is up)"