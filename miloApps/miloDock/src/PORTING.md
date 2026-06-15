# miloDock C++ Port

This directory contains the native C++17/GTK3 implementation of `miloDock`.

The Python implementation in `../milodock.py` remains as a reference while the native version is refined. The C++ code is split by responsibility:

- `main.cpp`: process setup, locale, GTK application lifecycle, Global Menu actions, home-directory cwd normalization.
- `settings.*`: reads and writes `~/.config/miloDock/settings.ini` and reads `/etc/xdg/miloDock/settings.ini`.
- `launcher.*`: reads `.dockitem` launchers and launches `.desktop` apps.
- `window_tracker.*`: reads X11/EWMH window state, activation, close requests, and workspace data.
- `dock_window.*`: GTK dock window, CSS, launcher rendering, reordering, DnD, running indicators, context menu, preferences, and auto-hide.

Feature baseline:

- Loads pinned launchers from miloDock config.
- Launches apps through `GDesktopAppInfo`.
- Shows running indicators and unpinned running apps.
- Activates existing windows before launching new ones.
- Supports launcher reordering, launcher add/remove, and dropping `.desktop` files.
- Supports icon size, launcher spacing, auto-hide, effect, and theme preferences.
- Normalizes process cwd to the user's home directory so terminal launchers do not inherit the repository path.
