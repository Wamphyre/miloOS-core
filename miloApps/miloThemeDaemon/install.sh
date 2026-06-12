#!/usr/bin/env mode
# Author: Wamphyre
# Description: Installer for miloOS theme daemon
# Version: 1.0

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if run with sudo/root
if [ "$(id -u)" -ne 0 ]; then
    log_error "This install script must be run as root (use sudo)"
    exit 1
fi

# Determine real user running the install
REAL_USER="${SUDO_USER:-$USER}"
REAL_HOME=$(getent passwd "$REAL_USER" | cut -d: -f6)

log_info "Installing miloOS Theme Daemon..."

# 1. Install executable to /usr/local/bin/
install -m 755 milo-theme-daemon.py /usr/local/bin/milo-theme-daemon
log_info "✓ Installed daemon to /usr/local/bin/milo-theme-daemon"

# 2. Configure autostart for all future users (/etc/skel)
mkdir -p /etc/skel/.config/autostart
install -m 644 milo-theme-daemon.desktop /etc/skel/.config/autostart/milo-theme-daemon.desktop
log_info "✓ Configured autostart template in /etc/skel"

# 3. Configure autostart for current user
if [ -d "$REAL_HOME" ]; then
    mkdir -p "$REAL_HOME/.config/autostart"
    install -m 644 -o "$REAL_USER" -g "$REAL_USER" milo-theme-daemon.desktop "$REAL_HOME/.config/autostart/milo-theme-daemon.desktop"
    log_info "✓ Configured autostart for user: $REAL_USER"
fi

# 4. Kill any running instances
pkill -f milo-theme-daemon || true

# 5. Start daemon in background as the real user
if [ -n "$REAL_USER" ]; then
    log_info "Starting daemon in the background for $REAL_USER..."
    sudo -u "$REAL_USER" nohup python3 /usr/local/bin/milo-theme-daemon > /dev/null 2>&1 &
    log_info "✓ Theme daemon successfully started!"
fi
