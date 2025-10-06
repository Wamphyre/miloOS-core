# miloOS Core

![miloOS Desktop](miloOS-desktop.png)

A beautiful macOS-inspired desktop environment for Debian-based Linux distributions, built on XFCE4. Transform your Linux system into a polished, production-ready workstation with real-time audio optimization and custom applications.

## Overview

miloOS Core provides a complete desktop transformation that combines the elegance of macOS with the power and flexibility of Linux. Built on XFCE4 for performance and stability, it includes custom applications (miloApps) designed specifically for the miloOS ecosystem.

## Features

### Visual Experience
- **macOS-like Interface**: Custom XFCE4 configuration with top panel and Plank dock
- **San Francisco Pro Typography**: System-wide macOS fonts for a cohesive look
- **WhiteSur Icon Theme**: Beautiful macOS Big Sur-style icons
- **Custom miloOS Theme**: GTK theme with classic macOS blue selection color (#007AFF)
- **Optimized Font Rendering**: macOS-like antialiasing and hinting

### System Integration
- **Complete Rebranding**: Transform Debian into miloOS (os-release, GRUB, login banners)
- **Custom Power Menu**: Bilingual (EN/ES) shutdown, restart, suspend, and logout dialogs
- **SLiM Display Manager**: Lightweight login manager with custom theme
- **Polkit Integration**: Seamless power management without password prompts

### Audio Production Ready
- **PipeWire Optimization**: Pre-configured for low-latency audio production
- **Real-time Kernel Parameters**: `preempt=full`, `nohz_full=all`, `threadirqs`, `mitigations=off`
- **Pro-Audio Profile**: Automatic device configuration for professional audio work
- **System Tuning**: CPU governor, I/O scheduler, and memory optimizations

### miloApps Suite

Custom applications designed for miloOS:

#### AudioConfig
A simple, elegant audio configuration tool for PipeWire:
- **Sample Rate Selection**: 44.1kHz to 192kHz
- **Buffer Size Control**: 32 to 1024 samples
- **Device Management**: Set default input/output devices
- **Automatic Application**: Restarts PipeWire automatically
- **Bilingual Interface**: English and Spanish support
- **Custom Icon**: Integrated with system theme

*More miloApps coming soon!*

## System Requirements

- **OS**: Debian 12 (Bookworm) or Debian 13 (Trixie)
- **Desktop**: XFCE4 Desktop Environment
- **RAM**: 2GB minimum (4GB recommended)
- **Storage**: 10GB free disk space
- **Network**: Internet connection for installation

## Installation

### Quick Start

```bash
# Clone the repository
git clone https://github.com/Wamphyre/miloOS-core.git
cd miloOS-core

# Make scripts executable
chmod +x core_install.sh

# Run the installer
bash core_install.sh install
```

The installer will prompt for your sudo password and guide you through the process. After installation, log out and log back in to see the changes.

### Installation Process

The installer performs the following steps:

1. **System Verification**: Checks Debian version, XFCE4 installation, and disk space
2. **Package Installation**: Installs required packages (GTK engines, Plank, PipeWire, etc.)
3. **Theme Installation**: Installs miloOS GTK theme and downloads WhiteSur icons
4. **Font Installation**: Downloads and installs San Francisco Pro fonts
5. **Visual Resources**: Installs wallpapers, Plank theme, and custom icons
6. **Menu System**: Installs custom application menu and power management scripts
7. **System Rebranding**: Updates system identification files and GRUB
8. **Audio Optimization**: Configures PipeWire and kernel parameters for real-time audio
9. **miloApps Installation**: Installs AudioConfig and other custom applications
10. **Service Configuration**: Disables Plymouth, configures polkit for power management

### Post-Installation

After installation:
- Log out and log back in
- The panel will appear at the top with macOS-style layout
- Plank dock will auto-start at the bottom
- All system branding will show "miloOS"
- Audio system will be optimized for low-latency

## Configuration

### Panel Layout
The top panel includes (left to right):
- Application menu (miloOS logo)
- Global menu (appmenu)
- Window buttons
- System tray
- Audio controls
- Notifications
- Clock (24-hour format)
- Power menu launcher

### Plank Dock
Located at the bottom center, auto-hides intelligently. Right-click for preferences.

### Audio Configuration
Use AudioConfig (in Applications menu) to adjust:
- Sample rate
- Buffer size
- Default audio devices

Configuration is saved to `~/.config/pipewire/pipewire.conf.d/99-custom.conf`

### Fonts
- **System**: SF Pro Text 10
- **Window Titles**: SF Pro Display Medium 9
- **Monospace**: SF Pro Text Regular 10

## miloApps

### AudioConfig

**Location**: Applications → Settings → Audio Configuration

**Features**:
- Visual interface for PipeWire configuration
- Real-time device detection
- Automatic PipeWire restart on apply
- Persistent settings
- Bilingual (detects system language)

**Usage**:
```bash
audio-config
```

**Configuration File**: `~/.config/pipewire/pipewire.conf.d/99-custom.conf`

## Real-time Audio Details

### PipeWire Configuration
- **Default Sample Rate**: 48kHz
- **Default Buffer**: 256 samples
- **RT Priority**: 88
- **Profile**: pro-audio (automatic)

### Kernel Parameters
Automatically configured in GRUB:
- `preempt=full` - Fully preemptible kernel for low latency
- `nohz_full=all` - Tickless operation on all CPUs
- `threadirqs` - Threaded interrupt handlers
- `mitigations=off` - Disabled CPU mitigations for better performance

### System Optimizations
- CPU governor: performance
- Swappiness: 10
- File descriptors: 524288
- Audio group: real-time permissions
- Memory: optimized for audio workloads

## Verification

Check your installation:

```bash
bash verify_installation.sh
```

This verifies:
- Package installation
- Theme and icon files
- Font availability
- Configuration files
- System rebranding
- Audio optimization
- Kernel parameters

## Customization

### Change Wallpaper
1. Right-click on desktop → Desktop Settings
2. Select from `/usr/share/backgrounds/miloOS/`

### Modify Panel
1. Right-click on panel → Panel → Panel Preferences
2. Or edit: `~/.config/xfce4/xfconf/xfce-perchannel-xml/xfce4-panel.xml`

### Adjust Audio Settings
Use AudioConfig application or manually edit:
```bash
nano ~/.config/pipewire/pipewire.conf.d/99-custom.conf
systemctl --user restart pipewire
```

### Theme Colors
Edit theme files in `/usr/share/themes/miloOS/gtk-3.0/`

## Troubleshooting

### Panel Issues
```bash
xfce4-panel --restart
```

### Plank Not Starting
```bash
plank &
```

### Font Cache
```bash
fc-cache -f
```

### Audio Problems
```bash
systemctl --user restart pipewire pipewire-pulse
```

### GRUB Changes Not Applied
```bash
sudo update-grub
sudo update-initramfs -u
```

### Power Menu Not Working
Ensure polkit is configured:
```bash
ls /etc/polkit-1/rules.d/50-miloOS-power.rules
```

## Backup and Restore

### Backup Location
Original Debian files are backed up to:
```
/root/debian-backup-YYYYMMDD-HHMMSS/
```

### Restore System
To revert to Debian:
1. Restore files from backup directory
2. Remove miloOS themes: `sudo rm -rf /usr/share/themes/miloOS`
3. Reset XFCE4: `xfce4-panel --restart`
4. Update GRUB: `sudo update-grub`

## Development

### Project Structure
```
miloOS-core/
├── AudioConfig/          # AudioConfig application
├── configurations/       # XFCE4 and system configs
├── resources/           # Themes, icons, fonts, menus
├── core_install.sh      # Main installer
└── verify_installation.sh
```

### Contributing
Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Submit a pull request

## Credits

- **Theme Base**: Elementary OS theme
- **Icons**: [WhiteSur Icon Theme](https://github.com/vinceliuice/WhiteSur-icon-theme) by vinceliuice
- **Fonts**: San Francisco Pro by Apple Inc.
- **Inspiration**: macOS design language

## License

MIT License - See LICENSE file for details

## Disclaimer

This project is not affiliated with Apple Inc. macOS is a trademark of Apple Inc. This is a fan-made project for educational and personal use only.

## Author

**Wamphyre**
- GitHub: [@Wamphyre](https://github.com/Wamphyre)

## Version

**2.1** - Enhanced with miloApps suite, improved audio optimization, and bilingual support

---

*Transform your Linux experience with miloOS Core* 🚀
