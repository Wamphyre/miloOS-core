# miloFiles

`miloFiles` is a custom, lightweight, Finder-inspired file manager developed for **miloOS**. It is designed with a hybrid visual aesthetic that sits at the intersection of classic macOS Snow Leopard (brushed glossy silver gradients) and the modern, flat design system of miloOS.

Under the hood, `miloFiles` is a Python 3 application built on top of **PyGObject (GTK+ 3)**, ensuring high performance, low resource utilization, and deep integration with Linux desktop standards.

---

## Key Features

1. **Classic Finder-Inspired Layout**:
   - **Double-Pane Sidebar**: Features structured lists for `DEVICES` (Home folder, Root file system, dynamically listed removable volumes/USB drives, and mounted network servers) and `FAVORITES` (Desktop, Documents, Downloads, Music, Pictures, Videos, and the Trash folder).
   - **Perfect Alignment**: Sidebar rows are horizontally aligned using a `Gtk.SizeGroup` on the icons to ensure labels and widgets start at the exact same column.
   - **Toolbar & View Switcher**: Compact header bar containing navigation buttons (Back, Forward, Parent directory), path breadcrumbs, a segmented control switcher for views, and a live search filter.

2. **Interactive Location/URL Bar with Autocomplete**:
   - Easily input paths by double-clicking the breadcrumbs area, using `Ctrl+L`, or choosing "Go to Location..." from the Go menu.
   - Built-in directory suggestions using `Gtk.EntryCompletion` pre-populated with default user folders.
   - Hitting `Enter` navigates to the input path; hitting `Esc` or losing focus reverts back to the breadcrumbs view.

3. **System-wide Global Menu Support**:
   - Fully integrates with XFCE4's Global Menu plugin (`appmenu-gtk3-module`). The traditional menu bar is automatically exported to DBus, hidden from the local window, and projected onto the panel at the top of the screen.

4. **Plug-and-Play Storage & Dynamic Mounts**:
   - Monitored by `Gio.VolumeMonitor`. Plugging in a USB flash drive, inserting a disc, or mounting a virtual filesystem immediately updates the sidebar in real time.
   - Built-in unmounting: An eject icon appears next to removable devices in the sidebar, allowing one-click safe removal (`gio mount -u`).

5. **Network Servers Integration**:
   - Connect directly to remote shared resources (Samba/SMB, FTP, SFTP) using the **"Connect to Server..."** action in the *File* menu, or by entering protocol URIs (e.g. `smb://192.168.1.50/share`, `ftp://foo.bar`) directly in the location bar and hitting Enter.
   - Successfully mounted connections automatically redirect the view pane to their local GVfs paths.
   - Uses GVfs backend mounts seamlessly. Authentication dialogs pop up natively when password-protected shares are queried.

6. **Built-in Archive Utilities (Zip & Tarball)**:
   - **Compression**: Select one or more files/folders, right-click, and choose **"Compress..."** to package them into `.zip` or `.tar.gz` archives.
   - **Extraction**: Right-click any archive file (`.zip`, `.tar.gz`, `.tgz`, `.bz2`, `.xz`, etc.) and select **"Extract Here"** to automatically unpack its contents.
   - **Security (Zip/Tar Slip prevention)**: Automatically sanitizes paths within compressed archives to prevent path traversal vulnerabilities.
   - Operations are processed in a background thread to prevent GUI lockups.

7. **File Operations & Properties**:
   - Basic functions: Cut, Copy, Paste, Rename, Move to Trash, and Permanent Delete.
   - **Keyboard Event Focus Bypass**: Standard text editing keys (such as `BackSpace`, `Delete`, and clipboard commands) propagate directly to text entry widgets instead of triggering global folder navigation commands when a text field is focused.
   - **Smart Clipboard Collision Resolution**: Pasting files/folders in the same directory automatically generates a unique duplicate name (e.g. 'file copy.txt'), preventing collisions and file descriptor errors.
   - **Asynchronous "Get Info" / Properties Dialog**: Displays deep metadata, owner details, and octal permissions. Directory size calculation is processed dynamically in a background thread with live updates, keeping the UI fully responsive.
   - **Drag and Drop (D&D)**: Supports dragging files into directory views to trigger standard GIO copy operations.

8. **Bilingual Localization**:
   - Natively localized in **English** and **Spanish**. The app reads the system locale on startup and translates all interface strings, dialogue boxes, context menus, and launcher labels accordingly.

9. **Theme Synchronization & Flat Borderless Styling**:
   - Automatically monitors active desktop settings. Switching theme to `miloOS-Dark` triggers a CSS provider swap, transitioning the layout from glossy-silver to space-grey.
   - Applies high-specificity GTK CSS overrides to strip default theme borders and scrollbar frames, preventing ugly white/light lines from showing up inside the card layout or around the window borders.

10. **Asynchronous Optimized Thumbnail Loader**:
    - Scans directory contents in background threads to retrieve system-cached thumbnails or scale local images on the fly, storing them in memory (`self.thumbnail_cache`) to guarantee high-performance UI rendering and lag-free folder navigation.
    - Dialog entries (Connect, Compress, Rename) automatically execute their actions when the Enter key is pressed.

---

## Installation & Integration

To install `miloFiles` locally and register it as the default system directory handler:

1. **Run the Installer**:
   ```bash
   sudo ./install.sh
   ```
   This script:
   - Creates a wrapper script in `/usr/local/bin/milofiles` pointing to the entrypoint.
   - Installs the app menu launcher (`milofiles.desktop`) and updates the system desktop database.
   - Copies the Finder-style `milofiles.svg` icon to the system-wide icon theme directories.
   - Configures mime-type defaults so that opening any directory in the system defaults to `miloFiles`.

---

## Standalone Execution

You can run the application directly from the source directory:
```bash
python3 milofiles.py [optional-directory-path]
```
If a path is provided (as a regular path or as a `file://` URI), the app will start navigated into that specific folder; otherwise, it defaults to the user's home directory.
