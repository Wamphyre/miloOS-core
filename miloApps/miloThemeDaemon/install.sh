#!/bin/bash
# Author: Wamphyre
# Description: Installer for miloOS theme daemon
# Version: 1.0

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

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

# 2. The daemon is launched by the miloOS XFCE session client list.
rm -f /etc/skel/.config/autostart/milo-theme-daemon.desktop
log_info "✓ Autostart template disabled; XFCE session starts the daemon"

# 3. Remove legacy per-user autostart entry
if [ -d "$REAL_HOME" ]; then
    rm -f "$REAL_HOME/.config/autostart/milo-theme-daemon.desktop"
    log_info "✓ Removed legacy autostart for user: $REAL_USER"
fi

# 4. Kill any running instances
pkill -f milo-theme-daemon || true

# 5. Start daemon in background as the real user
if [ -n "$REAL_USER" ]; then
    log_info "Starting daemon in the background for $REAL_USER..."
    sudo -u "$REAL_USER" nohup python3 /usr/local/bin/milo-theme-daemon > /dev/null 2>&1 &
    log_info "✓ Theme daemon successfully started!"
fi
