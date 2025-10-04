#!/bin/bash
# Author: Wamphyre
# Description: Restore original Debian branding
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

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Ensure execution as root
if [ "$EUID" -ne 0 ]; then
    log_error "This script must be run as root"
    exit 1
fi

# Find the most recent backup
BACKUP_DIR=$(ls -td /root/debian-backup-* 2>/dev/null | head -1)

if [ -z "$BACKUP_DIR" ]; then
    log_error "No backup directory found!"
    log_error "Cannot restore original Debian branding"
    exit 1
fi

log_info "Found backup at: $BACKUP_DIR"
log_warn "This will restore the original Debian branding"
read -p "Do you want to continue? (y/N): " -n 1 -r
echo

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    log_info "Restoration cancelled"
    exit 0
fi

log_info "Restoring Debian branding..."

# Restore files from backup
[ -f "$BACKUP_DIR/os-release.bak" ] && cp "$BACKUP_DIR/os-release.bak" /etc/os-release && log_info "Restored /etc/os-release"
[ -f "$BACKUP_DIR/issue.bak" ] && cp "$BACKUP_DIR/issue.bak" /etc/issue && log_info "Restored /etc/issue"
[ -f "$BACKUP_DIR/issue.net.bak" ] && cp "$BACKUP_DIR/issue.net.bak" /etc/issue.net && log_info "Restored /etc/issue.net"
[ -f "$BACKUP_DIR/lsb-release.bak" ] && cp "$BACKUP_DIR/lsb-release.bak" /etc/lsb-release && log_info "Restored /etc/lsb-release"
[ -f "$BACKUP_DIR/debian_version.bak" ] && cp "$BACKUP_DIR/debian_version.bak" /etc/debian_version && log_info "Restored /etc/debian_version"
[ -f "$BACKUP_DIR/grub.bak" ] && cp "$BACKUP_DIR/grub.bak" /etc/default/grub && log_info "Restored /etc/default/grub"
[ -f "$BACKUP_DIR/hostname.bak" ] && cp "$BACKUP_DIR/hostname.bak" /etc/hostname && log_info "Restored /etc/hostname"
[ -f "$BACKUP_DIR/lightdm-gtk-greeter.conf.bak" ] && cp "$BACKUP_DIR/lightdm-gtk-greeter.conf.bak" /etc/lightdm/lightdm-gtk-greeter.conf && log_info "Restored LightDM config"

# Remove custom MOTD
if [ -f "/etc/update-motd.d/00-header" ]; then
    rm -f /etc/update-motd.d/00-header
    log_info "Removed custom MOTD"
fi

# Update GRUB if needed
if [ -f "$BACKUP_DIR/grub.bak" ] && command -v update-grub &> /dev/null; then
    log_info "Updating GRUB..."
    update-grub 2>/dev/null || log_warn "Failed to update GRUB"
fi

log_info "Debian branding restored successfully!"
log_warn "Please reboot your system for all changes to take effect"
