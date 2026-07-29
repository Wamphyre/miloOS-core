#!/bin/bash
# Install miloPKG

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

echo "Installing miloPKG..."

REQUIRED_PACKAGES=(
    apt
    ca-certificates
    dpkg
    python3
    python3-gi
    gir1.2-gtk-3.0
    squashfs-tools
    desktop-file-utils
    gtk-update-icon-cache
    hicolor-icon-theme
    xdg-utils
)

MISSING_PACKAGES=()
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        MISSING_PACKAGES+=("$pkg")
    fi
done

if [ "${#MISSING_PACKAGES[@]}" -gt 0 ]; then
    echo "Installing miloPKG dependencies: ${MISSING_PACKAGES[*]}"
    apt-get update
    apt-get install -y "${MISSING_PACKAGES[@]}"
fi

install -m 755 milopkg.py /usr/local/bin/milopkg
install -m 644 milopkg.desktop /usr/share/applications/milopkg.desktop

mkdir -p /usr/share/icons/hicolor/scalable/apps
install -m 644 milopkg.svg /usr/share/icons/hicolor/scalable/apps/milopkg.svg

if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate /usr/share/applications/milopkg.desktop
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications 2>/dev/null || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
fi

echo "Installation complete."
echo "Run 'milopkg' or open miloPKG from the applications menu."
