# Core configuration and releases for miloOS

Contains the base desktop configuration and system core elements.

Releases of the final ISO are available too.

# About miloOS

miloOS is a GNU/Linux operating system based on Debian 13 (Trixie), designed specifically for multimedia audio/video tasks where latency is a crucial factor. It incorporates the latest technologies while maintaining a productive workflow.

Built upon the solid foundation of Debian 13, miloOS offers a stable and reliable environment for demanding audio and video production work. It leverages the robustness and flexibility of the GNU/Linux ecosystem to provide a seamless experience tailored to the needs of multimedia professionals.

One of the standout features of miloOS is its exceptional focus on minimizing latency. Recognizing the critical importance of real-time responsiveness in audio/video production, miloOS employs optimized configurations and kernel tweaks to achieve ultra-low latency performance. This ensures that users can work with precision and accuracy, capturing and editing audio and video with minimal delay.

Moreover, miloOS integrates cutting-edge technologies to enhance the creative workflow. It includes advanced audio and video editing tools, along with an array of plugins and effects for professionals to achieve the desired artistic results. The user interface is thoughtfully designed to promote productivity, with intuitive controls and efficient organization of resources.

As a GNU/Linux distribution, miloOS benefits from the vast software ecosystem of Debian and the broader open-source community. Users can leverage a wide range of multimedia applications, libraries, and utilities, all readily available through the comprehensive package management system.

## Screenshot

![Screenshot](https://github.com/Wamphyre/miloOS-core/blob/main/miloOS-desktop.png)

## Installation

### Quick Start

1. Clone this repository:
```bash
git clone https://github.com/Wamphyre/miloOS-core.git
cd miloOS-core
```

2. Make scripts executable:
```bash
./make_scripts_executable.sh
```

3. Run the installation:
```bash
./core_install.sh install
```

4. Reboot your system for all changes to take effect

### What Gets Installed

The installation script will:
- ✅ Install required packages and dependencies
- ✅ Install miloOS GTK+ themes
- ✅ Install WhiteSur icon theme (macOS Big Sur style)
- ✅ Install San Francisco Pro fonts (Apple's official fonts)
- ✅ Install and configure PipeWire for real-time audio
- ✅ Install custom wallpapers
- ✅ Install Plank dock theme
- ✅ Configure XFCE4 panel and settings with macOS-like font rendering
- ✅ Install custom menu items
- ✅ Optimize system for real-time audio production
- ✅ **Rebrand system from Debian to miloOS**

### System Rebranding

The installation automatically rebrands your Debian system to miloOS by modifying:
- System identification files (`/etc/os-release`, `/etc/lsb-release`)
- Login banners (`/etc/issue`, `/etc/issue.net`)
- GRUB bootloader configuration
- Message of the Day (MOTD)
- LightDM greeter theme

**Important:** 
- A backup is automatically created in `/root/debian-backup-YYYYMMDD-HHMMSS/`
- The system maintains full compatibility with Debian packages
- You can restore the original Debian branding using `sudo ./resources/restore_debian_branding.sh`

### Verification

After installation and reboot, verify the system:

```bash
# Check system information
cat /etc/os-release
lsb_release -a

# Check installed theme
xfconf-query -c xfwm4 -p /general/theme

# Check icon theme
xfconf-query -c xsettings -p /Net/IconThemeName
```

## Restoring Debian Branding

If you need to restore the original Debian branding:

```bash
sudo ./resources/restore_debian_branding.sh
```

This will restore all original system files from the backup.

## Features

- ✅ Complete system rebranding from Debian to miloOS
- ✅ macOS-like XFCE4 desktop environment
- ✅ Custom GTK+ themes and icon sets
- ✅ Plank dock with custom theme
- ✅ Automatic backup of original system files
- ✅ Full compatibility with Debian packages
- ✅ Reversible installation

## Requirements

- Debian 13 (Trixie) base system
- XFCE4 desktop environment
- Root/sudo access
- Internet connection for package installation

## Notes

* The system is fully compatible with Debian packages and repositories
* All changes are reversible with automatic backups
* Backup location: `/root/debian-backup-YYYYMMDD-HHMMSS/`
* Reboot required after installation
