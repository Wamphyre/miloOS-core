#!/bin/bash
# Install miloOS Audio Configuration Tool

set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

echo "Installing miloOS Audio Configuration Tool..."

# Install Python script
install -m 755 audio-config.py /usr/local/bin/audio-config

# Install icon
if [ -f "audio-config.svg" ]; then
    echo "Installing icon..."
    mkdir -p /usr/share/icons/hicolor/scalable/apps
    install -m 644 audio-config.svg /usr/share/icons/hicolor/scalable/apps/audio-config.svg
    
    # Update icon cache
    if command -v gtk-update-icon-cache &> /dev/null; then
        gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
    fi
fi

# Install desktop entry
install -m 644 audio-config.desktop /usr/share/applications/

# Update desktop database
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database /usr/share/applications/
fi

echo "Installation complete!"
echo "You can now run 'audio-config' or find it in your applications menu."
