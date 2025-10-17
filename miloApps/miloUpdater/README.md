# miloUpdater

Simple and elegant system updater for miloOS.

## Features

- Check for system updates (`apt update`)
- Install system updates (`apt upgrade`)
- Clean terminal output interface
- Bilingual support (English/Spanish)
- PolicyKit integration for secure privilege escalation

## Installation

Run as root:

```bash
sudo bash install.sh
```

## Usage

Launch from:
- Applications menu → System → System Updater
- Command line: `miloupdate`

## Requirements

- Python 3
- GTK 3
- VTE 2.91 (terminal widget)
- PolicyKit
- APT package manager

## How it works

1. Click "Check for Updates" to run `apt update`
2. If updates are available, the "Install Updates" button becomes active
3. Click "Install Updates" to run `apt upgrade -y`
4. All operations use PolicyKit for secure authentication

## Files

- `miloupdate.py` - Main application
- `miloupdate.desktop` - Desktop entry
- `org.milos.updater.policy` - PolicyKit policy
- `install.sh` - Installation script
