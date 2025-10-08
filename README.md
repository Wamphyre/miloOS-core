# miloOS

![miloOS Desktop](miloOS-desktop.png)

**A professional multimedia production Linux distribution.**

miloOS is a complete Debian 13 (Trixie) based operating system optimized for audio production, music creation, and multimedia content creation. Built from the ground up with real-time performance, low-latency audio, and a clean, elegant interface designed for creative professionals.

---

## ⚠️ Important Notice

**This repository contains the core configuration and build system for miloOS distribution.**

- ✅ **Intended for**: Building miloOS ISO images and development
- ❌ **NOT intended for**: Applying to existing production systems
- ⚠️ **Warning**: These scripts modify system files, bootloader, display manager, and core configurations

**Do NOT run these scripts on your production Debian system.** They are designed to build a complete distribution from scratch, not to transform an existing installation.

### Official ISO Releases

Pre-built ISO images will be released when ready. Follow the project for updates:
- **GitHub Releases**: Coming soon
- **Website**: Coming soon
- **Documentation**: Coming soon

---

## Project Vision

miloOS aims to be the **definitive Linux distribution for creative professionals** who demand professional-grade audio performance, elegant design, and a distraction-free workflow.

### Why miloOS?

**For Audio Professionals:**
- Professional audio performance out-of-the-box
- Full JACK compatibility without configuration
- Real-time optimizations for low-latency work
- Complete suite of professional plugins included
- No configuration needed - just create

**For Creative Professionals:**
- Clean, elegant interface that stays out of your way
- Optimized for multimedia production workflows
- Professional typography and visual design
- Intuitive system management
- Focus on your work, not the system

**For Linux Users:**
- Debian stability and vast package ecosystem
- XFCE4 performance with custom refinements
- Modern PipeWire audio stack
- Open source and transparent
- Community-driven development
- No vendor lock-in or forced obsolescence

---

## Features

### 🎨 Visual Experience

**Clean, Professional Interface**
- Top panel with integrated application menu
- Plank dock for quick application access
- Streamlined window controls (Close, Minimize, Maximize)
- Hidden window titles for maximum workspace
- Desktop icons aligned top-right, vertical layout

**Typography & Theming**
- San Francisco Pro system-wide fonts for clarity
- Custom miloOS GTK theme with professional blue accent (#007AFF)
- WhiteSur icon theme for visual consistency
- Optimized font rendering for comfortable long sessions
- Custom SLiM login theme

**Visual Polish**
- Curated wallpaper collection
- Smooth, subtle animations
- Consistent color scheme throughout
- Professional, distraction-free appearance

### 🎵 Audio Production Ready

**Real-Time Optimized System**
- Kernel parameters tuned for professional audio
- Lower latency than standard distributions
- Optimized scheduler for audio workloads
- Real-time priority for audio processes
- Zero-configuration audio setup

**PipeWire Audio Stack**
- Pre-configured for low-latency audio production
- Sample rates: 44.1kHz to 192kHz
- Buffer sizes: 32 to 1024 samples
- Pro-audio profile by default
- Automatic device configuration

**JACK Compatibility**
- Full JACK support without wrappers
- Works with Reaper, Ardour, Bitwig, Carla out-of-the-box
- Automatic library path configuration (.profile, .xsession, .bashrc, environment.d)
- Seamless integration with PipeWire
- No `pw-jack` wrapper needed - just launch your DAW from anywhere
- Works from terminal and application menu

**Professional Audio Plugins Included**
- **LSP Plugins** - Complete professional suite (200+ plugins)
- **Calf Studio Gear** - Vintage-style effects and processors
- **x42-plugins** - Professional meters and analyzers
- **Zam Plugins** - Mixing and mastering tools
- **Yoshimi** - Advanced software synthesizer
- **ZynAddSubFX** - Powerful synthesis engine
- **Hydrogen** - Advanced drum machine
- **DrumGizmo** - Realistic drum sampler
- **Guitarix** - Guitar amplifier and effects
- **Dragonfly Reverb** - High-quality reverbs
- **EQ10Q** - Parametric equalizer
- **qpwgraph** - PipeWire graph manager
- **Ardour** - Professional DAW

**Real-Time Kernel Parameters**
```
preempt=full        # Fully preemptible kernel
nohz_full=all       # No timer ticks on all CPUs
threadirqs          # Threaded interrupt handlers
mitigations=off     # Disabled for maximum performance
```

**System Tuning**
- CPU governor: performance mode
- Real-time priority for audio group (rtprio 99)
- Unlimited locked memory for audio applications
- Optimized I/O scheduler
- Memory management tuning (swappiness, dirty ratios)
- Proactive compaction disabled

**Audio Group Configuration**
- Automatic user setup with real-time privileges
- Nice level -20 for audio processes
- Unlimited file descriptors
- Proper security limits

### 🖥️ System Integration

**Complete Rebranding**
- System identification: miloOS (not Debian)
- Custom GRUB bootloader configuration
- Login banners and MOTD
- LSB release information
- OS release files

**Power Management**
- Custom bilingual dialogs (English/Spanish)
- Shutdown, restart, suspend, logout
- Polkit integration (no password prompts)
- Zenity-based confirmation dialogs
- Seamless user experience

**Display Manager**
- SLiM lightweight login manager
- Custom "milk" theme
- Fast boot times
- Minimal resource usage

**Menu System**
- Custom miloOS system menu
- Separate application menu
- Clean organization
- No duplicate system actions

### 📦 miloApps Suite

Custom applications designed specifically for miloOS:

#### AudioConfig
Professional audio server configuration tool:
- **Sample Rate**: 44.1kHz, 48kHz, 88.2kHz, 96kHz, 192kHz
- **Buffer Size**: 32, 64, 128, 256, 512, 1024 samples
- **Audio Format**: S16LE (16-bit), S24LE (24-bit), S32LE (32-bit), F32LE (32-bit float)
- **Global Configuration**: Controls PipeWire/JACK server parameters
- **Automatic Restart**: Applies changes and restarts PipeWire
- **Bilingual**: English and Spanish interface
- **macOS-Inspired UI**: Clean, simple, elegant

*Note: Device selection is done through XFCE audio plugin. AudioConfig controls only server parameters.*

#### More miloApps Coming Soon
- System monitor
- Backup utility
- Network manager
- And more...

---

## Technical Specifications

### Base System
- **Distribution**: Debian 13 (Trixie)
- **Desktop Environment**: XFCE4
- **Display Manager**: SLiM
- **Audio Server**: PipeWire 1.4+
- **Session Manager**: WirePlumber
- **Init System**: systemd

### Audio Stack
- **PipeWire**: Low-latency audio server
- **PipeWire-JACK**: JACK compatibility layer
- **PipeWire-Pulse**: PulseAudio compatibility
- **WirePlumber**: Session and policy manager
- **RTKit**: Real-time scheduling

### Themes & Icons
- **GTK Theme**: miloOS (custom)
- **Icon Theme**: WhiteSur-light
- **Window Manager Theme**: miloOS (custom)
- **Plank Theme**: milo (custom)
- **Fonts**: San Francisco Pro (Display, Text, Mono)

### Kernel Parameters
```bash
GRUB_CMDLINE_LINUX_DEFAULT="preempt=full nohz_full=all threadirqs mitigations=off"
```

### System Limits
```
@audio   -  rtprio     99
@audio   -  memlock    unlimited
@audio   -  nice      -20
@audio   -  nofile     524288
```

### Sysctl Tuning
```
vm.swappiness = 10
fs.inotify.max_user_watches = 524288
kernel.shmmax = 2147483648
fs.file-max = 524288
```

---

## System Requirements

### Minimum Requirements
- **CPU**: 64-bit processor (x86_64)
- **RAM**: 2GB
- **Storage**: 20GB free disk space
- **Graphics**: 1024x768 resolution
- **Network**: Internet connection for installation

### Recommended Requirements
- **CPU**: Multi-core processor (4+ cores)
- **RAM**: 8GB or more
- **Storage**: 50GB SSD
- **Graphics**: 1920x1080 or higher
- **Audio**: Professional audio interface (optional)

### Supported Hardware
- Most modern x86_64 computers
- Intel and AMD processors
- NVIDIA, AMD, and Intel graphics
- USB and PCIe audio interfaces
- MIDI controllers and devices

---

## Why Choose miloOS?

### Freedom & Ownership
- ✅ Complete control over your system
- ✅ No forced upgrades or planned obsolescence
- ✅ Full system access and customization
- ✅ No vendor lock-in
- ✅ Open source transparency

### Professional Performance
- ✅ Optimized for low-latency audio production
- ✅ Efficient resource utilization
- ✅ Real-time kernel parameters
- ✅ No background telemetry or bloat
- ✅ Stable and reliable for production work

### Cost Effective
- ✅ Free and open source
- ✅ No hardware restrictions
- ✅ Run on any compatible x86_64 hardware
- ✅ No subscription fees
- ✅ Community-driven support

### Complete Ecosystem
- ✅ Professional audio plugins included
- ✅ Windows VST support (via Yabridge)
- ✅ Full JACK ecosystem compatibility
- ✅ LV2, LADSPA, VST plugin support
- ✅ Vast Debian package repository

### Audio-First Design
- ✅ Pre-configured for professional audio
- ✅ Real-time optimizations out-of-the-box
- ✅ PipeWire with JACK compatibility
- ✅ Professional tools included
- ✅ Zero-configuration workflow

### Polished Experience
- ✅ Clean, professional interface
- ✅ Consistent design language
- ✅ Custom applications (miloApps)
- ✅ Attention to detail
- ✅ Distraction-free workflow

### Debian Foundation
- ✅ Rock-solid base system
- ✅ Extensive package repository
- ✅ Long-term support
- ✅ Regular security updates
- ✅ Proven reliability

### Ready to Use
- ✅ No configuration needed
- ✅ Works immediately after install
- ✅ Sensible defaults
- ✅ Professional appearance
- ✅ Optimized performance

---

## Included Software

miloOS comes with professional audio software pre-installed:

### Audio Plugins (Included)
- ✅ **LSP Plugins** - 200+ professional plugins
- ✅ **Calf Studio Gear** - Vintage effects
- ✅ **x42-plugins** - Meters and analyzers
- ✅ **Zam Plugins** - Mixing tools
- ✅ **Yoshimi** - Software synthesizer
- ✅ **ZynAddSubFX** - Synthesis engine
- ✅ **Hydrogen** - Drum machine
- ✅ **DrumGizmo** - Drum sampler
- ✅ **Guitarix** - Guitar amp/effects
- ✅ **Dragonfly Reverb** - Quality reverbs
- ✅ **EQ10Q** - Parametric EQ
- ✅ **Ardour** - Professional DAW

### Utilities (Included)
- ✅ **AudioConfig** - miloOS audio configuration tool
- ✅ **qpwgraph** - PipeWire graph manager
- ✅ **XFCE Audio Plugin** - Volume and device control

### Recommended Additional Software

**Digital Audio Workstations**
- **Reaper** - Professional, affordable, cross-platform
- **Bitwig Studio** - Modern, innovative
- **Qtractor** - Lightweight, MIDI-focused
- **LMMS** - Free, beginner-friendly

**Additional Tools**
- **Carla** - Universal plugin host (install separately)
- **Yabridge** - Windows VST bridge for Linux
- **Helvum** - Alternative PipeWire graph manager
- **Audacity** - Simple audio editing

---

## Development Status

### Current Status: Alpha
- ✅ Core system configuration complete
- ✅ Liquorix real-time kernel integration
- ✅ Audio optimization implemented
- ✅ Professional audio plugins included
- ✅ JACK compatibility configured
- ✅ Visual theming finished
- ✅ AudioConfig application ready
- ⏳ ISO build system in progress
- ⏳ Additional miloApps in development
- ⏳ Documentation being written

### Roadmap

**Phase 1: Foundation (Current)**
- [x] Base system configuration
- [x] Audio optimization
- [x] Visual theming
- [x] AudioConfig application
- [ ] ISO build system
- [ ] Installation wizard

**Phase 2: Polish**
- [ ] Additional miloApps
- [ ] Complete documentation
- [ ] User testing
- [ ] Bug fixes
- [ ] Performance tuning

**Phase 3: Release**
- [ ] Beta ISO release
- [ ] Community feedback
- [ ] Stable release
- [ ] Website launch
- [ ] Marketing materials

**Phase 4: Growth**
- [ ] Plugin marketplace
- [ ] Cloud sync features
- [ ] Mobile companion app
- [ ] Professional support options
- [ ] Hardware partnerships

---

## License

miloOS Core is released under the **GNU General Public License v3.0**.

See [LICENSE](LICENSE) file for details.

### Third-Party Components
- Debian: Various licenses (mostly GPL, LGPL, BSD)
- XFCE4: GPL-2.0
- PipeWire: MIT
- San Francisco Pro Fonts: Apple (for personal use)
- WhiteSur Icons: GPL-3.0

---

## Credits

### Created By
**Wamphyre** - Project founder and lead developer

### Special Thanks
- Debian Project - Solid foundation
- XFCE Team - Lightweight desktop environment
- PipeWire Developers - Modern audio stack
- Linux Audio Community - Tools and support
- All contributors and testers

### Inspiration
- Professional audio workstations and workflows
- Ubuntu Studio - Audio-focused distribution
- KXStudio - Professional audio tools
- Clean, minimal design principles

---

## Disclaimer

**miloOS is an independent open source project.**

- San Francisco Pro fonts are property of Apple Inc. and used under their license terms
- miloOS is an original Linux distribution with its own design philosophy
- No proprietary code or assets from third parties are included
- All trademarks belong to their respective owners

**Use at your own risk.** While miloOS is designed for stability and reliability, the developers are not responsible for any data loss, hardware damage, or other issues that may arise from using this software.

---

## FAQ

**Q: When will the ISO be available?**  
A: We're working on it! Follow the project for updates.

**Q: Can I install this on my existing Debian system?**  
A: No, these scripts are designed for building the distribution, not transforming existing systems.

**Q: Will my audio interface work?**  
A: Most USB and PCIe audio interfaces work with Linux. Check compatibility before installing.

**Q: Can I run Windows plugins?**  
A: Yes, using Yabridge for VST2/VST3 plugins.

**Q: Is this better than Ubuntu Studio?**  
A: Different focus. miloOS prioritizes macOS-like experience and out-of-the-box audio optimization.

**Q: How can I help?**  
A: Test, report bugs, contribute code, write documentation, or spread the word!

---

**miloOS - Professional Audio Production, Beautiful Design, Complete Freedom.**

*Built with ❤️ for audio professionals who demand more.*
