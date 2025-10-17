#!/bin/bash
# miloUpdater installation script

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "[INFO] Installing miloUpdater..."

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "[ERROR] This script must be run as root (use sudo)"
    exit 1
fi

# Install Python dependencies
echo "[INFO] Checking Python dependencies..."
if ! python3 -c "import gi; gi.require_version('Vte', '2.91')" 2>/dev/null; then
    echo "[INFO] Installing python3-vte..."
    apt-get install -y python3-gi gir1.2-vte-2.91 2>/dev/null || {
        echo "[WARN] Could not install VTE, trying alternative..."
        apt-get install -y python3-gi gir1.2-vte-2.90 2>/dev/null || true
    }
fi

# Install the Python script
echo "[INFO] Installing miloupdate script..."
cp "$SCRIPT_DIR/miloupdate.py" /usr/bin/miloupdate
chmod 755 /usr/bin/miloupdate
chown root:root /usr/bin/miloupdate

# Install icon
if [ -f "$SCRIPT_DIR/miloupdate.svg" ]; then
    echo "[INFO] Installing icon..."
    mkdir -p /usr/share/icons/hicolor/scalable/apps
    install -m 644 "$SCRIPT_DIR/miloupdate.svg" /usr/share/icons/hicolor/scalable/apps/miloupdate.svg
    
    # Update icon cache
    if command -v gtk-update-icon-cache &> /dev/null; then
        gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
    fi
fi

# Install desktop entry
echo "[INFO] Installing desktop entry..."
cp "$SCRIPT_DIR/miloupdate.desktop" /usr/share/applications/
chmod 644 /usr/share/applications/miloupdate.desktop
chown root:root /usr/share/applications/miloupdate.desktop

# Install PolicyKit policy
echo "[INFO] Installing PolicyKit policy..."
cp "$SCRIPT_DIR/org.milos.updater.policy" /usr/share/polkit-1/actions/
chmod 644 /usr/share/polkit-1/actions/org.milos.updater.policy
chown root:root /usr/share/polkit-1/actions/org.milos.updater.policy

# Update desktop database
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database /usr/share/applications 2>/dev/null || true
fi

echo "[INFO] miloUpdater installed successfully!"
echo "[INFO] You can run it from the applications menu or by typing 'miloupdate'"

exit 0
