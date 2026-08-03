# miloFiles

`miloFiles` is the Finder-inspired file manager for **miloOS**. The original production-ready reference implementation is `milofiles.py`; the current native port lives in `src/` and is intended to preserve the Python app feature-for-feature as a pure C++ application.

The C++ version is built with **C++17**, **GTK+ 3**, **GIO**, and **GLib**. It keeps the same visual language as the Python app, with Snow Leopard-inspired structure blended into the flatter miloOS desktop style.

---

## Architecture

The native application is organized as a modular C++17 codebase:

| File | Description |
|------|-------------|
| `main.cpp` | Application entry point, locale setup, command-line path/URI handling, and GTK main loop |
| `app_window.hpp/cpp` | Main window, toolbar, menu bar, breadcrumbs, location entry, navigation, and server connection dialogs |
| `sidebar.hpp/cpp` | Devices, removable volumes, network mounts, Favorites, and Trash sidebar |
| `file_view.hpp/cpp` | Icon/list views, async directory loading, search filtering, context menus, drag and drop, and file actions |
| `progress_dialog.hpp/cpp` | Modal progress and cancellation UI for background operations |
| `utils.hpp/cpp` | Filesystem helpers, MIME handling, default open handlers, archive operations, mounts, and bookmarks |
| `i18n.hpp` | Header-only English/Spanish translation catalog |
| `Makefile` | Native build using `pkg-config` for `gtk+-3.0`, `gio-2.0`, and related libraries |

---

## Key Features

1. **Finder-inspired layout**
   - Sidebar sections for Devices, Favorites, network mounts, and Trash.
   - Compact toolbar with Back, Forward, Parent, breadcrumb navigation, view switcher, and live search.
   - Grid/icon view and details/list view with persistent UI behavior matching the Python reference.

2. **Interactive location bar**
   - Double-click the breadcrumb area, press `Ctrl+L`, or use the Go menu to type a path.
   - Supports local paths and remote URIs such as `smb://server/share`, `ftp://host`, and `sftp://host`.
   - `Enter` navigates or mounts the target; `Esc` restores the breadcrumb view.

3. **Devices, removable media, and network mounts**
   - Uses `GVolumeMonitor` to update mounted devices and removable volumes in real time.
   - Sidebar device actions include mount, unmount, and eject where supported.
   - Remote servers are mounted through GVfs and opened through their native GIO URI, for example `smb://server/share`.
   - Connected SMB shares can be added to Favorites and stay stored as GTK bookmark URIs.

4. **Archive compression and extraction**
   - Compress selected files or folders to `.zip`, `.7z`, `.tar.gz`, `.tar.xz`, or `.tar.bz2`.
   - Extract `.zip`, `.7z`, `.rar`, `.tar`, `.tar.gz`, `.tgz`, `.tar.bz2`, `.tbz2`, `.tar.xz`, `.txz`, and related archive formats.
   - Archive jobs run in background worker threads through `7z` or `tar`, with completion, cancellation, and error reporting kept out of the GTK UI thread.

5. **File operations**
   - Cut, copy, paste, rename, move to Trash, permanent delete, drag and drop, and properties.
   - Copy, cut, paste, rename, delete, and folder/file creation use GIO locations, so they work with local paths and connected GVfs/SMB shares.
   - Clipboard data is exported as `x-special/gnome-copied-files` and `text/uri-list`, allowing copy/cut/paste between separate miloFiles windows.
   - Drag and drop works both as a source and a destination using URI lists, including between different miloFiles instances.
   - Single and multi-item selections survive icon/list view switches and same-directory refreshes; `Ctrl+A` selects all and `Esc` clears the selection.
   - Drag payloads are frozen from the item where the gesture starts, so dragging one selected item or a multi-selection remains reliable even if GTK updates the visual selection during the gesture.
   - Folder drop targets are highlighted. Internal drags move by default, `Ctrl` forces copy, `Shift` forces move, and copying within the same folder creates smart duplicate names.
   - Smart duplicate-name handling when pasting into the same folder.
   - Directory size calculation in the properties dialog runs asynchronously.
   - Context menu action to open the current folder in `xfce4-terminal` when available.

6. **Favorites and Trash**
   - Favorites are stored in `~/.config/gtk-3.0/bookmarks`, so they stay compatible with GTK desktop apps.
   - Local directories and connected SMB/GVfs locations can be added to Favorites from the main file view.
   - Favorite rows can be reordered by dragging, renamed, or removed from the sidebar.
   - Trash can be emptied from the sidebar with confirmation.

7. **Default app handling and Open With**
   - miloPKG AppImages open directly on double-click and use their embedded `.DirIcon`.
   - AppImage icons and desktop metadata are extracted safely through `unsquashfs`, cached, and registered locally with valid launchers for application menus and miloDock integration, including AppImage filenames containing spaces.
   - Registered AppImages are exposed to the desktop's default-application selector, including XFCE preferred browser, mail, file manager, and terminal categories when supported by their metadata.
   - Desktop launchers and XFCE helpers are removed when their AppImage is deleted or moved to Trash; moving or renaming an AppImage recreates one launcher for its new location.
   - Re-registering or updating a miloPKG AppImage replaces launchers for the same Debian package instead of creating duplicate application-menu entries.
   - Always routes audio and video through VLC and images through Ristretto, resolving either Debian installations or miloPKG AppImage registrations before consulting generic MIME defaults.
   - Sends a multi-file selection to each handler as one launch request instead of starting one process per file.
   - VLC launches use file-manager activation plus explicit single-instance mode, so an existing player window is reused for later files and multi-file selections become one playlist handoff.
   - Falls back to registered system handlers through GIO/GTK when custom defaults are unavailable.
   - Context menus expose applications shared by all selected MIME types and launch the complete selection together; a native `GtkAppChooserDialog` remains available for choosing another app.

8. **Localization and theme integration**
   - English and Spanish UI strings are selected from the process locale.
   - Scoped GTK CSS keeps the miloFiles window aligned with the light and `miloOS-Dark` themes.
   - The app reacts when the active GTK theme changes during the session.

9. **Native performance**
   - Compiles to a single native binary with no Python interpreter dependency.
   - Uses thread-safe caches for icons, MIME icons, and thumbnails.
   - Directory loading, thumbnails, file operations, archive work, and metadata calculations avoid blocking the main GTK thread.

---

## Building from Source

### Prerequisites

- `make`
- `squashfs-tools`
- `g++` with C++17 support
- `pkg-config`
- Development headers for `gtk+-3.0`, `gio-2.0`, `gdk-pixbuf-2.0`, and `gio-unix-2.0`
- Runtime archive tools: `7z` and `tar`
- Runtime desktop helpers: GVfs backends for remote mounts, `udisks2` for removable devices, and optionally `xfce4-terminal`

### Compile

```bash
cd src/
make
```

This produces the `milofiles-bin` binary in the `src/` directory.

---

## Installation and Integration

To install `miloFiles` locally and register it as the default directory handler:

```bash
sudo ./install.sh
```

The installer:

- Builds the C++ source from `src/`.
- Installs the native binary as `/usr/local/bin/milofiles`.
- Installs `milofiles.desktop` into `/usr/share/applications/`.
- Installs the `milofiles` icon into the system icon theme.
- Registers `milofiles.desktop` as the default handler for `inode/directory`.

The miloDock launcher used by miloOS should point to:

```ini
Launcher=file:///usr/share/applications/milofiles.desktop
```

---

## Standalone Execution

Run the app directly from the build directory:

```bash
./src/milofiles-bin [optional-path-or-file-uri]
```

If no path is provided, `miloFiles` opens the user's home directory. Local filesystem paths and `file://` URIs are accepted; invalid startup paths fall back to the home directory.
