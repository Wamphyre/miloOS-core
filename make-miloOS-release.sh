#!/bin/bash
# miloOS ISO Builder
# Creates a bootable Live ISO from current system
# Version 3.0

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

log_info "========================================="
log_info "miloOS ISO Builder v3.0"
log_info "========================================="

# Clean old builds
log_info "Cleaning old builds..."
rm -rf "$WORK_DIR" 2>/dev/null || true
mkdir -p "$SNAPSHOT_DIR"
mkdir -p "$ISO_DIR"/{live,isolinux,boot/grub}

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
KERNEL=$(ls "$SNAPSHOT_DIR/boot/vmlinuz-"* 2>/dev/null | head -n 1)
INITRD=$(ls "$SNAPSHOT_DIR/boot/initrd.img-"* 2>/dev/null | head -n 1)

if [ -z "$KERNEL" ] || [ ! -f "$KERNEL" ]; then
    log_error "Kernel not found in $SNAPSHOT_DIR/boot/"
    exit 1
fi

if [ -z "$INITRD" ] || [ ! -f "$INITRD" ]; then
    log_error "Initrd not found in $SNAPSHOT_DIR/boot/"
    exit 1
fi

# Get kernel version
KERNEL_VERSION=$(basename "$KERNEL" | sed 's/vmlinuz-//')
KERNEL_NAME="vmlinuz-${KERNEL_VERSION}"
INITRD_NAME="initrd.img-${KERNEL_VERSION}"

log_info "Kernel version: $KERNEL_VERSION"
log_info "Kernel file: $KERNEL_NAME"
log_info "Initrd file: $INITRD_NAME"

# Copy with original names
cp "$KERNEL" "$ISO_DIR/live/$KERNEL_NAME"
cp "$INITRD" "$ISO_DIR/live/$INITRD_NAME"

# Also create symlinks for compatibility
ln -sf "$KERNEL_NAME" "$ISO_DIR/live/vmlinuz"
ln -sf "$INITRD_NAME" "$ISO_DIR/live/initrd.img"

# Step 4: Create squashfs
log_info "Creating squashfs (this takes time)..."
mksquashfs "$SNAPSHOT_DIR" "$ISO_DIR/live/filesystem.squashfs" \
    -comp xz -e boot

log_info "Squashfs created: $(du -h "$ISO_DIR/live/filesystem.squashfs" | cut -f1)"

# Verify live directory contents
log_info "Verifying live directory..."
ls -lh "$ISO_DIR/live/"
if [ ! -f "$ISO_DIR/live/$KERNEL_NAME" ]; then
    log_error "Kernel not found: $KERNEL_NAME"
    exit 1
fi
if [ ! -f "$ISO_DIR/live/$INITRD_NAME" ]; then
    log_error "Initrd not found: $INITRD_NAME"
    exit 1
fi
if [ ! -f "$ISO_DIR/live/filesystem.squashfs" ]; then
    log_error "Squashfs not found!"
    exit 1
fi
log_info "✓ Kernel: $KERNEL_NAME"
log_info "✓ Initrd: $INITRD_NAME"
log_info "✓ Squashfs: filesystem.squashfs"

# Step 5: Create ISOLINUX config (for BIOS boot)
log_info "Creating ISOLINUX configuration..."
cat > "$ISO_DIR/isolinux/isolinux.cfg" << EOF
DEFAULT live
LABEL live
  MENU LABEL miloOS Live
  KERNEL /live/$KERNEL_NAME
  APPEND initrd=/live/$INITRD_NAME boot=live quiet splash
LABEL failsafe
  MENU LABEL miloOS Live (failsafe)
  KERNEL /live/$KERNEL_NAME
  APPEND initrd=/live/$INITRD_NAME boot=live noapic noacpi nosplash
EOF

# Copy ISOLINUX files
cp /usr/lib/ISOLINUX/isolinux.bin "$ISO_DIR/isolinux/" 2>/dev/null || \
    cp /usr/lib/syslinux/modules/bios/isolinux.bin "$ISO_DIR/isolinux/"
cp /usr/lib/syslinux/modules/bios/*.c32 "$ISO_DIR/isolinux/" 2>/dev/null || true

# Step 6: Create GRUB config (for UEFI boot)
log_info "Creating GRUB configuration..."
cat > "$ISO_DIR/boot/grub/grub.cfg" << EOF
set timeout=10
set default=0

menuentry "miloOS Live" {
    linux /live/$KERNEL_NAME boot=live quiet splash
    initrd /live/$INITRD_NAME
}

menuentry "miloOS Live (failsafe)" {
    linux /live/$KERNEL_NAME boot=live noapic noacpi nosplash
    initrd /live/$INITRD_NAME
}
EOF

# Step 7: Create UEFI boot
log_info "Creating UEFI boot..."
mkdir -p "$ISO_DIR/EFI/boot"

grub-mkstandalone \
    --format=x86_64-efi \
    --output="$ISO_DIR/EFI/boot/bootx64.efi" \
    --locales="" \
    --fonts="" \
    "boot/grub/grub.cfg=$ISO_DIR/boot/grub/grub.cfg"

# Create EFI boot image
dd if=/dev/zero of="$ISO_DIR/boot/grub/efi.img" bs=1M count=10 2>/dev/null
mkfs.vfat "$ISO_DIR/boot/grub/efi.img" >/dev/null 2>&1

MOUNT_POINT=$(mktemp -d)
mount -o loop "$ISO_DIR/boot/grub/efi.img" "$MOUNT_POINT"
mkdir -p "$MOUNT_POINT/EFI/boot"
cp "$ISO_DIR/EFI/boot/bootx64.efi" "$MOUNT_POINT/EFI/boot/"
umount "$MOUNT_POINT"
rmdir "$MOUNT_POINT"

# Step 8: Create ISO with both BIOS and UEFI support
log_info "Creating hybrid ISO..."
xorriso -as mkisofs \
    -iso-level 3 \
    -full-iso9660-filenames \
    -volid "miloOS" \
    -isohybrid-mbr /usr/lib/ISOLINUX/isohdpfx.bin \
    -eltorito-boot isolinux/isolinux.bin \
    -eltorito-catalog isolinux/boot.cat \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    -eltorito-alt-boot \
    -e boot/grub/efi.img \
    -no-emul-boot \
    -isohybrid-gpt-basdat \
    -output "$ISO_NAME" \
    "$ISO_DIR"

if [ -f "$ISO_NAME" ]; then
    log_info "ISO created successfully: $ISO_NAME"
    log_info "Size: $(du -h "$ISO_NAME" | cut -f1)"
    
    # Calculate checksum
    sha256sum "$ISO_NAME" > "${ISO_NAME}.sha256"
    log_info "Checksum: $(cut -d' ' -f1 "${ISO_NAME}.sha256")"
    
    # Verify ISO contents
    log_info "Verifying ISO contents..."
    if command -v isoinfo &> /dev/null; then
        log_info "ISO structure:"
        isoinfo -l -i "$ISO_NAME" | grep -E "live/|boot/|EFI/" | head -20
    fi
    
    # Cleanup
    log_info "Cleaning up..."
    rm -rf "$WORK_DIR"
    
    log_info "Done! Test with:"
    log_info "  BIOS: qemu-system-x86_64 -cdrom $ISO_NAME -m 2048"
    log_info "  UEFI: qemu-system-x86_64 -cdrom $ISO_NAME -m 2048 -bios /usr/share/ovmf/OVMF.fd"
else
    log_error "Failed to create ISO"
    exit 1
fi
