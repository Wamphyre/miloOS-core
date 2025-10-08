# miloOS ISO Build System

## Overview

This document describes how to build a bootable ISO image of miloOS from your configured system.

## Requirements

### System Requirements
- Debian-based system (Debian 12/13 or Ubuntu)
- Root access (sudo)
- At least 20GB free disk space in `/tmp`
- Internet connection (for dependencies)

### Software Requirements
The script will automatically install these if missing:
- `debootstrap`
- `squashfs-tools`
- `xorriso`
- `grub-pc-bin`
- `grub-efi-amd64-bin`
- `rsync`
- `git`
- `unzip`

## Building the ISO

### Basic Usage

```bash
sudo ./make-miloOS-release.sh
```

This will:
1. Verify system requirements
2. Prepare user configurations
3. Copy the system to a temporary directory
4. Configure Live system with user "milo" (password: 1234)
5. Install and configure Calamares installer
6. Create bootable ISO with BIOS and UEFI support
7. Generate SHA256 checksum

### Advanced Options

```bash
# Show help
sudo ./make-miloOS-release.sh --help

# Enable verbose output
sudo ./make-miloOS-release.sh --verbose

# Show version
./make-miloOS-release.sh --version
```

## Build Process

The build process takes approximately 20-40 minutes depending on your system:

1. **Preparation** (2-5 min): Copy configurations to /etc/skel
2. **System Copy** (5-10 min): Copy system files with rsync
3. **Live Configuration** (1-2 min): Create live user and services
4. **Calamares Setup** (2-3 min): Install and configure installer
5. **Squashfs Creation** (10-20 min): Compress filesystem
6. **ISO Creation** (2-5 min): Build bootable image

## Output

After successful build, you'll find:

- `miloOS-1.0-amd64.iso` - Bootable ISO image (2-4 GB)
- `miloOS-1.0-amd64.iso.sha256` - SHA256 checksum
- `/tmp/miloOS-build-YYYYMMDD-HHMMSS.log` - Detailed build log

## Testing the ISO

### In Virtual Machine

**VirtualBox:**
```bash
# Create new VM
VBoxManage createvm --name "miloOS-Test" --ostype Debian_64 --register
VBoxManage modifyvm "miloOS-Test" --memory 4096 --vram 128
VBoxManage storagectl "miloOS-Test" --name "IDE" --add ide
VBoxManage storageattach "miloOS-Test" --storagectl "IDE" --port 0 --device 0 --type dvddrive --medium miloOS-1.0-amd64.iso
VBoxManage startvm "miloOS-Test"
```

**QEMU:**
```bash
# BIOS mode
qemu-system-x86_64 -cdrom miloOS-1.0-amd64.iso -m 4096 -boot d

# UEFI mode
qemu-system-x86_64 -cdrom miloOS-1.0-amd64.iso -m 4096 -boot d -bios /usr/share/ovmf/OVMF.fd
```

### On Physical Hardware

#### Linux
```bash
# Find USB device
lsblk

# Write ISO to USB (replace sdX with your device)
sudo dd if=miloOS-1.0-amd64.iso of=/dev/sdX bs=4M status=progress oflag=sync

# Or use a GUI tool
sudo gnome-disks  # GNOME Disks
```

#### Windows
1. Download [Rufus](https://rufus.ie/)
2. Select miloOS ISO
3. Select USB drive
4. Click "Start"

Or use [Etcher](https://www.balena.io/etcher/)

#### macOS
```bash
# Find USB device
diskutil list

# Unmount (replace diskN with your device)
diskutil unmountDisk /dev/diskN

# Write ISO
sudo dd if=miloOS-1.0-amd64.iso of=/dev/rdiskN bs=4m

# Eject
diskutil eject /dev/diskN
```

Or use [Etcher](https://www.balena.io/etcher/)

## Live System

### Credentials
- **Username:** milo
- **Password:** 1234

### Features
- Full miloOS experience
- All applications and configurations
- Real-time audio optimizations
- PipeWire/JACK configured
- Installer icon on desktop

### Limitations
- Changes are not persistent (unless installed)
- Limited to available RAM
- Some hardware may require additional drivers

## Installation

1. Boot from ISO/USB
2. Log in with credentials (milo/1234)
3. Double-click "Install miloOS" icon on desktop
4. Follow Calamares installer:
   - Select language
   - Configure keyboard
   - Partition disk (automatic or manual)
   - Create user account
   - Review and install
5. Reboot into installed system

### Post-Installation

All configurations are preserved:
- ✅ XFCE4 settings
- ✅ Plank dock
- ✅ Audio optimizations
- ✅ PipeWire/JACK configuration
- ✅ Themes and fonts
- ✅ miloApps (AudioConfig, etc.)
- ✅ Real-time kernel parameters
- ✅ Audio group permissions

## Troubleshooting

### Build Fails

**Insufficient disk space:**
```bash
# Check available space
df -h /tmp

# Clean up if needed
sudo rm -rf /tmp/miloOS-build-*
```

**Missing dependencies:**
```bash
# Install manually
sudo apt-get update
sudo apt-get install debootstrap squashfs-tools xorriso grub-pc-bin grub-efi-amd64-bin rsync
```

**Permission denied:**
```bash
# Ensure running as root
sudo ./make-miloOS-release.sh
```

### ISO Won't Boot

**BIOS/UEFI issues:**
- Try both BIOS and UEFI modes in VM
- Check BIOS settings on physical hardware
- Disable Secure Boot if enabled

**USB not bootable:**
- Verify ISO integrity with SHA256
- Try different USB port
- Use different USB creation tool
- Try different USB drive

### Live System Issues

**No audio:**
- Audio works after installation
- Some hardware needs additional drivers

**Display issues:**
- Try "Safe Mode" from GRUB menu
- Use "Failsafe" mode for problematic hardware

**Keyboard/mouse not working:**
- Try different USB ports
- Check BIOS USB settings

## Customization

### Before Building

To customize the ISO, modify your system first:
1. Configure XFCE4 as desired
2. Install additional software
3. Customize themes
4. Then run the build script

All your configurations will be included in the ISO.

### After Building

To modify an existing ISO:
1. Extract ISO contents
2. Modify squashfs
3. Rebuild ISO with xorriso

(Advanced users only)

## Technical Details

### ISO Structure
```
miloOS-1.0-amd64.iso
├── boot/
│   └── grub/
│       ├── grub.cfg
│       ├── i386-pc/      (BIOS boot)
│       └── boot.cat
├── live/
│   ├── vmlinuz           (Kernel)
│   ├── initrd.img        (Initial RAM disk)
│   └── filesystem.squashfs (Compressed root filesystem)
└── EFI/
    └── BOOT/
        ├── bootx64.efi   (UEFI boot)
        └── efiboot.img
```

### Compression
- **Method:** XZ (best compression)
- **Block size:** 1MB
- **Dictionary:** 100%
- **Result:** ~50-60% compression ratio

### Boot Support
- ✅ BIOS Legacy
- ✅ UEFI 64-bit
- ✅ Hybrid MBR (USB bootable)
- ✅ Secure Boot compatible (if keys added)

## FAQ

**Q: How long does it take to build?**  
A: 20-40 minutes depending on system speed.

**Q: Can I build on Ubuntu?**  
A: Yes, any Debian-based system works.

**Q: Will my personal data be included?**  
A: No, only configurations from /etc/skel are included. Personal files in /home are excluded.

**Q: Can I customize the Live user?**  
A: Yes, edit the script and change username/password in `configure_live_user()` function.

**Q: How do I update the ISO?**  
A: Update your system, then rebuild the ISO.

**Q: Can I distribute the ISO?**  
A: Yes, miloOS is open source (GPL-3.0). Include the license and source code.

## Support

- **Issues:** https://github.com/Wamphyre/miloOS-core/issues
- **Discussions:** https://github.com/Wamphyre/miloOS-core/discussions
- **Documentation:** https://github.com/Wamphyre/miloOS-core

## License

miloOS is released under the GNU General Public License v3.0.

See LICENSE file for details.
