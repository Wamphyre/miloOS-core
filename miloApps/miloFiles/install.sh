#!/bin/bash
# Install miloFiles File Manager

set -e

# Get the directory where the script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

echo "Installing miloFiles File Manager..."

# Install Python script
echo "Installing application binary..."
install -m 755 milofiles.py /usr/local/bin/milofiles

# Install icon
if [ -f "milofiles.svg" ]; then
    echo "Installing application icon..."
    mkdir -p /usr/share/icons/hicolor/scalable/apps
    install -m 644 milofiles.svg /usr/share/icons/hicolor/scalable/apps/milofiles.svg
    
    # Update icon cache
    if command -v gtk-update-icon-cache &> /dev/null; then
        gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
    fi
fi

# Install desktop entry
echo "Installing desktop entry..."
install -m 644 milofiles.desktop /usr/share/applications/

# Update desktop database
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database /usr/share/applications/
fi

# Set as default file manager for the sudo user if applicable
if [ -n "$SUDO_USER" ]; then
    echo "Setting miloFiles as default directory handler for user $SUDO_USER..."
    sudo -u "$SUDO_USER" xdg-mime default milofiles.desktop inode/directory || true
fi

# Set system-wide default
if [ -f "/usr/share/applications/defaults.list" ]; then
    echo "Setting miloFiles as system-wide default directory handler..."
    if grep -q "inode/directory" /usr/share/applications/defaults.list; then
        sed -i 's|inode/directory=.*|inode/directory=milofiles.desktop|' /usr/share/applications/defaults.list
    else
        echo "inode/directory=milofiles.desktop" >> /usr/share/applications/defaults.list
    fi
fi

echo "Installation complete!"
echo "You can now run 'milofiles' or find it in your applications menu."
