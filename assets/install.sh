#!/bin/sh
# install Daemon as a proper desktop app with real icons
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

# Install binaries
cp -f "$BIN" "$DST_BIN"
cp -f "$BOOT" "$DST_BOOT"
chmod +x "$DST_BIN" "$DST_BOOT"

# Install icons to hicolor theme (Daemon2.png is the square icon, 2048x2048)
for size in 16 24 32 48 64 128 256 512; do
    dir="$HOME/.local/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$dir"
    # Use ImageMagick convert if available, otherwise just copy the original
    if command -v convert >/dev/null 2>&1; then
        convert "$ROOT/assets/Daemon2.png" -resize "${size}x${size}" "$dir/daemon.png" 2>/dev/null || cp "$ROOT/assets/Daemon2.png" "$dir/daemon.png"
    else
        cp "$ROOT/assets/Daemon2.png" "$dir/daemon.png"
    fi
done
# Also install scalable SVG if we had one, but PNG at 256/512 is fine for most DEs

# Install desktop entry with proper icon reference
cp -f "$ROOT/assets/daemon.desktop" "$HOME/.local/share/applications/daemon.desktop"
# Update Exec to point at the bootstrapper (the manager), not the raw overlay
sed -i "s|^Exec=.*|Exec=$DST_BOOT|" "$HOME/.local/share/applications/daemon.desktop" 2>/dev/null || true
# Update Icon to use our installed icon name
sed -i "s|^Icon=.*|Icon=daemon|" "$HOME/.local/share/applications/daemon.desktop" 2>/dev/null || true

# Update desktop database and icon cache
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

echo "installed $DST_BOOT (manager) + $DST_BIN (overlay)"
echo "installed $HOME/.local/share/applications/daemon.desktop (StartupWMClass=daemon, Icon=daemon)"
echo "installed icons to ~/.local/share/icons/hicolor/*/apps/daemon.png"
echo "XFCE: appears in Whisker Menu > Game. Ulauncher: type 'daemon'."
echo "Run: daemon-boot  or  $DST_BOOT   (click Load once Sober is up)"
