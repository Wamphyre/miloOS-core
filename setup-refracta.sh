#!/bin/bash
# Setup Refracta Tools from SourceForge
# Simple installation script - does NOT modify system configuration

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Check root
if [ "$(id -u)" -ne 0 ]; then
    log_error "Must run as root"
    exit 1
fi

log_info "========================================="
log_info "Refracta Tools Installer"
log_info "Installing from SourceForge"
log_info "========================================="
echo ""

# Check if already installed
if command -v refractasnapshot &> /dev/null; then
    log_warn "refractasnapshot is already installed"
    read -p "Do you want to reinstall? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_info "Installation cancelled"
        exit 0
    fi
fi

# Create temp directory for downloads
TEMP_DIR=$(mktemp -d)
log_info "Created temporary directory: $TEMP_DIR"
cd "$TEMP_DIR"

# Download all required packages
log_info "Downloading Refracta packages from SourceForge..."
echo ""

log_info "1/6 Downloading live-boot..."
wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/live-boot_20221008~fsr1_all.deb/download -O live-boot.deb

log_info "2/6 Downloading live-boot-initramfs-tools..."
wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/live-boot-initramfs-tools_20221008~fsr1_all.deb/download -O live-boot-initramfs-tools.deb

log_info "3/6 Downloading refractasnapshot-base..."
wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/refractasnapshot-base_10.2.12_all.deb/download -O refractasnapshot-base.deb

log_info "4/6 Downloading refractasnapshot-gui..."
wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/refractasnapshot-gui_10.2.12_all.deb/download -O refractasnapshot-gui.deb

log_info "5/6 Downloading refractainstaller-base..."
wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/refractainstaller-base_9.6.6_all.deb/download -O refractainstaller-base.deb

log_info "6/6 Downloading refractainstaller-gui..."
wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/refractainstaller-gui_9.6.6_all.deb/download -O refractainstaller-gui.deb

echo ""
log_info "All packages downloaded successfully"
echo ""

# Install dependencies first
log_info "Installing system dependencies..."
apt-get update -qq
apt-get install -y squashfs-tools xorriso isolinux syslinux-common \
    grub-pc-bin grub-efi-amd64-bin rsync genisoimage yad zenity \
    initramfs-tools

echo ""
log_info "Installing Refracta packages..."

# Install live-boot packages
log_info "Installing live-boot packages..."
dpkg -i live-boot.deb live-boot-initramfs-tools.deb || apt-get install -f -y

# Install refractasnapshot packages
log_info "Installing refractasnapshot packages..."
dpkg -i refractasnapshot-base.deb refractasnapshot-gui.deb || apt-get install -f -y

# Install refractainstaller packages
log_info "Installing refractainstaller packages..."
dpkg -i refractainstaller-base.deb refractainstaller-gui.deb || apt-get install -f -y

echo ""

# Verify installation
if command -v refractasnapshot &> /dev/null; then
    log_info "✓ refractasnapshot installed successfully"
    REFRACTA_VERSION=$(refractasnapshot --version 2>/dev/null || echo "unknown")
    log_info "  Version: $REFRACTA_VERSION"
else
    log_error "refractasnapshot installation failed"
    cd /
    rm -rf "$TEMP_DIR"
    exit 1
fi

if command -v refractainstaller &> /dev/null; then
    log_info "✓ refractainstaller installed successfully"
else
    log_warn "refractainstaller may not be installed correctly"
fi

# Cleanup
cd /
rm -rf "$TEMP_DIR"
log_info "✓ Cleaned up temporary files"

echo ""
log_info "========================================="
log_info "Installation completed successfully!"
log_info "========================================="
echo ""
log_info "Installed tools:"
log_info "  - refractasnapshot (create Live ISO)"
log_info "  - refractainstaller (system installer)"
echo ""
log_info "Usage:"
log_info "  sudo refractasnapshot        # GUI mode"
log_info "  sudo refractasnapshot -h     # Help"
echo ""
