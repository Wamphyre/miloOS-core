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

**Visual & Desktop Environment:**
- ✅ Install and configure SLiM display manager with custom "milk" theme
- ✅ Install miloOS GTK+ themes (macOS-inspired)
- ✅ Install WhiteSur-light icon theme (macOS Big Sur style)
- ✅ Install San Francisco Pro fonts (Apple's official typefaces: SF Pro Text, SF Pro Display, SF Mono)
- ✅ Configure macOS-like font rendering (DPI 96, hintslight, RGB subpixel)
- ✅ Install custom wallpapers
- ✅ Install Plank dock with custom "milo" theme
- ✅ Configure XFCE4 panel with top bar layout
- ✅ Install Apple Mac Plymouth boot theme
- ✅ Desktop icons aligned to top-right (macOS style)

**Audio Production:**
- ✅ Install and configure PipeWire with pro-audio profile
- ✅ Configure low-latency audio (48kHz, 256 buffer, S32LE format)
- ✅ Set RTKit priority 88 for real-time audio
- ✅ Configure kernel parameters (preempt=full, nohz_full=all, threadirqs)
- ✅ Set system limits for audio production (rtprio 99, memlock unlimited)
- ✅ Configure sysctl optimizations (swappiness, inotify, shmmax)
- ✅ Add all users to audio group automatically

**Custom Menu System:**
- ✅ Install custom miloOS menu items (bilingual: English/Spanish)
- ✅ Hide default XFCE system menu items
- ✅ Custom actions: About, Settings, Sleep, Restart, Shutdown, Logout

**System Rebranding:**
- ✅ **Rebrand system from Debian to miloOS**
- ✅ Update system identification files
- ✅ Configure GRUB bootloader
- ✅ Custom MOTD (Message of the Day)

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
# Run the verification script
./verify_installation.sh

# Or manually check components:

# Check system information
cat /etc/os-release
lsb_release -a

# Check installed theme
xfconf-query -c xfwm4 -p /general/theme

# Check icon theme
xfconf-query -c xsettings -p /Net/IconThemeName

# Check fonts
fc-list | grep "SF"

# Check PipeWire
pactl info | grep "Server Name"

# Check audio limits
cat /etc/security/limits.d/99-audio-production.conf

# Check if user is in audio group
groups $USER
```

## Restoring Debian Branding

If you need to restore the original Debian branding:

```bash
sudo ./resources/restore_debian_branding.sh
```

This will restore all original system files from the backup.

## Features

**Desktop Experience:**
- ✅ Complete macOS Big Sur-like appearance
- ✅ San Francisco Pro fonts throughout the system
- ✅ WhiteSur icon theme (light variant)
- ✅ Custom SLiM login screen
- ✅ Apple Mac Plymouth boot animation
- ✅ Top panel with global menu support
- ✅ Plank dock at bottom (intelligent hide mode)
- ✅ Desktop icons on top-right

**Audio Production:**
- ✅ PipeWire with pro-audio profile by default
- ✅ Ultra-low latency configuration (256 samples @ 48kHz)
- ✅ Real-time kernel parameters
- ✅ Optimized system limits for audio work
- ✅ Ready for professional DAWs (Ardour, Reaper, Bitwig, etc.)

**Internationalization:**
- ✅ Bilingual menu system (English/Spanish)
- ✅ Automatic language detection

**System:**
- ✅ Complete system rebranding from Debian to miloOS
- ✅ Automatic backup of original system files
- ✅ Full compatibility with Debian packages
- ✅ Reversible installation
- ✅ Verification script included

## Requirements

- **Operating System:** Debian 13 (Trixie) base system
- **Desktop Environment:** XFCE4 (must be pre-installed)
- **Permissions:** Root/sudo access
- **Network:** Internet connection for downloading packages and themes
- **Disk Space:** At least 500MB free space
- **Tools:** wget or curl, git, unzip (will be installed if missing)

## Technical Details

**Fonts:**
- SF Pro Text 10pt (UI)
- SF Pro Display Medium 9pt (Window titles)
- SF Mono 11pt (System monospace)
- SF Mono 13pt (Terminal)

**Audio Configuration:**
- Sample Rate: 48kHz
- Buffer Size: 256 samples (quantum)
- Format: S32LE (32-bit)
- Profile: pro-audio
- RTKit Priority: 88

**Kernel Parameters:**
- preempt=full (Fully preemptible kernel)
- nohz_full=all (No tick on all CPUs)
- threadirqs (Threaded IRQs)

## Troubleshooting

**Fonts not showing correctly:**
```bash
# Update font cache
fc-cache -f
# Logout and login again
```

**Audio not working:**
```bash
# Check if user is in audio group
groups $USER
# If not, add user and reboot
sudo usermod -aG audio $USER
```

**Plymouth theme not showing:**
```bash
# Update initramfs
sudo update-initramfs -u -k all
```

## Notes

* The system is fully compatible with Debian packages and repositories
* All changes are reversible with automatic backups
* System backup location: `/root/debian-backup-YYYYMMDD-HHMMSS/`
* User config backup: `~/.config/miloOS-backup-YYYYMMDD-HHMMSS/` (if created)
* Reboot required after installation for all changes to take effect
* First login after installation may take a moment as fonts are cached

## Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

## License

This project inherits licenses from its components. See individual component licenses for details.

## Credits

- **Themes:** WhiteSur by vinceliuice
- **Fonts:** San Francisco Pro by Apple (via sahibjotsaggu)
- **Plymouth:** Apple Mac theme by Navis Michael Bearly
- **Base System:** Debian Project
