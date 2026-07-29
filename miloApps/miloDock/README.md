# miloDock

`miloDock` is the native miloOS dock. The installed app is now the C++/GTK implementation in `src/`; `milodock.py` remains in the tree as a Python reference while the dock continues to be refined.

## Goals

- Provide a dock designed specifically for miloOS.
- Stay lightweight, theme-aware, and easy to debug.
- Keep launchers in miloDock-owned configuration paths only.

## Current Features

- C++17/GTK3 dock window positioned at the bottom center of the primary monitor.
- Compact miloOS light/dark styling with rounded top corners, squared bottom edge, and no floating capsule gap.
- Reads `.dockitem` launchers from miloDock configuration paths.
- Launches apps through `GDesktopAppInfo`.
- Uses X11/EWMH to detect running windows, show indicators, activate existing windows, and close windows from the context menu.
- Shows running applications even when they are not pinned to the dock, when their `.desktop` launcher can be matched.
- Resolves running miloPKG AppImages through their process identity and locally registered desktop metadata, including embedded icons and pinning.
- Shows a temporary running-window item when an unpinned app cannot be matched to a `.desktop` launcher, so unknown apps are still visible and activatable.
- Left-click activates an existing window or launches the app; middle-click opens a new app instance.
- Drag launchers inside the dock to reorder them.
- Drop `.desktop` launchers onto the dock to add them.
- Global Menu actions for icon size, launcher spacing, auto-hide, effect mode, theme mode, preferences, and reload.
- Supported effect modes are `magnify` and `none`.
- Auto-hide fully hides the dock and only acts when a maximized/fullscreen window or another window overlaps the dock area.
- Magnify reserves its maximum launcher size so the dock does not jump while hovering icons.
- Window tracking is sampled once per refresh cycle and reused for running-app list updates, indicators, and auto-hide decisions.
- Normalizes its process working directory to the user's home folder so terminal launchers do not inherit the repository path during development.
- Right-click menu supports open, open new window, close windows, add/remove launcher, preferences, and reload dock.
- Preferences are stored in `~/.config/miloDock/settings.ini`.

## Launcher Lookup Order

1. `~/.config/miloDock/launchers`
2. `/etc/xdg/miloDock/launchers`
3. Repo development launchers under `configurations/miloDock/launchers`

If no launcher config exists, miloDock falls back to common miloOS apps such as miloFiles, Xfce Terminal, Mousepad, and Firefox ESR when their desktop files are installed.

## Run from Source

```bash
cd src
make
./milodock-bin
```

The Python reference can still be run for comparison:

```bash
python3 milodock.py --replace
```

## Install

```bash
sudo ./install.sh
```

The installer builds the C++ source and adds:

- `/usr/local/bin/milodock`
- `/usr/share/applications/milodock.desktop`
- `/usr/share/icons/hicolor/scalable/apps/milodock.svg`
- `/etc/xdg/miloDock/launchers/*.dockitem`
- `/etc/xdg/miloDock/settings.ini`

## Dependencies

- C++17 compiler
- GTK 3 development headers
- GIO/GDesktopAppInfo
- X11 development headers

Debian packages:

```bash
sudo apt install build-essential pkg-config libgtk-3-dev libx11-dev
```

## Theme Integration

`miloDock` follows the active GTK theme by default. `miloThemeDaemon` writes the current system theme hint to `~/.config/miloDock/settings.ini` so the dock can reload light/dark CSS without a full restart.

Relevant settings:

```ini
[Dock]
icon_size = 38
launcher_spacing = 3
auto_hide = false
effect = magnify
theme = auto
system_theme = light
```

Default visual values:

- Top radius: `32px`
- Border width: `1px`
- Light fill: `141,141,141,200` to `241,241,241,220`
- Dark fill: `20,20,20,210` to `35,35,35,230`
- Launcher spacing: `3px`
