# miloOS

![miloOS Desktop](miloOS-desktop.png)

**Transform Debian 13 into a professional audio workstation.**

miloOS is a collection of scripts and configurations that converts a clean Debian 13 (Trixie) installation into an elegant, professional audio production system. No bloat, no complexity—just a refined, ready-to-use creative environment.

---

## What is miloOS?

miloOS is **not a distribution**—it's a transformation kit. Install vanilla Debian 13 with XFCE, run our scripts, and get a complete professional audio workstation with:

- **Optimized kernel** for low-latency audio
- **PipeWire + JACK** pre-configured and working out-of-the-box
- **Professional audio plugins** (LSP, Calf, x42, ZynAddSubFX, and more)
- **Clean, elegant interface** inspired by professional workflows
- **Custom miloApps** for system management
- **Zero configuration needed**—just install and create

---

## Why miloOS?

### vs. Commercial Systems
- ✅ **No vendor lock-in** - Your system, your rules
- ✅ **No planned obsolescence** - Run on any hardware
- ✅ **No subscription fees** - Free and open source
- ✅ **Complete control** - Customize everything
- ✅ **Better performance** - Optimized for audio, no bloat

### vs. Other Linux Audio Distros
- ✅ **Debian foundation** - Rock-solid stability
- ✅ **Modern audio stack** - PipeWire with full JACK compatibility
- ✅ **Elegant interface** - Professional appearance, not cluttered
- ✅ **Custom tools** - miloApps designed for audio workflows
- ✅ **Simple installation** - One script, done

### The miloOS Difference
- **Professional appearance** - Clean interface that stays out of your way
- **Audio-first design** - Every optimization focused on low-latency performance
- **Thoughtful defaults** - Works immediately, no tweaking required
- **Debian reliability** - Stable base with vast package ecosystem

---

## Features

### 🎵 Professional Audio
- **Real-time kernel parameters** - Fully preemptible, no timer ticks, threaded IRQs
- **PipeWire + JACK** - Full compatibility with professional DAWs (Reaper, Ardour, Bitwig)
- **Pro-audio profile** - Automatic device configuration for lowest latency
- **Professional plugins included** - LSP Plugins, Calf, x42, Guitarix, Hydrogen, and more
- **Zero configuration** - Launch your DAW from anywhere, it just works

### 🎨 Elegant Interface
- **Clean design** - Top panel, Plank dock, hidden window titles
- **San Francisco Pro fonts** - Professional typography system-wide
- **WhiteSur icons** - Consistent, modern visual language
- **Custom miloOS theme** - Professional blue accent (#007AFF)
- **Distraction-free** - Focus on your work, not the system

### 🛠️ miloApps Suite

**AudioConfig** - Professional audio server configuration
- Sample rates: 44.1kHz to 192kHz
- Buffer sizes: 32 to 1024 samples
- Audio formats: 16/24/32-bit, float
- Bilingual interface (English/Spanish)
- macOS-inspired design

**SysStats** - System statistics and hardware information
- Real-time CPU, memory, disk, and network monitoring
- Hardware details: CPU model, RAM modules, GPU, storage
- Per-core CPU usage visualization
- Network activity graphs
- Process management
- Replaces "About this computer" in system menu
- Bilingual interface (English/Spanish)
- Activity Monitor-inspired design

**miloUpdater** - System update manager
- Check for system updates (apt update)
- Install updates with one click (apt upgrade)
- Real-time terminal output
- Clean, modern interface
- PolicyKit integration for secure authentication
- Bilingual interface (English/Spanish)
- Integrated in XFCE Settings and miloOS menu

### ⚙️ System Integration
- **Complete rebranding** - System identifies as miloOS
- **SLiM login manager** - Fast, lightweight, custom theme
- **Power management** - Bilingual dialogs, no password prompts
- **Custom menus** - Clean organization, no clutter
- **Automatic user setup** - Real-time privileges, audio group configuration

---

## Installation

### Requirements
- Fresh Debian 13 (Trixie) installation
- 20GB free disk space
- Internet connection

### Quick Start

```bash
# Clone the repository
git clone https://github.com/Wamphyre/miloOS-core.git
cd miloOS-core

# Run the installer
./core_install.sh install
```

The script will:
1. Install required packages
2. Configure audio system for real-time performance
3. Install professional audio plugins
4. Apply visual themes and fonts
5. Install miloApps
6. Configure user environment
7. Optimize system for audio production

**Reboot after installation to apply all changes.**

---

## What's Included

### Audio Plugins
- **LSP Plugins** - 200+ professional effects and processors
- **Calf Studio Gear** - Vintage-style effects
- **x42-plugins** - Professional meters and analyzers
- **Zam Plugins** - Mixing and mastering tools
- **ZynAddSubFX** - Powerful synthesis engine
- **Yoshimi** - Advanced software synthesizer
- **Hydrogen** - Professional drum machine
- **Guitarix** - Guitar amplifier and effects
- **Dragonfly Reverb** - High-quality reverbs
- **Ardour** - Professional DAW

### Multimedia Applications
- **Audacious** - Lightweight music player
- **VLC** - Universal media player
- **GIMP** - Image editing
- **Shotcut** - Video editing
- **DigiKam** - Photo management

### System Tools
- **qpwgraph** - PipeWire graph manager
- **GParted** - Partition manager
- **BleachBit** - System cleaner
- **Font Manager** - Typography management

---

## Technical Details

### Audio Optimization
```bash
# Kernel parameters
preempt=full nohz_full=all mitigations=off

# System limits
@audio rtprio=99 memlock=unlimited nice=-20

# Sysctl tuning
vm.swappiness=10 fs.inotify.max_user_watches=524288
```

### Base System
- **OS**: Debian 13 (Trixie)
- **Desktop**: XFCE4
- **Audio**: PipeWire 1.4+ with JACK compatibility
- **Display Manager**: SLiM
- **Theme**: miloOS custom GTK theme
- **Icons**: WhiteSur-light
- **Fonts**: San Francisco Pro

---

## Development Status

**Current: Beta**
- ✅ Core system complete
- ✅ Audio optimization implemented
- ✅ Visual theming finished
- ✅ AudioConfig ready
- ⏳ Additional miloApps in development
- ⏳ Documentation in progress

---

## Support & Community

- **Issues**: [GitHub Issues](https://github.com/Wamphyre/miloOS-core/issues)
- **Discussions**: [GitHub Discussions](https://github.com/Wamphyre/miloOS-core/discussions)
- **Donations**: [Ko-fi](https://ko-fi.com/wamphyre94078) ☕

If you find miloOS useful, consider supporting its development!

---

## License

GNU General Public License v3.0 - See [LICENSE](LICENSE)

### Third-Party Components
- Debian: Various licenses
- XFCE4: GPL-2.0
- PipeWire: MIT
- San Francisco Pro: Apple (personal use)
- WhiteSur Icons: GPL-3.0

---

## Credits

**Created by Wamphyre**

Special thanks to:
- Debian Project
- XFCE Team
- PipeWire Developers
- Linux Audio Community

---

**miloOS - Professional Audio Production Made Simple.**

*Transform Debian into a professional audio workstation in minutes.*
