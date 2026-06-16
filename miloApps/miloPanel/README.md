# miloPanel

`miloPanel` is the native C++17/GTK3 top panel for miloOS. It replaces `xfce4-panel` in the miloOS-Core session while XFCE still provides the session, window manager, settings daemon, and desktop manager.

## Features

- Native top dock window with X11 `_NET_WM_STRUT_PARTIAL` space reservation.
- Full-width `24px` miloOS panel layout with `16px` status icons.
- miloOS menu button backed by `/etc/xdg/menus/milo.menu`.
- Global menu host for AppMenu/DBusMenu/GMenu exported menus.
- GTK global-menu environment configured to hide local application menubars and avoid duplicate menus.
- Real application display names resolved from `.desktop` metadata when available.
- Desktop fallback menu when `xfdesktop` or the root desktop is active.
- XEmbed system tray host.
- PulseAudio/PipeWire output and input volume controls through `pactl`.
- Clock using `%a %d, %R`, tooltip `%A %d %B %Y`, font `SF Pro Text Medium 10`.
- Notification launcher for `xfce4-notifyd-config`.
- Light/dark styling synchronized by `miloThemeDaemon`.
- Single-instance behavior through `--replace`.

## Session Integration

miloOS-Core starts `miloPanel` in two ways:

- XFCE failsafe session client: `milopanel --replace` replaces the old `xfce4-panel` client.
- User autostart entry: `~/.config/autostart/Panel.desktop`.

The `--replace` flag keeps this robust if both paths are present. `xfce4-panel` is stopped during configuration and is no longer part of the miloOS session startup.

## Global Menu

The panel expects the AppMenu stack installed by miloOS-Core:

- `appmenu-gtk3-module`
- `vala-panel-appmenu`
- `xfce4-appmenu-plugin`

The session config sets:

```text
/Gtk/ShellShowsMenubar = true
/Gtk/ShellShowsAppmenu = true
/Gtk/Modules = appmenu-gtk-module
org.appmenu.gtk-module always-show-inner-menu = false
```

`miloPanel` also normalizes `GTK_MODULES` for child applications so they inherit a single `appmenu-gtk-module` entry instead of duplicated module lists.

## Build

```bash
cd src
make
```

The Makefile tracks header dependencies with `-MMD -MP`, so changes to headers trigger the right rebuilds.

## Run

```bash
./milopanel-bin --replace
```

Use `--no-struts` only for temporary side-by-side debugging.

## Install

```bash
sudo ./install.sh
```

The installer adds:

- `/usr/local/bin/milopanel`
- `/usr/share/applications/milopanel.desktop`
- `/etc/xdg/miloPanel/settings.ini`

## Dependencies

```bash
sudo apt install build-essential pkg-config libgtk-3-dev libx11-dev appmenu-gtk3-module vala-panel-appmenu xfce4-notifyd pulseaudio-utils pavucontrol
```
