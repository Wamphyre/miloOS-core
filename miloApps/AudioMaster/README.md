# AudioMaster

**Professional audio mastering tool for miloOS**

A beautiful, simple audio mastering application powered by Matchering AI, designed following Steve Jobs' principles: intuitive, elegant, and powerful.

![AudioMaster](https://img.shields.io/badge/version-1.0-blue) ![Platform](https://img.shields.io/badge/platform-Linux-lightgrey) ![License](https://img.shields.io/badge/license-MIT-green)

## Philosophy

AudioMaster embodies the core principles of great design:
- **Simplicity first**: Three file selections, one button to master
- **Professional results**: AI-powered mastering using Matchering
- **Visual clarity**: Clean interface with clear feedback
- **Zero friction**: From selection to mastered audio in seconds

## Features

### Audio Mastering
- **Reference-based mastering**: Use any professional track as reference
- **AI-powered processing**: Matchering analyzes and applies mastering
- **Multiple format support**: WAV, FLAC, MP3, OGG, M4A
- **High-quality output**: Professional-grade mastered audio

### User Experience
- **macOS-style interface**: Clean, familiar design
- **Bilingual**: Automatic English/Spanish based on system locale
- **Progress feedback**: Visual progress during mastering
- **Auto-install**: Matchering installs automatically if needed

## What is Matchering?

Matchering is an open-source audio matching and mastering library that uses AI to:
1. Analyze the reference track's characteristics
2. Apply similar processing to your target track
3. Match loudness, EQ, stereo width, and dynamics
4. Produce professional-sounding results

## Requirements

- Python 3.6+
- GTK 3
- pip3 (for Matchering installation)

```bash
sudo apt install python3-gi gir1.2-gtk-3.0 python3-pip
```

Matchering and its dependencies will be installed automatically on first run.

## Installation

### Automatic (via miloOS installer)
AudioMaster can be installed with miloOS Core.

### Manual Installation
```bash
cd AudioMaster
sudo ./install.sh
```

This installs:
- `/usr/local/bin/audiomaster` - Main application
- `/usr/share/applications/audiomaster.desktop` - Desktop entry
- `/usr/share/icons/hicolor/scalable/apps/audiomaster.svg` - Application icon

## Usage

### From Applications Menu
AudioVideo → Audio Master

### From Terminal
```bash
audiomaster
```

### Workflow
1. **Select Reference Track**: Choose a professionally mastered track you want to match
2. **Select Target Track**: Choose the audio you want to master
3. **Select Output Location**: Choose where to save the mastered audio
4. **Click "Start Mastering"**: Wait for processing to complete

### Tips for Best Results

#### Choosing a Reference Track
- Use professionally mastered tracks in your genre
- Match the style and energy of your target track
- Avoid heavily compressed or distorted references
- Use high-quality files (WAV or FLAC preferred)

#### Preparing Your Target Track
- Use the highest quality source available
- Ensure proper gain staging (peaks around -6dB)
- Apply basic mixing before mastering
- Remove any limiting or mastering plugins

#### Output Format
- WAV: Uncompressed, highest quality (recommended)
- FLAC: Lossless compression, smaller file size

## Technical Details

### Matchering Process
1. **Analysis**: Both tracks are analyzed for:
   - Frequency spectrum
   - Loudness (LUFS)
   - Stereo width
   - Dynamic range
   
2. **Processing**: Target track is processed with:
   - EQ matching
   - Compression
   - Limiting
   - Stereo enhancement
   
3. **Output**: Final mastered track matching reference characteristics

### Processing Time
- Typical 3-4 minute song: 30-60 seconds
- Depends on CPU speed and file format
- Progress bar shows activity during processing

## Design Principles

Following Steve Jobs' philosophy:

1. **Start with the user experience**: Simple three-step workflow
2. **Simplify relentlessly**: No complex settings or options
3. **Instant feedback**: Progress indication and clear results
4. **Beautiful details**: Clean interface, proper spacing
5. **3-click rule**: Master any track in 3 clicks

## Troubleshooting

### Matchering installation fails
```bash
pip3 install --user matchering
```

### Processing takes too long
- Use WAV or FLAC files instead of compressed formats
- Ensure sufficient CPU resources available
- Close other audio applications

### Output quality issues
- Use higher quality reference tracks
- Ensure target track is properly mixed
- Try different reference tracks
- Check input file quality

### Python module not found
```bash
export PATH="$HOME/.local/bin:$PATH"
python3 -m matchering --help
```

## Uninstall

```bash
sudo rm /usr/local/bin/audiomaster
sudo rm /usr/share/applications/audiomaster.desktop
sudo rm /usr/share/icons/hicolor/scalable/apps/audiomaster.svg
sudo update-desktop-database /usr/share/applications/
sudo gtk-update-icon-cache -f /usr/share/icons/hicolor

# Optional: Remove Matchering
pip3 uninstall matchering
```

## Development

### Architecture
- **Language**: Python 3
- **GUI Framework**: GTK 3
- **Mastering Engine**: Matchering
- **Design Pattern**: Event-driven with threaded processing

### Contributing
Contributions welcome! Please maintain the design philosophy:
- Keep it simple
- Make it beautiful
- Ensure clear feedback
- Follow macOS design language

## Credits

- **Mastering Engine**: [Matchering](https://github.com/sergree/matchering)
- **Design inspiration**: macOS applications
- **Philosophy**: Steve Jobs' principles of great design
- **Part of**: miloOS Core

## License

MIT License - See LICENSE file for details

## References

- [Matchering Documentation](https://github.com/sergree/matchering)
- [Audio Mastering Basics](https://en.wikipedia.org/wiki/Audio_mastering)
- [LUFS Loudness Standards](https://en.wikipedia.org/wiki/LUFS)

---

*"Design is not just what it looks like and feels like. Design is how it works."* - Steve Jobs
