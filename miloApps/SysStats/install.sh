#!/bin/bash
# Install miloOS System Statistics Monitor

set -e

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

echo "Installing miloOS System Statistics Monitor..."

# Install Python dependencies
echo "Installing dependencies..."
apt-get install -y python3-psutil 2>/dev/null || echo "psutil already installed or not available"

# Install Python script
install -m 755 sysstats.py /usr/local/bin/sysstats

# Install icon
if [ -f "sysstats.svg" ]; then
    echo "Installing icon..."
    mkdir -p /usr/share/icons/hicolor/scalable/apps
    install -m 644 sysstats.svg /usr/share/icons/hicolor/scalable/apps/sysstats.svg
    
    # Update icon cache
    if command -v gtk-update-icon-cache &> /dev/null; then
        gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
    fi
fi

# Install desktop entry
install -m 644 sysstats.desktop /usr/share/applications/

# Install PolicyKit policy for hardware access
if [ -f "org.milos.sysstats.policy" ]; then
    echo "Installing PolicyKit policy..."
    mkdir -p /usr/share/polkit-1/actions
    install -m 644 org.milos.sysstats.policy /usr/share/polkit-1/actions/
fi

# Update desktop database
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database /usr/share/applications/
fi

echo "Installation complete!"
echo "You can now run 'sysstats' or find it in your applications menu."
