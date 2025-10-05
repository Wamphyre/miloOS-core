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

# Install desktop entry
install -m 644 audio-config.desktop /usr/share/applications/

# Update desktop database
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database /usr/share/applications/
fi

echo "Installation complete!"
echo "You can now run 'audio-config' or find it in your applications menu."
