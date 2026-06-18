# miloThemeDaemon

`miloThemeDaemon` keeps the native miloOS shell in sync with the selected
`miloOS` or `miloOS-Dark` theme.

It is a small Python/GLib daemon that listens to Xfconf changes and updates the
related GTK, xfwm4, icon-theme, miloDock, and miloPanel settings without
restarting the panel or dock.

## Features

- Watches the `xsettings` and `xfwm4` Xfconf channels.
- Keeps `/Net/ThemeName`, `/general/theme`, and `/Net/IconThemeName` aligned.
- Maps `miloOS` to `WhiteSur-light`.
- Maps `miloOS-Dark` to `WhiteSur-dark`.
- Writes `system_theme` hints to `~/.config/miloDock/settings.ini` and `~/.config/miloPanel/settings.ini`.
- Sends `SIGUSR1` to running `milodock` and `milopanel` processes when their theme hint changes.
- Debounces `xfdesktop --reload` so desktop styling refreshes without heavy session churn.
- Uses a lock file at `~/.cache/milo-theme-daemon.lock` to keep a single instance.

## Session Integration

The daemon is launched by the miloOS XFCE session client list, not by a normal
per-user autostart entry. The installer removes legacy autostart files from:

- `/etc/skel/.config/autostart/milo-theme-daemon.desktop`
- `~/.config/autostart/milo-theme-daemon.desktop`

The intended shell startup model is:

1. `xfdesktop` starts first so the desktop/wallpaper is ready.
2. `milo-theme-daemon`, `milopanel --replace`, and `milodock --replace` start in the same shell priority group.

`xfce4-panel` is not part of the miloOS shell and the daemon does not modify
`xfce4-panel` settings.

## Run

```bash
milo-theme-daemon
```

For source-tree testing:

```bash
python3 milo-theme-daemon.py
```

## Install

```bash
sudo ./install.sh
```

The installer adds:

- `/usr/local/bin/milo-theme-daemon`

It also stops old daemon instances and starts the installed daemon as the real
user that invoked sudo.

## Dependencies

- Python 3
- PyGObject / `python3-gi`
- GLib introspection bindings
- Xfconf introspection bindings
- `xfdesktop`
- `pgrep`
