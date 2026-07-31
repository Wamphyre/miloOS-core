#!/bin/bash
# Install miloDock

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

echo "Installing miloDock..."

REQUIRED_PACKAGES=(
    build-essential
    pkg-config
    libgtk-3-dev
    libx11-dev
    desktop-file-utils
    gtk-update-icon-cache
    hicolor-icon-theme
)

MISSING_PACKAGES=()
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        MISSING_PACKAGES+=("$pkg")
    fi
done

if [ "${#MISSING_PACKAGES[@]}" -gt 0 ]; then
    echo "Installing miloDock dependencies: ${MISSING_PACKAGES[*]}"
    apt-get update
    apt-get install -y "${MISSING_PACKAGES[@]}"
fi

echo "Building native C++ dock..."
make -C src clean
make -C src

echo "Installing application..."
install -m 755 src/milodock-bin /usr/local/bin/milodock

echo "Installing desktop entry..."
install -m 644 milodock.desktop /usr/share/applications/milodock.desktop

if [ -n "$SUDO_USER" ]; then
    USER_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
    LEGACY_DESKTOP="$USER_HOME/.local/share/applications/milodock.desktop"
    LEGACY_BINARY="$USER_HOME/.local/bin/milodock"
    if [ -f "$LEGACY_DESKTOP" ] && grep -Eq '^Exec=.*/\.local/bin/milodock([[:space:]]|$)' "$LEGACY_DESKTOP"; then
        rm -f "$LEGACY_DESKTOP"
    fi
    if [ -L "$LEGACY_BINARY" ]; then
        LEGACY_TARGET="$(readlink "$LEGACY_BINARY")"
        case "$LEGACY_TARGET" in
            */miloApps/miloDock/src/milodock-bin)
                rm -f "$LEGACY_BINARY"
                ;;
        esac
    fi
fi

if [ -f "milodock.svg" ]; then
    echo "Installing icon..."
    mkdir -p /usr/share/icons/hicolor/scalable/apps
    install -m 644 milodock.svg /usr/share/icons/hicolor/scalable/apps/milodock.svg
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
    fi
fi

if [ -d "launchers" ]; then
    echo "Installing default launchers..."
    mkdir -p /etc/xdg/miloDock/launchers
    install -m 644 launchers/*.dockitem /etc/xdg/miloDock/launchers/ 2>/dev/null || true
fi

if [ -f "settings.ini" ]; then
    echo "Installing default settings..."
    mkdir -p /etc/xdg/miloDock
    install -m 644 settings.ini /etc/xdg/miloDock/settings.ini
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database /usr/share/applications 2>/dev/null || true
    if [ -n "$SUDO_USER" ] && [ -d "$USER_HOME/.local/share/applications" ]; then
        sudo -u "$SUDO_USER" update-desktop-database "$USER_HOME/.local/share/applications" 2>/dev/null || true
    fi
fi

echo "Installation complete."
echo "Run 'milodock --replace' to start the dock."
