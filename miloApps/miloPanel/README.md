# miloPanel

`miloPanel` is the native C++17/GTK3 top panel for miloOS. It replaces `xfce4-panel` in the miloOS-Core session while XFCE still provides the session, window manager, settings daemon, and desktop manager.

## Features

- Native top dock window with X11 `_NET_WM_STRUT_PARTIAL` space reservation.
- Full-width `24px` miloOS panel layout with `16px` status icons.
- miloOS menu button backed by `/etc/xdg/menus/milo.menu`.
- Global menu host for AppMenu/DBusMenu/GMenu exported menus.
- GTK global-menu environment configured to hide local application menubars and avoid duplicate menus.
- Real application display names resolved from `.desktop` metadata when available.
- Desktop fallback menu labeled as `Escritorio` when `xfdesktop` or the root desktop is active.
- Filtered system-services tray host for XEmbed and StatusNotifier/AppIndicator icons.
- App status icons such as media players are ignored; the tray is reserved for system-service/hardware indicators.
- Native battery indicator shown only when `/sys/class/power_supply` exposes a real battery, placed between the tray and volume controls.
- Legacy XEmbed tray sockets repaint their native X11 background when the panel theme changes.
- PulseAudio/PipeWire output and input volume controls through `pactl`.
- Clock using `%a %d, %R`, tooltip `%A %d %B %Y`, font `SF Pro Text Medium 10`.
- Notification launcher for `xfce4-notifyd-config`.
- Light/dark styling synchronized by `miloThemeDaemon`.
- Single-instance behavior through `--replace`.

## Tray Policy

The tray intentionally does not behave like a generic notification area. `miloPanel`
only accepts system-service and hardware indicators, including NetworkManager,
Bluetooth, removable-device, and similar status providers. Battery state is
handled by the native battery indicator instead of accepting external power
manager tray icons.

Both legacy XEmbed icons and StatusNotifier/AppIndicator items are filtered. SNI
items in `ApplicationStatus` and `Communications` categories are rejected unless
they match an explicit system-service allowlist. This keeps ordinary running app
status icons out of the top panel.

When the theme changes, the panel reapplies the computed CSS background to each
legacy XEmbed socket and plug window so indicators such as NetworkManager do not
keep the old dark/light slot color.

## Session Integration

miloOS-Core starts the native shell through XFCE session clients:

- `xfdesktop` starts before the panel so the wallpaper is ready when the bar appears.
- `milo-theme-daemon`, `milopanel --replace`, and `milodock --replace` start in the same session priority group.
- Panel and dock autostart `.desktop` files are installed only as disabled overrides.

`xfce4-panel` is stopped during configuration, removed from the failsafe session, and old saved XFCE session files are moved aside so it is not restored on login.

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

The panel imports DBusMenu and GMenu models directly. Active-window and menu
change notifications are coalesced into the next GTK main-loop cycle, so the
global menu follows focus changes without a fixed delay. For compatibility
testing, the external `vala-panel-appmenu` widget can still be selected with
`MILO_PANEL_USE_EXTERNAL_APPMENU=1`.

## Theme Integration

`miloThemeDaemon` writes the current system theme hint to
`~/.config/miloPanel/settings.ini` and sends `SIGUSR1` to the running `milopanel`
process. On reload, miloPanel updates CSS, menu logo, clock/volume widgets, and
legacy tray backgrounds without a full panel restart.

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

The default settings are:

```ini
[Panel]
height=24
icon_size=16
reserve_space=true
theme=auto
system_theme=light
logo_light=/usr/share/themes/miloOS/logo.png
logo_dark=/usr/share/themes/miloOS-Dark/logo.png
menu_file=/etc/xdg/menus/milo.menu
clock_format=%a %d, %R
clock_tooltip_format=%A %d %B %Y
clock_font=SF Pro Text Medium 10
```

## Dependencies

```bash
sudo apt install build-essential pkg-config libgtk-3-dev libx11-dev appmenu-gtk3-module vala-panel-appmenu xfce4-notifyd pulseaudio-utils pavucontrol
```
