#!/bin/bash
# Install miloOS Audio Master

set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

# Get the directory where the script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "Installing miloOS Audio Master..."

# Install pip if not present
if ! command -v pip3 &> /dev/null; then
    echo "Installing pip3..."
    apt-get update
    apt-get install -y python3-pip
fi

# Install matchering
echo "Installing matchering library..."
pip3 install matchering --break-system-packages 2>/dev/null || pip3 install matchering

# Install Python script
install -m 755 audiomaster.py /usr/local/bin/audiomaster

# Install icon
if [ -f "audiomaster.svg" ]; then
    echo "Installing icon..."
    mkdir -p /usr/share/icons/hicolor/scalable/apps
    install -m 644 audiomaster.svg /usr/share/icons/hicolor/scalable/apps/audiomaster.svg
    
    # Update icon cache
    if command -v gtk-update-icon-cache &> /dev/null; then
        gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
    fi
fi

# Install desktop entry
install -m 644 audiomaster.desktop /usr/share/applications/

# Update desktop database
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database /usr/share/applications/
fi

echo "Installation complete!"
echo "You can now run 'audiomaster' or find it in your applications menu."
