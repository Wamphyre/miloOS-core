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

log_step() {
    echo -e "${BLUE}[STEP $1/$2]${NC} $3"
}

# Ensure execution as root
if [ "$EUID" -ne 0 ]; then
    log_error "This script must be run as root"
    exit 1
fi

CURRENT_DIR="$PWD"
TOTAL_STEPS=8

install_debian_packages() {
    log_step 1 $TOTAL_STEPS "Installing required packages..."
    
    # Check internet connectivity
    if ! ping -c 1 debian.org &> /dev/null; then
        log_warn "Cannot reach debian.org, network might be down"
        log_warn "Continuing anyway..."
    fi
    
    if apt-get update; then
        log_info "Package lists updated"
    else
        log_error "Failed to update package lists"
        return 1
    fi
    
    # Install packages with error handling
    apt-get install -y \
        gufw firmware-linux gmtp cifs-utils smbclient winbind \
        gtk2-engines-murrine gtk2-engines-pixbuf gnome-icon-theme \
        plank catfish appmenu-gtk2-module appmenu-gtk3-module \
        vala-panel-appmenu xfce4-appmenu-plugin xfce4-statusnotifier-plugin \
        xfce4-notifyd meson ninja-build libgee-0.8-dev libgnome-menu-3-dev \
        cdbs valac git libglib2.0-dev libwnck-3-dev libgtk-3-dev xterm \
        python3 python3-full python3-wheel python3-setuptools \
        gnome-menus gnome-maps shotwell gnome-calendar gedit zenity \
        || log_warn "Some packages may have failed to install"
    
    log_info "Package installation completed"
}

install_gtk_themes() {
    log_step 2 $TOTAL_STEPS "Installing Gtk+ themes..."
    
    if [ ! -d "resources/theme/miloOS" ]; then
        log_error "Theme directory resources/theme/miloOS not found!"
        return 1
    fi
    
    cp -R resources/theme/miloOS /usr/share/themes/
    chown root:root -R /usr/share/themes/miloOS/
    log_info "Gtk+ themes installed"
    
    if [ -d "resources/milk" ]; then
        if [ ! -d "/usr/share/slim/themes" ]; then
            log_warn "SLiM themes directory doesn't exist, creating it..."
            mkdir -p /usr/share/slim/themes
        fi
        cp -R resources/milk /usr/share/slim/themes/
        chown root:root -R /usr/share/slim/themes/milk
        log_info "SLiM theme installed"
    else
        log_warn "SLiM theme directory not found, skipping"
    fi
}

install_icon_themes() {
    log_step 3 $TOTAL_STEPS "Installing cursor and icon themes..."
    
    if [ ! -d "resources/icons/Cocoa" ]; then
        log_error "Icon theme resources/icons/Cocoa not found!"
        return 1
    fi
    
    cp -R resources/icons/Cocoa /usr/share/icons/
    chown root:root -R /usr/share/icons/Cocoa/
    
    if command -v gtk-update-icon-cache &> /dev/null; then
        gtk-update-icon-cache -f /usr/share/icons/Cocoa/
        log_info "Icon cache updated"
    else
        log_warn "gtk-update-icon-cache not found, skipping cache update"
    fi
    
    if [ -f "resources/icons/catfish-symbolic.png" ]; then
        cp resources/icons/catfish-symbolic.png /usr/share/pixmaps/
        log_info "Catfish icon installed"
    fi
}

install_fonts() {
    log_step 4 $TOTAL_STEPS "Installing fonts..."
    
    if [ ! -d "resources/fonts/Inter-Desktop" ]; then
        log_error "Font directory resources/fonts/Inter-Desktop not found!"
        return 1
    fi
    
    cp -R resources/fonts/Inter-Desktop /usr/share/fonts/
    chown root:root -R /usr/share/fonts/Inter-Desktop/
    
    if command -v fc-cache &> /dev/null; then
        fc-cache -f
        log_info "Font cache updated"
    else
        log_warn "fc-cache not found, skipping cache update"
    fi
}

install_wallpaper() {
    log_step 5 $TOTAL_STEPS "Installing wallpapers..."
    
    if [ ! -d "resources/backgrounds" ]; then
        log_error "Backgrounds directory not found!"
        return 1
    fi
    
    if [ ! -d "/usr/share/backgrounds" ]; then
        mkdir -p /usr/share/backgrounds
    fi
    
    cp resources/backgrounds/* /usr/share/backgrounds/ 2>/dev/null || log_warn "No wallpapers found"
    chmod 644 /usr/share/backgrounds/* 2>/dev/null
    chown root:root /usr/share/backgrounds/* 2>/dev/null
    log_info "Wallpapers installed"
}

install_plank_theme() {
    log_step 6 $TOTAL_STEPS "Installing Plank theme..."
    
    if [ ! -d "resources/plank/milo" ]; then
        log_error "Plank theme resources/plank/milo not found!"
        return 1
    fi
    
    if [ ! -d "/usr/share/plank/themes" ]; then
        mkdir -p /usr/share/plank/themes
    fi
    
    cp -R resources/plank/milo /usr/share/plank/themes/
    chmod 755 /usr/share/plank/themes/milo
    chmod 644 /usr/share/plank/themes/milo/*.theme 2>/dev/null || true
    chown root:root -R /usr/share/plank/themes/milo
    log_info "Plank theme installed"
}

install_menus() {
    log_step 7 $TOTAL_STEPS "Installing custom menus..."
    
    # Menu binary
    if [ -f "resources/menus/bin/milo-session" ]; then
        cp resources/menus/bin/milo-session /usr/bin/
        chmod 755 /usr/bin/milo-session
        chown root:root /usr/bin/milo-session
        log_info "Menu binary installed"
    else
        log_warn "Menu binary not found, skipping"
    fi
    
    # Menu Items
    local menu_items=(
        "milo-logout" "milo-shutdown" "milo-restart" 
        "milo-sleep" "milo-settings" "milo-about"
    )
    
    for item in "${menu_items[@]}"; do
        if [ -f "resources/menus/items/${item}.desktop" ]; then
            cp "resources/menus/items/${item}.desktop" /usr/share/applications/
            chmod 644 "/usr/share/applications/${item}.desktop"
            chown root:root "/usr/share/applications/${item}.desktop"
        else
            log_warn "Menu item ${item}.desktop not found, skipping"
        fi
    done
    
    # Menu XDG for XFCE4
    if [ -f "resources/menus/xdg/milo.menu" ]; then
        if [ ! -d "/etc/xdg/menus" ]; then
            mkdir -p /etc/xdg/menus
        fi
        cp resources/menus/xdg/milo.menu /etc/xdg/menus/
        chmod 644 /etc/xdg/menus/milo.menu
        chown root:root /etc/xdg/menus/milo.menu
        log_info "XDG menu installed"
    else
        log_warn "XDG menu not found, skipping"
    fi
}

rebrand_system() {
    log_step 8 $TOTAL_STEPS "Rebranding system from Debian to miloOS..."
    
    # Backup original files
    local backup_dir="/root/debian-backup-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$backup_dir"
    log_info "Creating backup at: $backup_dir"
    
    # 1. /etc/os-release - System identification
    if [ -f "/etc/os-release" ]; then
        cp /etc/os-release "$backup_dir/os-release.bak"
        cat > /etc/os-release << 'EOF'
PRETTY_NAME="miloOS"
NAME="miloOS"
VERSION_ID="1.0"
VERSION="1.0 (Stable)"
VERSION_CODENAME=stable
ID=miloos
ID_LIKE=debian
DEBIAN_VERSION="13"
DEBIAN_CODENAME="trixie"
HOME_URL="https://github.com/Wamphyre/miloOS-core"
SUPPORT_URL="https://github.com/Wamphyre/miloOS-core/issues"
BUG_REPORT_URL="https://github.com/Wamphyre/miloOS-core/issues"
EOF
        log_info "Updated /etc/os-release"
    fi
    
    # 2. /etc/issue - Login banner
    if [ -f "/etc/issue" ]; then
        cp /etc/issue "$backup_dir/issue.bak"
        cat > /etc/issue << 'EOF'
miloOS 1.0 \n \l

EOF
        log_info "Updated /etc/issue"
    fi
    
    # 3. /etc/issue.net - Network login banner
    if [ -f "/etc/issue.net" ]; then
        cp /etc/issue.net "$backup_dir/issue.net.bak"
        echo "miloOS 1.0" > /etc/issue.net
        log_info "Updated /etc/issue.net"
    fi
    
    # 4. /etc/lsb-release - LSB information
    if [ -f "/etc/lsb-release" ]; then
        cp /etc/lsb-release "$backup_dir/lsb-release.bak"
    fi
    cat > /etc/lsb-release << 'EOF'
DISTRIB_ID=miloOS
DISTRIB_RELEASE=1.0
DISTRIB_CODENAME=stable
DISTRIB_DESCRIPTION="miloOS 1.0"
EOF
    log_info "Updated /etc/lsb-release"
    
    # 5. /etc/debian_version - Keep for compatibility but update
    if [ -f "/etc/debian_version" ]; then
        cp /etc/debian_version "$backup_dir/debian_version.bak"
        echo "miloOS/1.0" > /etc/debian_version
        log_info "Updated /etc/debian_version"
    fi
    
    # 6. MOTD (Message of the Day)
    if [ -d "/etc/update-motd.d" ]; then
        # Disable default Debian MOTD scripts
        chmod -x /etc/update-motd.d/* 2>/dev/null || true
        
        # Create custom miloOS MOTD
        cat > /etc/update-motd.d/00-header << 'EOF'
#!/bin/sh
echo ""
echo "  __  __ _ _       ___  ____  "
echo " |  \/  (_) | ___ / _ \/ ___| "
echo " | |\/| | | |/ _ \ | | \___ \ "
echo " | |  | | | | (_) | |_| |___) |"
echo " |_|  |_|_|_|\___/ \___/|____/ "
echo ""
echo " Welcome to miloOS - A beautiful macOS-like experience"
echo ""
EOF
        chmod +x /etc/update-motd.d/00-header
        log_info "Created custom MOTD"
    fi
    
    # 7. Hostname display in terminal (optional)
    # Update /etc/hostname if user wants (commented out for safety)
    # read -p "Do you want to change hostname to 'miloos'? (y/N): " -n 1 -r
    # if [[ $REPLY =~ ^[Yy]$ ]]; then
    #     cp /etc/hostname "$backup_dir/hostname.bak"
    #     echo "miloos" > /etc/hostname
    #     hostnamectl set-hostname miloos 2>/dev/null || true
    #     log_info "Updated hostname"
    # fi
    
    # 8. Update GRUB bootloader (if exists)
    if [ -f "/etc/default/grub" ]; then
        cp /etc/default/grub "$backup_dir/grub.bak"
        sed -i 's/GRUB_DISTRIBUTOR=.*/GRUB_DISTRIBUTOR="miloOS"/' /etc/default/grub
        
        # Update grub if update-grub is available
        if command -v update-grub &> /dev/null; then
            log_info "Updating GRUB configuration..."
            update-grub 2>/dev/null || log_warn "Failed to update GRUB, you may need to run 'update-grub' manually"
        fi
        log_info "Updated GRUB distributor name"
    fi
    
    # 9. Plymouth theme (boot splash) - if plymouth is installed
    if command -v plymouth-set-default-theme &> /dev/null; then
        log_info "Plymouth detected, you may want to install a custom miloOS theme"
    fi
    
    # 10. LightDM/GDM greeter configuration
    if [ -f "/etc/lightdm/lightdm-gtk-greeter.conf" ]; then
        cp /etc/lightdm/lightdm-gtk-greeter.conf "$backup_dir/lightdm-gtk-greeter.conf.bak"
        # Add or update the theme
        if ! grep -q "^theme-name" /etc/lightdm/lightdm-gtk-greeter.conf; then
            echo "theme-name=miloOS" >> /etc/lightdm/lightdm-gtk-greeter.conf
        else
            sed -i 's/^theme-name=.*/theme-name=miloOS/' /etc/lightdm/lightdm-gtk-greeter.conf
        fi
        log_info "Updated LightDM greeter theme"
    fi
    
    log_info "System rebranding completed!"
    log_info "Backup saved at: $backup_dir"
    log_warn "Note: Some changes require a reboot to take full effect"
}

# Main execution
log_info "Starting miloOS resources installation..."
log_info "Current directory: $CURRENT_DIR"

install_debian_packages
install_gtk_themes
install_icon_themes
install_fonts
install_wallpaper
install_plank_theme
install_menus
rebrand_system

log_info "All resources installed successfully!"
log_info "System has been rebranded to miloOS"
log_warn "Please reboot your system for all changes to take effect"
