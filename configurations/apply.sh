#!/bin/bash
# Author: Wamphyre
# Description: Customized skinpack for XFCE4 to look like macOS
# Version: 2.0 (Fixed and improved)

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Prevents execution with root user
if [ "$EUID" -eq 0 ]; then
    log_error "Don't run this script with root user"
    exit 1
fi

# Validate arguments
if [ -z "$1" ] || [ -z "$2" ]; then
    log_error "Usage: $0 <user_home> <username>"
    exit 1
fi

USER_HOME="$1"
EXEC_USER="$2"

log_info "Applying configurations for user: $EXEC_USER"
log_info "Home directory: $USER_HOME"

# Verify user home exists
if [ ! -d "$USER_HOME" ]; then
    log_error "User home directory does not exist: $USER_HOME"
    exit 1
fi

# Verify required commands
for cmd in xfconf-query dconf; do
    if ! command -v "$cmd" &> /dev/null; then
        log_error "Required command not found: $cmd"
        exit 1
    fi
done

# Make sure that the apps that make up the interface are not running
log_info "Stopping running processes..."
pkill plank 2>/dev/null || true
pkill xfce4-panel 2>/dev/null || true
sleep 1

# Remove old configurations (no backup needed)
log_info "Removing old configurations..."
rm -f "$USER_HOME/.gtkrc-2.0"
rm -f "$USER_HOME/.config/gtk-3.0/gtk.css"
rm -rf "$USER_HOME/.config/xfce4/panel"
rm -rf "$USER_HOME/.config/xfce4/xfconf"
rm -rf "$USER_HOME/.config/plank/dock1"

# Create necessary directories
log_info "Creating configuration directories..."
mkdir -p "$USER_HOME/.config/gtk-3.0"
mkdir -p "$USER_HOME/.config/xfce4"
mkdir -p "$USER_HOME/.config/plank/dock1/launchers"

# Apply new settings
log_info "Applying GTK configurations..."
echo "#xfce4-power-manager-plugin * { -gtk-icon-transform: scale(1.2); }" > "$USER_HOME/.config/gtk-3.0/gtk.css"

# Copy font configuration for macOS-like rendering
if [ -f "configurations/fonts.conf" ]; then
    log_info "Configuring font rendering..."
    mkdir -p "$USER_HOME/.config/fontconfig"
    cp configurations/fonts.conf "$USER_HOME/.config/fontconfig/fonts.conf"
    chmod 644 "$USER_HOME/.config/fontconfig/fonts.conf"
    chown "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/fontconfig/fonts.conf"
    
    # Update font cache
    if command -v fc-cache &> /dev/null; then
        fc-cache -f "$USER_HOME/.fonts" 2>/dev/null || true
    fi
fi

# Create GTK bookmarks dynamically with correct user paths
log_info "Creating GTK bookmarks..."
cat > "$USER_HOME/.config/gtk-3.0/bookmarks" << EOF
EOF

# Add common directories if they exist
[ -d "$USER_HOME/Documents" ] && echo "file://$USER_HOME/Documents" >> "$USER_HOME/.config/gtk-3.0/bookmarks"
[ -d "$USER_HOME/Documentos" ] && echo "file://$USER_HOME/Documentos" >> "$USER_HOME/.config/gtk-3.0/bookmarks"
[ -d "$USER_HOME/Downloads" ] && echo "file://$USER_HOME/Downloads" >> "$USER_HOME/.config/gtk-3.0/bookmarks"
[ -d "$USER_HOME/Descargas" ] && echo "file://$USER_HOME/Descargas" >> "$USER_HOME/.config/gtk-3.0/bookmarks"
[ -d "$USER_HOME/Music" ] && echo "file://$USER_HOME/Music" >> "$USER_HOME/.config/gtk-3.0/bookmarks"
[ -d "$USER_HOME/Música" ] && echo "file://$USER_HOME/Música" >> "$USER_HOME/.config/gtk-3.0/bookmarks"
[ -d "$USER_HOME/Pictures" ] && echo "file://$USER_HOME/Pictures" >> "$USER_HOME/.config/gtk-3.0/bookmarks"
[ -d "$USER_HOME/Imágenes" ] && echo "file://$USER_HOME/Imágenes" >> "$USER_HOME/.config/gtk-3.0/bookmarks"
[ -d "$USER_HOME/Videos" ] && echo "file://$USER_HOME/Videos" >> "$USER_HOME/.config/gtk-3.0/bookmarks"
[ -d "$USER_HOME/Vídeos" ] && echo "file://$USER_HOME/Vídeos" >> "$USER_HOME/.config/gtk-3.0/bookmarks"

chmod 644 "$USER_HOME/.config/gtk-3.0/bookmarks"
chown "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/gtk-3.0/bookmarks"

# Copy XFCE4 panel configuration
if [ -d "configurations/xfce4/panel" ]; then
    log_info "Copying XFCE4 panel configuration..."
    cp -R configurations/xfce4/panel "$USER_HOME/.config/xfce4/"
    find "$USER_HOME/.config/xfce4/panel" -type d -exec chmod 755 {} \;
    find "$USER_HOME/.config/xfce4/panel" -type f -exec chmod 644 {} \;
    chown -R "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/xfce4/panel"
else
    log_warn "XFCE4 panel configuration not found, skipping"
fi

# Copy XFCE4 xfconf configuration (FIXED: This was missing!)
if [ -d "configurations/xfce4/xfconf" ]; then
    log_info "Copying XFCE4 xfconf configuration..."
    cp -R configurations/xfce4/xfconf "$USER_HOME/.config/xfce4/"
    find "$USER_HOME/.config/xfce4/xfconf" -type d -exec chmod 755 {} \;
    find "$USER_HOME/.config/xfce4/xfconf" -type f -exec chmod 644 {} \;
    chown -R "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/xfce4/xfconf"
else
    log_warn "XFCE4 xfconf configuration not found, skipping"
fi

# Copy Plank launchers
if [ -d "configurations/plank/dock1/launchers" ]; then
    log_info "Copying Plank launchers..."
    cp configurations/plank/dock1/launchers/*.dockitem "$USER_HOME/.config/plank/dock1/launchers/" 2>/dev/null || log_warn "No dockitems found in configurations"
    chmod 755 "$USER_HOME/.config/plank/dock1/launchers"
    chmod 644 "$USER_HOME/.config/plank/dock1/launchers"/*.dockitem 2>/dev/null || true
    chown -R "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/plank/dock1/"
else
    log_warn "Plank launchers not found, skipping"
fi

# Copy autostart configuration for Plank
if [ -f "configurations/autostart/Dock.desktop" ]; then
    log_info "Copying autostart configuration..."
    mkdir -p "$USER_HOME/.config/autostart"
    cp configurations/autostart/Dock.desktop "$USER_HOME/.config/autostart/"
    chmod 644 "$USER_HOME/.config/autostart/Dock.desktop"
    chown "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/autostart/Dock.desktop"
else
    log_warn "Autostart configuration not found, skipping"
fi

# Copy XFCE4 terminal configuration
if [ -d "configurations/xfce4/terminal" ]; then
    log_info "Copying XFCE4 terminal configuration..."
    mkdir -p "$USER_HOME/.config/xfce4/terminal"
    cp -R configurations/xfce4/terminal/* "$USER_HOME/.config/xfce4/terminal/"
    find "$USER_HOME/.config/xfce4/terminal" -type f -exec chmod 644 {} \;
    chown -R "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/xfce4/terminal"
else
    log_warn "XFCE4 terminal configuration not found, skipping"
fi

# Copy XFCE4 desktop configuration
if [ -d "configurations/xfce4/desktop" ]; then
    log_info "Copying XFCE4 desktop configuration..."
    mkdir -p "$USER_HOME/.config/xfce4/desktop"
    cp -R configurations/xfce4/desktop/* "$USER_HOME/.config/xfce4/desktop/"
    find "$USER_HOME/.config/xfce4/desktop" -type f -exec chmod 644 {} \;
    chown -R "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/xfce4/desktop"
else
    log_warn "XFCE4 desktop configuration not found, skipping"
fi

# Apply xfconf settings
log_info "Applying xfconf settings..."

# Font rendering (macOS-like)
log_info "Configuring font rendering..."
xfconf-query -c xsettings -p /Xft/DPI -n -t int -s 96 2>/dev/null || \
    xfconf-query -c xsettings -p /Xft/DPI -t int -s 96

xfconf-query -c xsettings -p /Xft/Antialias -n -t int -s 1 2>/dev/null || \
    xfconf-query -c xsettings -p /Xft/Antialias -t int -s 1

xfconf-query -c xsettings -p /Xft/Hinting -n -t int -s 1 2>/dev/null || \
    xfconf-query -c xsettings -p /Xft/Hinting -t int -s 1

xfconf-query -c xsettings -p /Xft/HintStyle -n -t string -s "hintslight" 2>/dev/null || \
    xfconf-query -c xsettings -p /Xft/HintStyle -t string -s "hintslight"

xfconf-query -c xsettings -p /Xft/RGBA -n -t string -s "rgb" 2>/dev/null || \
    xfconf-query -c xsettings -p /Xft/RGBA -t string -s "rgb"

xfconf-query -c xsettings -p /Xft/Lcdfilter -n -t string -s "lcddefault" 2>/dev/null || \
    xfconf-query -c xsettings -p /Xft/Lcdfilter -t string -s "lcddefault"

# Font names (San Francisco Pro)
xfconf-query -c xsettings -p /Gtk/FontName -n -t string -s "SF Pro Text 10" 2>/dev/null || \
    xfconf-query -c xsettings -p /Gtk/FontName -t string -s "SF Pro Text 10"

xfconf-query -c xsettings -p /Gtk/MonospaceFontName -n -t string -s "SF Mono 10" 2>/dev/null || \
    xfconf-query -c xsettings -p /Gtk/MonospaceFontName -t string -s "SF Mono 10"

# Window title font
xfconf-query -c xfwm4 -p /general/title_font -n -t string -s "SF Pro Display Medium 9" 2>/dev/null || \
    xfconf-query -c xfwm4 -p /general/title_font -t string -s "SF Pro Display Medium 9"

# GTK settings
xfconf-query -c xsettings -p /Gtk/ShellShowsMenubar -n -t bool -s true 2>/dev/null || \
    xfconf-query -c xsettings -p /Gtk/ShellShowsMenubar -t bool -s true

xfconf-query -c xsettings -p /Gtk/ShellShowsAppmenu -n -t bool -s true 2>/dev/null || \
    xfconf-query -c xsettings -p /Gtk/ShellShowsAppmenu -t bool -s true

xfconf-query -c xsettings -p /Gtk/Modules -n -t string -s "appmenu-gtk-module" 2>/dev/null || \
    xfconf-query -c xsettings -p /Gtk/Modules -t string -s "appmenu-gtk-module"

# Window manager theme
xfconf-query -c xfwm4 -p /general/theme -n -t string -s miloOS 2>/dev/null || \
    xfconf-query -c xfwm4 -p /general/theme -t string -s miloOS

xfconf-query -c xsettings -p /Net/ThemeName -n -t string -s miloOS 2>/dev/null || \
    xfconf-query -c xsettings -p /Net/ThemeName -t string -s miloOS

# Window manager settings
xfconf-query -c xfwm4 -p /general/title_alignment -n -t string -s center 2>/dev/null || \
    xfconf-query -c xfwm4 -p /general/title_alignment -t string -s center

xfconf-query -c xfwm4 -p /general/button_layout -n -t string -s "CHM|" 2>/dev/null || \
    xfconf-query -c xfwm4 -p /general/button_layout -t string -s "CHM|"

# Icon theme (WhiteSur)
xfconf-query -c xsettings -p /Net/IconThemeName -n -t string -s WhiteSur 2>/dev/null || \
    xfconf-query -c xsettings -p /Net/IconThemeName -t string -s WhiteSur

# Cursor theme (using default Adwaita as Cocoa doesn't include cursors)
xfconf-query -c xsettings -p /Gtk/CursorThemeName -n -t string -s Adwaita 2>/dev/null || \
    xfconf-query -c xsettings -p /Gtk/CursorThemeName -t string -s Adwaita

# Desktop icons alignment (right side, macOS style)
xfconf-query -c xfce4-desktop -p /desktop-icons/gravity -n -t int -s 1 2>/dev/null || \
    xfconf-query -c xfce4-desktop -p /desktop-icons/gravity -t int -s 1

# Desktop icons settings
xfconf-query -c xfce4-desktop -p /desktop-icons/file-icons/show-home -n -t bool -s false 2>/dev/null || \
    xfconf-query -c xfce4-desktop -p /desktop-icons/file-icons/show-home -t bool -s false

xfconf-query -c xfce4-desktop -p /desktop-icons/file-icons/show-filesystem -n -t bool -s true 2>/dev/null || \
    xfconf-query -c xfce4-desktop -p /desktop-icons/file-icons/show-filesystem -t bool -s true

xfconf-query -c xfce4-desktop -p /desktop-icons/file-icons/show-removable -n -t bool -s true 2>/dev/null || \
    xfconf-query -c xfce4-desktop -p /desktop-icons/file-icons/show-removable -t bool -s true

xfconf-query -c xfce4-desktop -p /desktop-icons/file-icons/show-trash -n -t bool -s false 2>/dev/null || \
    xfconf-query -c xfce4-desktop -p /desktop-icons/file-icons/show-trash -t bool -s false

# Desktop wallpaper
xfconf-query -c xfce4-desktop -p /backdrop/screen0/monitor0/workspace0/last-image -n -t string -s /usr/share/backgrounds/blue-mountain.jpg 2>/dev/null || \
    xfconf-query -c xfce4-desktop -p /backdrop/screen0/monitor0/workspace0/last-image -t string -s /usr/share/backgrounds/blue-mountain.jpg

# Apply Plank settings using dconf directly
log_info "Applying Plank settings..."
dconf write /net/launchpad/plank/docks/dock1/theme "'milo'"
dconf write /net/launchpad/plank/docks/dock1/icon-size 48
dconf write /net/launchpad/plank/docks/dock1/hide-mode "'intelligent'"
dconf write /net/launchpad/plank/docks/dock1/position "'bottom'"
dconf write /net/launchpad/plank/docks/dock1/alignment "'center'"

# Set proper ownership
log_info "Setting proper ownership..."
chown -R "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/gtk-3.0"
chown -R "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/xfce4"
chown -R "$EXEC_USER:$EXEC_USER" "$USER_HOME/.config/plank"

log_info "Configuration applied successfully!"
log_warn "Please log out and log back in for all changes to take effect."

