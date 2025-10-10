#!/bin/bash
# miloOS ISO Builder using refractasnapshot
# Creates a bootable Live ISO from current system
# Version 4.0

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Configuration
ISO_NAME="miloOS-1.0-amd64.iso"
LIVE_USER="milo"
LIVE_PASSWORD="1234"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check root
if [ "$(id -u)" -ne 0 ]; then
    log_error "Must run as root"
    exit 1
fi

log_info "========================================="
log_info "miloOS ISO Builder v4.0"
log_info "Using refractasnapshot"
log_info "========================================="
echo ""

# Check disk space
AVAILABLE_GB=$(df -BG /home | awk 'NR==2 {print $4}' | sed 's/G//')
if [ "$AVAILABLE_GB" -lt 20 ]; then
    log_error "Insufficient disk space in /home. Need at least 20GB, have ${AVAILABLE_GB}GB"
    exit 1
fi
log_info "Available disk space: ${AVAILABLE_GB}GB"
echo ""

# Step 1: Install refractasnapshot and refractainstaller from SourceForge
log_info "Step 1: Installing Refracta tools from SourceForge..."

if ! command -v refractasnapshot &> /dev/null; then
    log_info "Downloading Refracta packages..."
    
    # Create temp directory for downloads
    TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"
    
    # Download all required packages
    log_info "Downloading live-boot..."
    wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/live-boot_20221008~fsr1_all.deb/download -O live-boot.deb
    
    log_info "Downloading live-boot-initramfs-tools..."
    wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/live-boot-initramfs-tools_20221008~fsr1_all.deb/download -O live-boot-initramfs-tools.deb
    
    log_info "Downloading refractasnapshot-base..."
    wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/refractasnapshot-base_10.2.12_all.deb/download -O refractasnapshot-base.deb
    
    log_info "Downloading refractasnapshot-gui..."
    wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/refractasnapshot-gui_10.2.12_all.deb/download -O refractasnapshot-gui.deb
    
    log_info "Downloading refractainstaller-base..."
    wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/refractainstaller-base_9.6.6_all.deb/download -O refractainstaller-base.deb
    
    log_info "Downloading refractainstaller-gui..."
    wget -q --show-progress https://sourceforge.net/projects/refracta/files/tools/refractainstaller-gui_9.6.6_all.deb/download -O refractainstaller-gui.deb
    
    # Install dependencies first
    log_info "Installing dependencies..."
    apt-get update
    apt-get install -y squashfs-tools xorriso isolinux syslinux-common grub-pc-bin grub-efi-amd64-bin \
        rsync genisoimage yad zenity
    
    # Install Refracta packages in order
    log_info "Installing Refracta packages..."
    dpkg -i live-boot.deb live-boot-initramfs-tools.deb || apt-get install -f -y
    dpkg -i refractasnapshot-base.deb refractasnapshot-gui.deb || apt-get install -f -y
    dpkg -i refractainstaller-base.deb refractainstaller-gui.deb || apt-get install -f -y
    
    # Cleanup
    cd "$SCRIPT_DIR"
    rm -rf "$TEMP_DIR"
    
    log_info "Refracta tools installed successfully"
else
    log_info "refractasnapshot already installed"
fi

# Verify installation
if ! command -v refractasnapshot &> /dev/null; then
    log_error "refractasnapshot installation failed"
    exit 1
fi

log_info "✓ refractasnapshot version: $(refractasnapshot --version 2>/dev/null || echo 'unknown')"
echo ""

# Step 2: Populate /etc/skel with user configuration
log_info "Step 2: Populating /etc/skel with miloOS user configuration..."

# Backup existing skel
if [ -d "/etc/skel" ]; then
    log_info "Backing up existing /etc/skel to /etc/skel.backup-$(date +%Y%m%d-%H%M%S)"
    cp -a /etc/skel "/etc/skel.backup-$(date +%Y%m%d-%H%M%S)"
fi

# Clear /etc/skel (except hidden files we want to keep)
rm -rf /etc/skel/.config 2>/dev/null || true
rm -rf /etc/skel/.local 2>/dev/null || true

# Copy user configurations to /etc/skel
log_info "Copying .config to /etc/skel..."
if [ -d "$SCRIPT_DIR/configurations/xfce4" ]; then
    mkdir -p /etc/skel/.config/xfce4
    cp -R "$SCRIPT_DIR/configurations/xfce4"/* /etc/skel/.config/xfce4/ 2>/dev/null || true
fi

if [ -d "$SCRIPT_DIR/configurations/plank" ]; then
    mkdir -p /etc/skel/.config/plank
    cp -R "$SCRIPT_DIR/configurations/plank"/* /etc/skel/.config/plank/ 2>/dev/null || true
fi

if [ -d "$SCRIPT_DIR/configurations/autostart" ]; then
    mkdir -p /etc/skel/.config/autostart
    cp "$SCRIPT_DIR/configurations/autostart"/* /etc/skel/.config/autostart/ 2>/dev/null || true
fi

if [ -d "$SCRIPT_DIR/configurations/gtk-3.0" ]; then
    mkdir -p /etc/skel/.config/gtk-3.0
    cp "$SCRIPT_DIR/configurations/gtk-3.0"/* /etc/skel/.config/gtk-3.0/ 2>/dev/null || true
fi

if [ -f "$SCRIPT_DIR/configurations/fonts.conf" ]; then
    mkdir -p /etc/skel/.config/fontconfig
    cp "$SCRIPT_DIR/configurations/fonts.conf" /etc/skel/.config/fontconfig/fonts.conf
fi

# Copy environment.d for PipeWire JACK
if [ -d "$SCRIPT_DIR/configurations/environment.d" ]; then
    mkdir -p /etc/skel/.config/environment.d
    cp "$SCRIPT_DIR/configurations/environment.d"/* /etc/skel/.config/environment.d/ 2>/dev/null || true
fi

# Copy .local directory if exists
if [ -d "$SCRIPT_DIR/configurations/.local" ]; then
    log_info "Copying .local to /etc/skel..."
    mkdir -p /etc/skel/.local
    cp -R "$SCRIPT_DIR/configurations/.local"/* /etc/skel/.local/ 2>/dev/null || true
fi

# Create .bashrc with JACK configuration
log_info "Creating .bashrc with JACK paths..."
cat > /etc/skel/.bashrc << 'EOF'
# miloOS .bashrc

# Source global definitions
if [ -f /etc/bashrc ]; then
    . /etc/bashrc
fi

# User specific environment
if ! [[ "$PATH" =~ "$HOME/.local/bin:$HOME/bin:" ]]
then
    PATH="$HOME/.local/bin:$HOME/bin:$PATH"
fi
export PATH

# PipeWire JACK configuration
export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PIPEWIRE_JACK=1

# Aliases
alias ll='ls -lah'
alias la='ls -A'
alias l='ls -CF'
EOF

# Create .profile
log_info "Creating .profile with JACK paths..."
cat > /etc/skel/.profile << 'EOF'
# miloOS .profile

# if running bash
if [ -n "$BASH_VERSION" ]; then
    if [ -f "$HOME/.bashrc" ]; then
        . "$HOME/.bashrc"
    fi
fi

# PipeWire JACK configuration
export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PIPEWIRE_JACK=1
EOF

# Create .xsession
log_info "Creating .xsession..."
cat > /etc/skel/.xsession << 'EOF'
#!/bin/sh
# miloOS .xsession

# PipeWire JACK configuration
export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PIPEWIRE_JACK=1

# Start XFCE session
exec startxfce4
EOF
chmod +x /etc/skel/.xsession

log_info "/etc/skel populated successfully"
echo ""

# Step 3: Configure refractasnapshot
log_info "Step 3: Configuring refractasnapshot..."

# Get current kernel version
KERNEL_VERSION=$(uname -r)
log_info "Detected kernel: $KERNEL_VERSION"

# Create refractasnapshot configuration
cat > /etc/refractasnapshot.conf << EOF
# miloOS refractasnapshot configuration
# Based on Refracta documentation

# Directories
snapshot_dir="/home/refracta"
work_dir="/home/work"
iso_dir="/home/iso"

# Live system
live_user="milo"
live_user_fullname="miloOS Live User"
live_hostname="miloOS"

# Kernel
kernel_image="/boot/vmlinuz-${KERNEL_VERSION}"
initrd_image="/boot/initrd.img-${KERNEL_VERSION}"

# Compression
squashfs_compression="xz"
squashfs_compression_options="-Xbcj x86 -b 1M"

# ISO
iso_name="miloOS-1.0-amd64.iso"
iso_label="miloOS"

# Boot
boot_options="boot=live components quiet splash"

# Architecture
architecture="amd64"

# Features
make_efi="yes"
make_isohybrid="yes"
make_md5sum="yes"

# Exclusions
snapshot_excludes="
/home/*/.cache
/home/*/.thumbnails
/home/*/Downloads
/home/*/Descargas
/root/.cache
/var/cache/apt/archives/*.deb
/var/tmp/*
/tmp/*
/swapfile
/home/refracta
/home/work
/home/iso
"
EOF

# Verify configuration file was created
if [ ! -f /etc/refractasnapshot.conf ]; then
    log_error "Failed to create /etc/refractasnapshot.conf"
    exit 1
fi

log_info "✓ Configuration file created: /etc/refractasnapshot.conf"



# Step 4: Configure GRUB for bilingual menu
log_info "Step 4: Creating bilingual GRUB menu..."

mkdir -p /etc/refractasnapshot/grub

cat > /etc/refractasnapshot/grub/grub.cfg << GRUBEOF
set default=0
set timeout=30

# Load video modules
insmod all_video
insmod gfxterm

# Set graphics mode
set gfxmode=auto
terminal_output gfxterm

# Menu colors
set menu_color_normal=white/black
set menu_color_highlight=black/light-gray

# English/Spanish menu
menuentry "miloOS Live (English)" {
    set gfxpayload=keep
    linux /live/vmlinuz-${KERNEL_VERSION} boot=live components quiet splash locales=en_US.UTF-8
    initrd /live/initrd.img-${KERNEL_VERSION}
}

menuentry "miloOS Live (Español)" {
    set gfxpayload=keep
    linux /live/vmlinuz-${KERNEL_VERSION} boot=live components quiet splash locales=es_ES.UTF-8
    initrd /live/initrd.img-${KERNEL_VERSION}
}

menuentry "miloOS Live (failsafe)" {
    linux /live/vmlinuz-${KERNEL_VERSION} boot=live components noapic noacpi nomodeset vga=normal
    initrd /live/initrd.img-${KERNEL_VERSION}
}

menuentry "Memory Test (memtest86+)" {
    linux /live/memtest
}
GRUBEOF

log_info "GRUB menu configured (English/Spanish)"
echo ""

# Step 4b: Configure ISOLINUX for Legacy BIOS
log_info "Step 4b: Configuring ISOLINUX for Legacy BIOS..."

mkdir -p /etc/refractasnapshot/isolinux

cat > /etc/refractasnapshot/isolinux/isolinux.cfg << ISOLINUXEOF
default live
label live
  menu label ^miloOS Live
  kernel /live/vmlinuz-${KERNEL_VERSION}
  append initrd=/live/initrd.img-${KERNEL_VERSION} boot=live components quiet splash

label live-en
  menu label miloOS Live (^English)
  kernel /live/vmlinuz-${KERNEL_VERSION}
  append initrd=/live/initrd.img-${KERNEL_VERSION} boot=live components quiet splash locales=en_US.UTF-8

label live-es
  menu label miloOS Live (^Español)
  kernel /live/vmlinuz-${KERNEL_VERSION}
  append initrd=/live/initrd.img-${KERNEL_VERSION} boot=live components quiet splash locales=es_ES.UTF-8

label failsafe
  menu label miloOS Live (^Failsafe)
  kernel /live/vmlinuz-${KERNEL_VERSION}
  append initrd=/live/initrd.img-${KERNEL_VERSION} boot=live components noapic noacpi nomodeset vga=normal

prompt 0
timeout 300
ISOLINUXEOF

log_info "ISOLINUX configured for Legacy BIOS"
echo ""

# Step 5: Clean old snapshots
log_info "Step 5: Cleaning old snapshots..."
rm -rf /home/refracta /home/work /home/iso 2>/dev/null || true
mkdir -p /home/refracta /home/work /home/iso

# Step 6: Run refractasnapshot
log_info "Step 6: Running refractasnapshot to create ISO..."
log_warn "This will take 30-60 minutes (xz compression is slow but produces smallest ISO)"
echo ""

# Verify refractasnapshot is installed
if ! command -v refractasnapshot &> /dev/null; then
    log_error "refractasnapshot command not found"
    log_info "Please install with: apt-get install refractasnapshot"
    exit 1
fi

# Verify configuration file exists
if [ ! -f /etc/refractasnapshot.conf ]; then
    log_error "Configuration file /etc/refractasnapshot.conf not found"
    exit 1
fi

# Verify kernel files exist
if [ ! -f "/boot/vmlinuz-${KERNEL_VERSION}" ]; then
    log_error "Kernel image not found: /boot/vmlinuz-${KERNEL_VERSION}"
    exit 1
fi

if [ ! -f "/boot/initrd.img-${KERNEL_VERSION}" ]; then
    log_error "Initrd image not found: /boot/initrd.img-${KERNEL_VERSION}"
    exit 1
fi

log_info "✓ All prerequisites verified"
echo ""

# Run refractasnapshot with the configuration file
log_info "Starting snapshot process..."
log_info "Progress will be shown below..."
echo ""

refractasnapshot -c /etc/refractasnapshot.conf 2>&1 | tee /tmp/refractasnapshot.log

# Check if it completed successfully
if [ ${PIPESTATUS[0]} -ne 0 ]; then
    log_error "refractasnapshot failed. Check /tmp/refractasnapshot.log"
    exit 1
fi

log_info "Snapshot completed successfully"
echo ""

# Step 7: Move ISO to current directory and create checksum
log_info "Step 7: Finalizing ISO..."

if [ -f "/home/iso/$ISO_NAME" ]; then
    mv "/home/iso/$ISO_NAME" "./$ISO_NAME"
    sync
    
    log_info "Calculating SHA256 checksum..."
    sha256sum "$ISO_NAME" > "${ISO_NAME}.sha256"
    
    ISO_SIZE=$(du -h "$ISO_NAME" | cut -f1)
    CHECKSUM=$(cut -d' ' -f1 "${ISO_NAME}.sha256")
    
    log_info "========================================="
    log_info "ISO created successfully!"
    log_info "========================================="
    log_info "File: $ISO_NAME"
    log_info "Size: $ISO_SIZE"
    log_info "SHA256: $CHECKSUM"
    echo ""
    log_info "Test with:"
    log_info "  BIOS: qemu-system-x86_64 -cdrom $ISO_NAME -m 2048"
    log_info "  UEFI: qemu-system-x86_64 -cdrom $ISO_NAME -m 2048 -bios /usr/share/ovmf/OVMF.fd"
    echo ""
    
    # Cleanup
    log_info "Cleaning up temporary files..."
    rm -rf /home/refracta /home/work /home/iso
    
    log_info "Done!"
else
    log_error "ISO not found at /home/iso/$ISO_NAME"
    log_error "Check refractasnapshot output for errors"
    exit 1
fi
