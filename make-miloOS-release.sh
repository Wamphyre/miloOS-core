#!/bin/bash
# miloOS ISO Builder - Simplified Approach
# Based on refractasnapshot methodology
# Version 2.0

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
WORK_DIR="/home/miloOS-snapshot"
SNAPSHOT_DIR="$WORK_DIR/myfs"
ISO_DIR="$WORK_DIR/iso"
ISO_NAME="miloOS-1.0-amd64.iso"

# Check root
if [ "$(id -u)" -ne 0 ]; then
    log_error "Must run as root"
    exit 1
fi

log_info "miloOS ISO Builder v2.0"
log_info "Using simplified snapshot approach"

# Clean old builds
log_info "Cleaning old builds..."
rm -rf "$WORK_DIR" 2>/dev/null || true
mkdir -p "$SNAPSHOT_DIR"
mkdir -p "$ISO_DIR/live"

# Step 1: Create snapshot of current system
log_info "Creating system snapshot..."
rsync -av --one-file-system \
    --exclude=/proc/* \
    --exclude=/sys/* \
    --exclude=/dev/* \
    --exclude=/run/* \
    --exclude=/tmp/* \
    --exclude=/mnt/* \
    --exclude=/media/* \
    --exclude=/lost+found \
    --exclude=/home/*/.cache \
    --exclude=/root/.cache \
    --exclude=/var/cache/apt/archives/*.deb \
    --exclude=/var/tmp/* \
    --exclude="$WORK_DIR" \
    / "$SNAPSHOT_DIR/"

log_info "Snapshot created"

# Step 2: Prepare snapshot for Live boot
log_info "Preparing snapshot for Live boot..."

# Create necessary directories
mkdir -p "$SNAPSHOT_DIR"/{proc,sys,dev,run,tmp,mnt,media}

# Install live-boot in snapshot
log_info "Installing live-boot packages..."
mount --bind /proc "$SNAPSHOT_DIR/proc"
mount --bind /sys "$SNAPSHOT_DIR/sys"  
mount --bind /dev "$SNAPSHOT_DIR/dev"

chroot "$SNAPSHOT_DIR" apt-get update
chroot "$SNAPSHOT_DIR" apt-get install -y live-boot live-boot-initramfs-tools systemd-sysv

# Update initramfs
log_info "Updating initramfs..."
chroot "$SNAPSHOT_DIR" update-initramfs -u

# Unmount
umount "$SNAPSHOT_DIR/proc"
umount "$SNAPSHOT_DIR/sys"
umount "$SNAPSHOT_DIR/dev"

# Step 3: Extract kernel and initrd
log_info "Extracting kernel and initrd..."
KERNEL=$(ls "$SNAPSHOT_DIR/boot/vmlinuz-"* | head -n 1)
INITRD=$(ls "$SNAPSHOT_DIR/boot/initrd.img-"* | head -n 1)

cp "$KERNEL" "$ISO_DIR/live/vmlinuz"
cp "$INITRD" "$ISO_DIR/live/initrd"

log_info "Kernel: $(basename $KERNEL)"
log_info "Initrd: $(basename $INITRD)"

# Step 4: Create squashfs
log_info "Creating squashfs (this takes time)..."
mksquashfs "$SNAPSHOT_DIR" "$ISO_DIR/live/filesystem.squashfs" \
    -comp xz -e boot

log_info "Squashfs created: $(du -h "$ISO_DIR/live/filesystem.squashfs" | cut -f1)"

# Step 5: Create GRUB config
log_info "Creating GRUB configuration..."
mkdir -p "$ISO_DIR/boot/grub"

cat > "$ISO_DIR/boot/grub/grub.cfg" << 'EOF'
set timeout=10
set default=0

menuentry "miloOS Live" {
    linux /live/vmlinuz boot=live quiet splash
    initrd /live/initrd
}

menuentry "miloOS Live (failsafe)" {
    linux /live/vmlinuz boot=live noapic noacpi nosplash
    initrd /live/initrd
}
EOF

# Step 6: Install GRUB for BIOS
log_info "Installing GRUB for BIOS..."
grub-mkstandalone \
    --format=i386-pc \
    --output="$ISO_DIR/boot/grub/core.img" \
    --install-modules="linux normal iso9660 biosdisk memdisk search tar ls" \
    --modules="linux normal iso9660 biosdisk search" \
    --locales="" \
    --fonts="" \
    "boot/grub/grub.cfg=$ISO_DIR/boot/grub/grub.cfg"

cat /usr/lib/grub/i386-pc/cdboot.img "$ISO_DIR/boot/grub/core.img" \
    > "$ISO_DIR/boot/grub/bios.img"

# Step 7: Create ISO
log_info "Creating ISO..."
xorriso -as mkisofs \
    -iso-level 3 \
    -full-iso9660-filenames \
    -volid "miloOS" \
    -eltorito-boot boot/grub/bios.img \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    --eltorito-catalog boot/grub/boot.cat \
    --grub2-boot-info \
    --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img \
    -output "$ISO_NAME" \
    -graft-points \
        "$ISO_DIR" \
        /boot/grub/bios.img="$ISO_DIR/boot/grub/bios.img"

if [ -f "$ISO_NAME" ]; then
    log_info "ISO created successfully: $ISO_NAME"
    log_info "Size: $(du -h "$ISO_NAME" | cut -f1)"
    
    # Calculate checksum
    sha256sum "$ISO_NAME" > "${ISO_NAME}.sha256"
    log_info "Checksum: $(cut -d' ' -f1 "${ISO_NAME}.sha256")"
    
    # Cleanup
    log_info "Cleaning up..."
    rm -rf "$WORK_DIR"
    
    log_info "Done! Test with:"
    log_info "  qemu-system-x86_64 -cdrom $ISO_NAME -m 2048"
else
    log_error "Failed to create ISO"
    exit 1
fi
