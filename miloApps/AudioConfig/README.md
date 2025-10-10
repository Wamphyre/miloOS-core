# AudioConfig

**macOS-inspired audio configuration tool for miloOS**

A beautiful, simple audio settings application designed following Steve Jobs' principles: intuitive, elegant, and powerful.

![AudioConfig](https://img.shields.io/badge/version-2.0-blue) ![Platform](https://img.shields.io/badge/platform-Linux-lightgrey) ![License](https://img.shields.io/badge/license-MIT-green)

## Philosophy

AudioConfig embodies the core principles of great design:
- **Instant feedback**: Changes apply immediately, no "Apply" button needed
- **Visual clarity**: macOS-inspired interface with clear device lists
- **Professional grade**: Full control over sample rate, buffer size, and audio format
- **Zero friction**: Three clicks or less to any setting

## Features

### Device Management
- **Output devices**: Visual list with radio-style selection
- **Input devices**: Separate list for microphones and line inputs
- **Instant switching**: Changes apply immediately when selected

### Audio Settings
- **Sample Rate**: 44.1kHz to 192kHz
- **Buffer Size**: 32 to 1024 samples (low-latency to high-stability)
- **Audio Format**: 16-bit, 24-bit, 32-bit, and 32-bit float
- **Real-time priority**: Automatic RT scheduling for professional audio

### User Experience
- **macOS-style interface**: Clean, familiar design
- **Bilingual**: Automatic English/Spanish based on system locale
- **Instant apply**: No confirmation dialogs, changes happen immediately
- **Visual feedback**: Active devices clearly marked

## Requirements

- Python 3.6+
- GTK 3
- PipeWire
- PulseAudio compatibility layer (pactl)

```bash
sudo apt install python3-gi gir1.2-gtk-3.0 pipewire pipewire-pulse
```

## Installation

### Automatic (via miloOS installer)
AudioConfig is automatically installed with miloOS Core.

### Manual Installation
```bash
cd AudioConfig
sudo ./install.sh
```

This installs:
- `/usr/local/bin/audio-config` - Main application
- `/usr/share/applications/audio-config.desktop` - Desktop entry
- `/usr/share/icons/hicolor/scalable/apps/audio-config.svg` - Application icon

## Usage

### From Applications Menu
Settings → Audio Configuration

### From Terminal
```bash
audio-config
```

### Quick Actions
1. **Change output device**: Click on device in Output list
2. **Change input device**: Click on device in Input list
3. **Adjust latency**: Select buffer size from dropdown
4. **Change quality**: Select sample rate from dropdown

## Configuration Files

AudioConfig creates and manages:

```
~/.config/pipewire/pipewire.conf.d/99-custom.conf
```

This file contains:
- Sample rate settings
- Buffer size (quantum) settings
- Real-time priority configuration
- Audio format preferences

## Technical Details

### Sample Rates
- **44100 Hz**: CD quality, compatible
- **48000 Hz**: Professional standard (default)
- **88200 Hz**: High-resolution audio
- **96000 Hz**: Studio quality
- **192000 Hz**: Ultra high-resolution

### Buffer Sizes
- **32-64 samples**: Ultra-low latency (< 2ms) - for live performance
- **128-256 samples**: Low latency (3-6ms) - for recording (default)
- **512-1024 samples**: High stability (10-20ms) - for playback

### Audio Formats
- **S16LE**: 16-bit signed integer (CD quality)
- **S24LE**: 24-bit signed integer (professional)
- **S32LE**: 32-bit signed integer (maximum precision)
- **F32LE**: 32-bit floating point (studio standard)

## Design Principles

Following Steve Jobs' philosophy:

1. **Start with the user experience**: The interface feels natural and familiar
2. **Simplify relentlessly**: No unnecessary options or buttons
3. **Instant feedback**: See and hear changes immediately
4. **Beautiful details**: Custom icons, proper spacing, macOS-style lists
5. **3-click rule**: Any setting is accessible in 3 clicks or less

## Troubleshooting

### Changes not applying
```bash
systemctl --user restart pipewire pipewire-pulse
```

### No devices showing
```bash
pactl list sinks
pactl list sources
```

### Reset to defaults
```bash
rm ~/.config/pipewire/pipewire.conf.d/99-custom.conf
systemctl --user restart pipewire
```

## Uninstall

```bash
sudo rm /usr/local/bin/audio-config
sudo rm /usr/share/applications/audio-config.desktop
sudo rm /usr/share/icons/hicolor/scalable/apps/audio-config.svg
sudo update-desktop-database /usr/share/applications/
sudo gtk-update-icon-cache -f /usr/share/icons/hicolor
```

## Development

### Architecture
- **Language**: Python 3
- **GUI Framework**: GTK 3
- **Audio Backend**: PipeWire via pactl
- **Design Pattern**: Event-driven with immediate application

### Contributing
Contributions welcome! Please maintain the design philosophy:
- Keep it simple
- Make it beautiful
- Ensure instant feedback
- Follow macOS design language

## Credits

- **Design inspiration**: macOS System Preferences
- **Philosophy**: Steve Jobs' principles of great design
- **Part of**: miloOS Core

## License

MIT License - See LICENSE file for details

---

*"Design is not just what it looks like and feels like. Design is how it works."* - Steve Jobs
