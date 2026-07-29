# miloPKG

`miloPKG` searches the APT repositories configured on a Debian/miloOS system and
turns the selected application package into a portable AppImage. It never
installs the selected package on the host.

## Features

- GTK 3 interface with repository search, package list, destination picker,
  progress and detailed build output.
- Interactive terminal mode for scripts and systems without a graphical
  session.
- Downloads the selected package and its required dependency closure with
  `apt-get download`.
- Extracts packages into an isolated AppDir without running Debian maintainer
  scripts or changing the installed package database.
- Detects the best `.desktop` launcher, executable and application icon.
- Embeds the icon in the AppImage through both the root icon and `.DirIcon`.
- Integrates generated AppImages with miloFiles icons and launching, miloDock
  window matching and pinning, and miloPanel Global Menu.
- Produces a clean, versionless filename such as `Audacity.appimage`.
- Uses the official AppImage `appimagetool`; it is cached under
  `~/.cache/milopkg/tools/` after its first use.
- Supports `x86_64`, `aarch64`, `armhf` and `i686`.
- Spanish and English interface.

## Installation

```bash
sudo ./install.sh
```

The installer adds `milopkg` to `/usr/local/bin` and registers the application
in the desktop menu. Building an AppImage itself does not need root privileges.

## Usage

Open **miloPKG** from the applications menu, enter an application name, select a
result, choose a destination folder and press **Create AppImage**.

Terminal mode:

```bash
milopkg --cli audacity
```

Direct conversion when the exact Debian package name is already known:

```bash
milopkg --package audacity --output "$HOME/Applications"
```

Existing output files are preserved. Pass `--force` explicitly to replace one:

```bash
milopkg --package audacity -o "$HOME/Applications" --force
```

## How it works

1. `apt-cache` searches the repositories and reads package metadata.
2. Required dependencies are resolved recursively. Core host runtime packages
   such as glibc, the dynamic loader, systemd and the shell are not bundled.
3. `apt-get download` stores all selected `.deb` files in a temporary directory.
4. `dpkg-deb -x` assembles an AppDir without installing anything.
5. miloPKG chooses the package's primary desktop entry and executable, copies
   its highest-quality icon and creates a relocatable `AppRun`.
6. The official `appimagetool` creates a type-2 AppImage and miloPKG moves it
   atomically to the requested destination.

The temporary build directory is removed after success, failure or
cancellation.

## Compatibility notes

An AppImage made from a Debian repository binary still requires a Linux system
with a glibc version at least as new as the one required by that binary.
Applications with unusual post-installation scripts, kernel modules, system
services, privileged helpers or hard-coded system paths may need package-specific
adjustments. The best candidates are normal desktop applications and
self-contained command-line programs.

The first build requires Internet access both for Debian packages and, when
`appimagetool` is not installed, for the official AppImage tool. Later builds
reuse the cached tool.
