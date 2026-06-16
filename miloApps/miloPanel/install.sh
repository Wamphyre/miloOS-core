#!/bin/bash
# Install miloPanel

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

echo "Installing miloPanel..."

REQUIRED_PACKAGES=(
    build-essential
    pkg-config
    libgtk-3-dev
    libx11-dev
    appmenu-gtk3-module
    vala-panel-appmenu
    xfce4-notifyd
    pulseaudio-utils
    pavucontrol
    desktop-file-utils
)

MISSING_PACKAGES=()
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        MISSING_PACKAGES+=("$pkg")
    fi
done

if [ "${#MISSING_PACKAGES[@]}" -gt 0 ]; then
    echo "Installing miloPanel dependencies: ${MISSING_PACKAGES[*]}"
    apt-get update
    apt-get install -y "${MISSING_PACKAGES[@]}"
fi

echo "Building native C++ panel..."
make -C src clean
make -C src

echo "Installing application..."
install -m 755 src/milopanel-bin /usr/local/bin/milopanel

echo "Installing desktop entry..."
install -m 644 milopanel.desktop /usr/share/applications/milopanel.desktop

if [ -f "settings.ini" ]; then
    echo "Installing default settings..."
    mkdir -p /etc/xdg/miloPanel
    install -m 644 settings.ini /etc/xdg/miloPanel/settings.ini
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications 2>/dev/null || true
fi

echo "Installation complete."
echo "Run 'milopanel --replace' to start the native miloOS panel."
