#!/bin/bash
# Author: Wamphyre
# Description: miloOS installation and configuration script
# Version: 2.1 (Fixed critical issues)

# Exit on error but handle it properly
set -e
set -o pipefail

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

# Error handler
error_exit() {
    log_error "$1"
    log_error "Installation failed. Check the errors above."
    exit 1
}

# Ensure execution as root
if [ "$(id -u)" -ne 0 ]; then
    error_exit "This script must be run as root (use sudo)"
fi

CURRENT_DIR="$PWD"
TOTAL_STEPS=13

# Verify we're on Debian
verify_system() {
    log_step 0 $TOTAL_STEPS "Verifying system requirements..."
    
    if [ ! -f "/etc/debian_version" ]; then
        error_exit "This script is designed for Debian systems only"
    fi
    
    # Check if XFCE4 is installed
    if ! command -v xfce4-session &> /dev/null; then
        log_warn "XFCE4 doesn't appear to be installed"
        log_warn "This theme is designed for XFCE4 desktop environment"
        read -p "Continue anyway? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 0
        fi
    fi
    
    # Check disk space (need at least 500MB)
    available_space=$(df -m "$CURRENT_DIR" | awk 'NR==2 {print $4}')
    if [ "$available_space" -lt 500 ]; then
        error_exit "Insufficient disk space. Need at least 500MB free"
    fi
    
    log_info "System verification passed"
}

install_debian_packages() {
    log_step 1 $TOTAL_STEPS "Installing required packages..."
    
    # Check internet connectivity
    if ! ping -c 1 -W 3 debian.org &> /dev/null; then
        log_warn "Cannot reach debian.org, network might be down"
        log_warn "Package installation may fail"
    fi
    
    if ! apt-get update; then
        error_exit "Failed to update package lists. Check your internet connection"
    fi
    
    log_info "Package lists updated"
    
    # Install only essential packages for miloOS
    log_info "Installing packages..."
    
    # Try to install packages, but don't fail if some are missing
    local FAILED_PKGS=""
    
    for pkg in gtk2-engines-murrine gtk2-engines-pixbuf plank catfish \
               appmenu-gtk3-module dconf-cli vala-panel-appmenu \
               xfce4-appmenu-plugin xfce4-notifyd cifs-utils smbclient slim zenity; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            FAILED_PKGS="$FAILED_PKGS $pkg"
        fi
    done
    
    if [ -n "$FAILED_PKGS" ]; then
        log_warn "Some packages failed to install:$FAILED_PKGS"
        log_warn "Continuing anyway, but some features may not work"
    fi
    
    # Check and install PipeWire if not present
    log_info "Checking PipeWire installation..."
    if ! command -v pipewire &> /dev/null; then
        log_info "PipeWire not found, installing complete PipeWire stack..."
        
        local PW_FAILED=""
        for pkg in pipewire pipewire-audio-client-libraries pipewire-pulse \
                   pipewire-alsa pipewire-jack pipewire-v4l2 pipewire-bin \
                   wireplumber libspa-0.2-bluetooth libspa-0.2-jack \
                   libspa-0.2-modules gstreamer1.0-pipewire rtkit; do
            if apt-get install -y "$pkg" 2>/dev/null; then
                log_info "✓ $pkg installed"
            else
                log_warn "✗ $pkg failed to install"
                PW_FAILED="$PW_FAILED $pkg"
            fi
        done
        
        if [ -n "$PW_FAILED" ]; then
            log_warn "Some PipeWire packages failed:$PW_FAILED"
        fi
    else
        log_info "PipeWire already installed, ensuring components..."
        for pkg in pipewire-pulse pipewire-alsa pipewire-jack pipewire-v4l2 \
                   wireplumber libspa-0.2-modules gstreamer1.0-pipewire rtkit; do
            apt-get install -y "$pkg" 2>/dev/null || log_warn "Could not install $pkg"
        done
    fi
    
    log_info "Package installation completed"
}

install_gtk_themes() {
    log_step 2 $TOTAL_STEPS "Installing Gtk+ themes..."
    
    if [ ! -d "resources/theme/miloOS" ]; then
        error_exit "Theme directory resources/theme/miloOS not found!"
    fi
    
    cp -R resources/theme/miloOS /usr/share/themes/
    chown -R root:root /usr/share/themes/miloOS/
    log_info "Gtk+ themes installed"
    
    # Configure xfwm4 theme to hide window titles
    log_info "Configuring window manager theme..."
    mkdir -p /usr/share/themes/miloOS/xfwm4
    cat >> /usr/share/themes/miloOS/xfwm4/themerc << 'EOF'
title_vertical_offset_active=-100
title_vertical_offset_inactive=-100
EOF
    log_info "Window titles hidden"
    
    if [ -d "resources/milk" ]; then
        mkdir -p /usr/share/slim/themes
        cp -R resources/milk /usr/share/slim/themes/
        chown -R root:root /usr/share/slim/themes/milk
        log_info "SLiM theme installed"
        
        # Configure SLiM to use milk theme
        if [ -f "/etc/slim.conf" ]; then
            # Set milk theme
            if grep -q "^current_theme" /etc/slim.conf; then
                sed -i 's/^current_theme.*/current_theme milk/' /etc/slim.conf
            else
                echo "current_theme milk" >> /etc/slim.conf
            fi
            log_info "SLiM configured to use milk theme"
            
            # Set SLiM as default display manager
            log_info "Setting SLiM as default display manager..."
            
            # Disable other display managers
            systemctl disable lightdm.service 2>/dev/null || true
            systemctl disable gdm.service 2>/dev/null || true
            systemctl disable gdm3.service 2>/dev/null || true
            systemctl disable sddm.service 2>/dev/null || true
            
            # Enable SLiM
            systemctl enable slim.service 2>/dev/null || log_warn "Could not enable SLiM service"
            
            # Configure default display manager via debconf
            if command -v debconf-set-selections &> /dev/null; then
                echo "slim shared/default-x-display-manager select slim" | debconf-set-selections
                log_info "SLiM set as default display manager"
            fi
            
            # Update alternatives
            if command -v update-alternatives &> /dev/null; then
                update-alternatives --set x-session-manager /usr/bin/xfce4-session 2>/dev/null || true
            fi
        else
            log_warn "SLiM configuration file not found"
            log_warn "SLiM may not be installed correctly"
        fi
    else
        log_warn "SLiM theme directory not found, skipping"
    fi
}

install_icon_themes() {
    log_step 3 $TOTAL_STEPS "Installing WhiteSur icon theme..."
    
    # Install git if not present (needed to clone)
    if ! command -v git &> /dev/null; then
        log_info "Installing git..."
        apt-get install -y git 2>/dev/null || log_warn "Could not install git"
    fi
    
    # Clone WhiteSur icon theme
    local TEMP_DIR="/tmp/WhiteSur-icon-theme-$$"
    log_info "Downloading WhiteSur icon theme..."
    
    if git clone --depth=1 https://github.com/vinceliuice/WhiteSur-icon-theme.git "$TEMP_DIR" 2>/dev/null; then
        log_info "Installing WhiteSur icon theme..."
        cd "$TEMP_DIR"
        
        # Install with options: -b (bold icons), -d (destination)
        # Note: -a flag removed to avoid installing all alternative versions
        if ./install.sh -b -d /usr/local/share/icons 2>/dev/null; then
            log_info "WhiteSur icon theme installed successfully"
        else
            log_warn "WhiteSur installation script failed, trying manual installation..."
            # Fallback: copy manually
            mkdir -p /usr/local/share/icons
            cp -R src/WhiteSur* /usr/local/share/icons/ 2>/dev/null || true
        fi
        
        cd "$CURRENT_DIR"
        rm -rf "$TEMP_DIR"
    else
        log_error "Failed to download WhiteSur icon theme"
        log_warn "Continuing without icon theme..."
    fi
    
    # Install catfish icon if available
    if [ -f "resources/icons/catfish-symbolic.png" ]; then
        cp resources/icons/catfish-symbolic.png /usr/share/pixmaps/
        log_info "Catfish icon installed"
    fi
    
    # Update icon cache
    if command -v gtk-update-icon-cache &> /dev/null; then
        log_info "Updating icon cache..."
        for theme_dir in /usr/local/share/icons/WhiteSur*; do
            if [ -d "$theme_dir" ]; then
                gtk-update-icon-cache -f "$theme_dir" 2>/dev/null || true
            fi
        done
    fi
    
    log_info "Icon theme installation completed"
}

install_fonts() {
    log_step 4 $TOTAL_STEPS "Installing San Francisco Pro fonts..."
    
    # Install unzip if not present
    if ! command -v unzip &> /dev/null; then
        log_info "Installing unzip..."
        apt-get install -y unzip 2>/dev/null || log_warn "Could not install unzip"
    fi
    
    # Download San Francisco Pro fonts
    local TEMP_DIR="/tmp/sf-fonts-$$"
    local FONT_DIR="/usr/share/fonts/truetype/san-francisco"
    
    log_info "Downloading San Francisco Pro fonts..."
    mkdir -p "$TEMP_DIR"
    
    # Download from GitHub
    if command -v wget &> /dev/null; then
        wget -q -O "$TEMP_DIR/sf-fonts.zip" "https://github.com/sahibjotsaggu/San-Francisco-Pro-Fonts/archive/master.zip" 2>/dev/null || {
            log_warn "Failed to download fonts with wget, trying curl..."
            curl -sL -o "$TEMP_DIR/sf-fonts.zip" "https://github.com/sahibjotsaggu/San-Francisco-Pro-Fonts/archive/master.zip" 2>/dev/null || {
                log_error "Failed to download San Francisco Pro fonts"
                log_warn "Continuing without custom fonts..."
                rm -rf "$TEMP_DIR"
                return 0
            }
        }
    elif command -v curl &> /dev/null; then
        curl -sL -o "$TEMP_DIR/sf-fonts.zip" "https://github.com/sahibjotsaggu/San-Francisco-Pro-Fonts/archive/master.zip" 2>/dev/null || {
            log_error "Failed to download San Francisco Pro fonts"
            log_warn "Continuing without custom fonts..."
            rm -rf "$TEMP_DIR"
            return 0
        }
    else
        log_error "Neither wget nor curl available"
        log_warn "Continuing without custom fonts..."
        rm -rf "$TEMP_DIR"
        return 0
    fi
    
    # Extract fonts
    log_info "Extracting fonts..."
    cd "$TEMP_DIR"
    if unzip -q sf-fonts.zip 2>/dev/null; then
        # Create font directory
        mkdir -p "$FONT_DIR"
        
        # Copy all OTF and TTF files
        find San-Francisco-Pro-Fonts-master -type f \( -name "*.otf" -o -name "*.ttf" \) -exec cp {} "$FONT_DIR/" \; 2>/dev/null
        
        # Set permissions
        chown -R root:root "$FONT_DIR"
        chmod 644 "$FONT_DIR"/*.{otf,ttf} 2>/dev/null || true
        
        log_info "San Francisco Pro fonts installed"
    else
        log_warn "Failed to extract fonts"
    fi
    
    cd "$CURRENT_DIR"
    rm -rf "$TEMP_DIR"
    
    # Update font cache
    if command -v fc-cache &> /dev/null; then
        log_info "Updating font cache..."
        fc-cache -f 2>/dev/null || log_warn "Font cache update failed"
    fi
    
    log_info "Font installation completed"
}

install_wallpaper() {
    log_step 5 $TOTAL_STEPS "Installing wallpapers..."
    
    if [ ! -d "resources/backgrounds" ]; then
        error_exit "Backgrounds directory not found!"
    fi
    
    # Create miloOS subdirectory to avoid conflicts
    mkdir -p /usr/share/backgrounds/miloOS
    
    # Copy only our wallpapers
    if ls resources/backgrounds/*.jpg &> /dev/null || ls resources/backgrounds/*.png &> /dev/null; then
        cp resources/backgrounds/*.{jpg,png} /usr/share/backgrounds/miloOS/ 2>/dev/null || true
        chmod 644 /usr/share/backgrounds/miloOS/*
        chown root:root /usr/share/backgrounds/miloOS/*
        
        # Create symlinks in main backgrounds dir for compatibility
        ln -sf /usr/share/backgrounds/miloOS/blue-mountain.jpg /usr/share/backgrounds/blue-mountain.jpg 2>/dev/null || true
        ln -sf /usr/share/backgrounds/miloOS/dusk.jpg /usr/share/backgrounds/dusk.jpg 2>/dev/null || true
        
        log_info "Wallpapers installed"
    else
        log_warn "No wallpapers found in resources/backgrounds/"
    fi
}

install_plank_theme() {
    log_step 6 $TOTAL_STEPS "Installing Plank theme..."
    
    if [ ! -d "resources/plank/milo" ]; then
        error_exit "Plank theme resources/plank/milo not found!"
    fi
    
    mkdir -p /usr/share/plank/themes
    cp -R resources/plank/milo /usr/share/plank/themes/
    chmod 755 /usr/share/plank/themes/milo
    chmod 644 /usr/share/plank/themes/milo/*.theme 2>/dev/null || true
    chown -R root:root /usr/share/plank/themes/milo
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
    
    local installed_count=0
    for item in "${menu_items[@]}"; do
        if [ -f "resources/menus/items/${item}.desktop" ]; then
            cp "resources/menus/items/${item}.desktop" /usr/share/applications/
            chmod 644 "/usr/share/applications/${item}.desktop"
            chown root:root "/usr/share/applications/${item}.desktop"
            installed_count=$((installed_count + 1))
        fi
    done
    
    log_info "Installed $installed_count menu items"
    
    # Menu XDG for XFCE4
    if [ -f "resources/menus/xdg/milo.menu" ]; then
        mkdir -p /etc/xdg/menus
        cp resources/menus/xdg/milo.menu /etc/xdg/menus/
        chmod 644 /etc/xdg/menus/milo.menu
        chown root:root /etc/xdg/menus/milo.menu
        log_info "XDG menu installed"
    else
        log_warn "XDG menu not found, skipping"
    fi
    
    # Clean up XFCE applications menu (remove separators)
    if [ -f "/etc/xdg/menus/xfce-applications.menu" ]; then
        log_info "Cleaning up XFCE applications menu..."
        sed -i '/<Layout>/,/<\/Layout>/ { /^[[:space:]]*<Separator\/>/d }' /etc/xdg/menus/xfce-applications.menu
        log_info "XFCE applications menu cleaned"
    fi
}

rebrand_system() {
    log_step 8 $TOTAL_STEPS "Rebranding system from Debian to miloOS..."
    
    # 1. /etc/os-release - System identification
    if [ -f "/etc/os-release" ]; then
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
        cat > /etc/issue << 'EOF'
miloOS 1.0 \n \l

EOF
        log_info "Updated /etc/issue"
    fi
    
    # 3. /etc/issue.net - Network login banner
    if [ -f "/etc/issue.net" ]; then
        echo "miloOS 1.0" > /etc/issue.net
        log_info "Updated /etc/issue.net"
    fi
    
    # 4. /etc/lsb-release - LSB information
    cat > /etc/lsb-release << 'EOF'
DISTRIB_ID=miloOS
DISTRIB_RELEASE=1.0
DISTRIB_CODENAME=stable
DISTRIB_DESCRIPTION="miloOS 1.0"
EOF
    log_info "Updated /etc/lsb-release"
    
    # 5. /etc/debian_version - Keep for compatibility
    if [ -f "/etc/debian_version" ]; then
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
echo " Welcome to miloOS - Professional Audio & Multimedia Production"
echo ""
EOF
        chmod +x /etc/update-motd.d/00-header
        log_info "Created custom MOTD"
    fi
    
    # 7. Update GRUB bootloader (if exists)
    if [ -f "/etc/default/grub" ]; then
        sed -i 's/GRUB_DISTRIBUTOR=.*/GRUB_DISTRIBUTOR="miloOS"/' /etc/default/grub
        
        # Update grub if update-grub is available
        if command -v update-grub &> /dev/null; then
            log_info "Updating GRUB configuration..."
            update-grub 2>/dev/null || log_warn "Failed to update GRUB, you may need to run 'update-grub' manually"
        fi
        log_info "Updated GRUB distributor name"
    fi
    
    # 8. Plymouth theme (boot splash) - if plymouth is installed
    if command -v plymouth-set-default-theme &> /dev/null; then
        log_info "Plymouth detected, you may want to install a custom miloOS theme"
    fi
    
    # 9. LightDM/GDM greeter configuration
    if [ -f "/etc/lightdm/lightdm-gtk-greeter.conf" ]; then
        
        # Add or update the theme in [greeter] section
        if ! grep -q "^\[greeter\]" /etc/lightdm/lightdm-gtk-greeter.conf; then
            echo "[greeter]" >> /etc/lightdm/lightdm-gtk-greeter.conf
        fi
        
        if ! grep -q "^theme-name" /etc/lightdm/lightdm-gtk-greeter.conf; then
            sed -i '/^\[greeter\]/a theme-name=miloOS' /etc/lightdm/lightdm-gtk-greeter.conf
        else
            sed -i 's/^theme-name=.*/theme-name=miloOS/' /etc/lightdm/lightdm-gtk-greeter.conf
        fi
        log_info "Updated LightDM greeter theme"
    fi
    
    log_info "System rebranding completed!"
    log_warn "Note: Some changes require a reboot to take full effect"
}

optimize_realtime_audio() {
    log_step 9 $TOTAL_STEPS "Optimizing system for real-time audio..."
    
    # 1. Configure PipeWire for real-time audio
    log_info "Configuring PipeWire for real-time audio production..."
    
    # Create PipeWire configuration directory
    mkdir -p /etc/pipewire/pipewire.conf.d
    
    # Create low-latency configuration
    cat > /etc/pipewire/pipewire.conf.d/99-lowlatency.conf << 'EOF'
# Low-latency PipeWire configuration for miloOS
context.properties = {
    default.clock.rate = 48000
    default.clock.quantum = 256
    default.clock.min-quantum = 64
    default.clock.max-quantum = 2048
}

context.modules = [
    {   name = libpipewire-module-rtkit
        args = {
            nice.level   = -15
            rt.prio      = 88
            rt.time.soft = 200000
            rt.time.hard = 200000
        }
        flags = [ ifexists nofail ]
    }
]
EOF
    
    # Create JACK configuration for PipeWire
    log_info "Configuring JACK compatibility..."
    mkdir -p /etc/pipewire/jack.conf.d
    cat > /etc/pipewire/jack.conf.d/99-jack-lowlatency.conf << 'EOF'
# JACK configuration for miloOS
jack.properties = {
    node.latency = 256/48000
    jack.merge-monitor = true
    jack.short-name = true
}
EOF
    
    # Configure JACK library path for all users
    log_info "Configuring JACK library path..."
    mkdir -p /etc/profile.d
    cat > /etc/profile.d/pipewire-jack.sh << 'EOF'
# PipeWire JACK library path configuration for miloOS
# This allows JACK applications to use PipeWire's JACK implementation
export LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack:${LD_LIBRARY_PATH}"
EOF
    chmod 644 /etc/profile.d/pipewire-jack.sh
    
    # Also configure for systemd user sessions
    mkdir -p /etc/systemd/user.conf.d
    cat > /etc/systemd/user.conf.d/pipewire-jack.conf << 'EOF'
[Manager]
DefaultEnvironment="LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack:${LD_LIBRARY_PATH}"
EOF
    
    log_info "JACK library path configured globally"
    
    # Create WirePlumber low-latency and pro-audio configuration
    mkdir -p /etc/wireplumber/main.lua.d
    mkdir -p /etc/wireplumber/policy.lua.d
    
    cat > /etc/wireplumber/main.lua.d/99-lowlatency.lua << 'EOF'
-- Low-latency audio configuration for miloOS
alsa_monitor.rules = {
  {
    matches = {
      {
        { "node.name", "matches", "alsa_output.*" },
      },
    },
    apply_properties = {
      ["audio.format"] = "S32LE",
      ["audio.rate"] = 48000,
      ["api.alsa.period-size"] = 256,
      ["api.alsa.headroom"] = 1024,
    },
  },
}
EOF
    
    # Set pro-audio profile as default for ALL audio devices
    cat > /etc/wireplumber/main.lua.d/51-pro-audio-profile.lua << 'EOF'
-- Set pro-audio profile as default for all ALSA cards in miloOS
alsa_monitor.rules = {
  {
    matches = {
      {
        { "device.name", "matches", "alsa_card.*" },
      },
    },
    apply_properties = {
      ["device.profile"] = "pro-audio",
    },
  },
}
EOF
    
    # Create WirePlumber bluetooth config to also use pro-audio
    mkdir -p /etc/wireplumber/bluetooth.lua.d
    cat > /etc/wireplumber/bluetooth.lua.d/51-pro-audio-bluetooth.lua << 'EOF'
-- Set pro-audio profile for bluetooth devices in miloOS
bluez_monitor.rules = {
  {
    matches = {
      {
        { "device.name", "matches", "bluez_card.*" },
      },
    },
    apply_properties = {
      ["device.profile"] = "a2dp-sink",
    },
  },
}
EOF
    
    log_info "Pro-audio profile set as default for all devices"
    
    # Create global WirePlumber configuration for pro-audio
    mkdir -p /etc/wireplumber/wireplumber.conf.d
    cat > /etc/wireplumber/wireplumber.conf.d/51-miloOS-pro-audio.conf << 'EOF'
# miloOS pro-audio configuration
monitor.alsa.rules = [
  {
    matches = [
      {
        device.name = "~alsa_card.*"
      }
    ]
    actions = {
      update-props = {
        api.alsa.use-acp = true
        device.profile = "pro-audio"
      }
    }
  }
]
EOF
    
    # Enable PipeWire services for all users
    log_info "Enabling PipeWire services..."
    systemctl --global enable pipewire.service 2>/dev/null || true
    systemctl --global enable pipewire-pulse.service 2>/dev/null || true
    systemctl --global enable wireplumber.service 2>/dev/null || true
    
    # Disable PulseAudio if present (PipeWire replaces it)
    if systemctl is-enabled pulseaudio.service &>/dev/null; then
        log_info "Disabling PulseAudio (replaced by PipeWire)..."
        systemctl --global disable pulseaudio.service 2>/dev/null || true
        systemctl --global disable pulseaudio.socket 2>/dev/null || true
    fi
    
    log_info "PipeWire configured for low-latency audio production"
    
    # 2. Configure GRUB for low-latency audio
    if [ -f "/etc/default/grub" ]; then
        log_info "Configuring kernel parameters for real-time audio..."
        
        # Audio profile: preempt=full nohz_full=all threadirqs mitigations=off
        # This provides fully preemptible kernel + no tick on all CPUs + threaded IRQs + disabled CPU mitigations for better performance
        local AUDIO_PARAMS="preempt=full nohz_full=all threadirqs mitigations=off"
        
        # Check if parameters already exist
        if grep -q "GRUB_CMDLINE_LINUX_DEFAULT=" /etc/default/grub; then
            # Get current parameters
            local CURRENT_PARAMS=$(grep "^GRUB_CMDLINE_LINUX_DEFAULT=" /etc/default/grub | sed 's/GRUB_CMDLINE_LINUX_DEFAULT="\(.*\)"/\1/')
            
            # Add audio parameters if not present
            if ! echo "$CURRENT_PARAMS" | grep -q "preempt=full"; then
                local NEW_PARAMS="$CURRENT_PARAMS $AUDIO_PARAMS"
                sed -i "s|^GRUB_CMDLINE_LINUX_DEFAULT=.*|GRUB_CMDLINE_LINUX_DEFAULT=\"$NEW_PARAMS\"|" /etc/default/grub
                log_info "Added real-time audio kernel parameters"
            else
                log_info "Real-time audio parameters already present"
            fi
        else
            # Add the line if it doesn't exist
            echo "GRUB_CMDLINE_LINUX_DEFAULT=\"$AUDIO_PARAMS\"" >> /etc/default/grub
            log_info "Added real-time audio kernel parameters"
        fi
        
        # Update GRUB
        if command -v update-grub &> /dev/null; then
            log_info "Updating GRUB configuration..."
            update-grub 2>&1 | grep -v "^Generating" || log_warn "Failed to update GRUB"
        fi
        
        # Update initramfs
        if command -v update-initramfs &> /dev/null; then
            log_info "Updating initramfs (this may take a moment)..."
            update-initramfs -u -k all 2>&1 | grep -v "^update-initramfs:" | head -10 || true
            log_info "Initramfs updated"
        fi
    fi
    
    # 3. Create systemd service for runtime optimizations
    log_info "Creating real-time audio optimization service..."
    
    cat > /etc/systemd/system/miloOS-audio-optimization.service << 'EOF'
[Unit]
Description=miloOS Real-Time Audio Optimizations
After=multi-user.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/bin/miloOS-audio-optimize.sh

[Install]
WantedBy=multi-user.target
EOF
    
    # 4. Create optimization script
    cat > /usr/local/bin/miloOS-audio-optimize.sh << 'EOF'
#!/bin/bash
# miloOS Real-Time Audio Optimization Script

# Set CPU governor to performance
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    if [ -f "$cpu" ]; then
        echo "performance" > "$cpu" 2>/dev/null || true
    fi
done

# Disable proactive memory compaction
echo 0 > /proc/sys/vm/compaction_proactiveness 2>/dev/null || true

# Disable kernel samepage merging
echo 0 > /sys/kernel/mm/ksm/run 2>/dev/null || true

# Thrashing mitigation (prevent working set eviction for 1000ms)
if [ -f /sys/kernel/mm/lru_gen/min_ttl_ms ]; then
    echo 1000 > /sys/kernel/mm/lru_gen/min_ttl_ms 2>/dev/null || true
fi

# Prevent stuttering during intense I/O writes
echo 5 > /proc/sys/vm/dirty_ratio 2>/dev/null || true
echo 5 > /proc/sys/vm/dirty_background_ratio 2>/dev/null || true

# Increase max locked memory for audio applications
if [ -f /etc/security/limits.d/audio.conf ]; then
    # Already configured
    :
else
    cat > /etc/security/limits.d/audio.conf << 'LIMITS'
# Real-time audio configuration
@audio   -  rtprio     95
@audio   -  memlock    unlimited
@audio   -  nice      -19
LIMITS
fi

# Add user to audio group if not already
if [ -n "$SUDO_USER" ] && [ "$SUDO_USER" != "root" ]; then
    usermod -aG audio "$SUDO_USER" 2>/dev/null || true
fi

exit 0
EOF
    
    chmod +x /usr/local/bin/miloOS-audio-optimize.sh
    
    # 5. Enable and start the service
    systemctl daemon-reload
    systemctl enable miloOS-audio-optimization.service 2>/dev/null || log_warn "Failed to enable audio optimization service"
    
    # Run optimizations now
    log_info "Applying runtime optimizations..."
    /usr/local/bin/miloOS-audio-optimize.sh
    
    # 6. Configure audio group limits and system limits
    log_info "Configuring system limits for audio production..."
    
    # Ensure audio group exists
    groupadd -f audio 2>/dev/null || true
    
    # Enhanced audio limits
    cat > /etc/security/limits.d/99-audio-production.conf << 'EOF'
# Audio production limits for miloOS
@audio   -  rtprio     99
@audio   -  memlock    unlimited
@audio   -  nice      -20
@audio   -  nofile     524288
@audio   soft  nproc      unlimited
@audio   hard  nproc      unlimited

# For all users (general improvements)
*        -  nofile     524288
*        soft  nproc      65536
*        hard  nproc      65536
EOF
    
    # Configure sysctl for audio production
    cat > /etc/sysctl.d/99-audio-production.conf << 'EOF'
# Audio production optimizations for miloOS
vm.swappiness = 10
fs.inotify.max_user_watches = 524288
kernel.shmmax = 2147483648
kernel.shmall = 2147483648
fs.file-max = 524288
EOF
    
    # Apply sysctl settings
    sysctl -p /etc/sysctl.d/99-audio-production.conf 2>/dev/null || true
    
    # 7. Add all existing users to audio and necessary groups
    log_info "Adding users to audio and production groups..."
    
    # Get all regular users (UID >= 1000, excluding nobody)
    for user in $(awk -F: '$3 >= 1000 && $3 < 65534 {print $1}' /etc/passwd); do
        if [ "$user" != "nobody" ]; then
            log_info "Adding $user to audio, video, and realtime groups..."
            usermod -aG audio,video "$user" 2>/dev/null || true
        fi
    done
    
    # Add SUDO_USER if available
    if [ -n "$SUDO_USER" ] && [ "$SUDO_USER" != "root" ]; then
        usermod -aG audio,video "$SUDO_USER" 2>/dev/null || true
        log_info "Added $SUDO_USER to audio and video groups"
    fi
    
    # 8. Configure polkit for power management (no password required)
    log_info "Configuring polkit for power management..."
    
    # Install polkit if not present
    if ! command -v pkaction &> /dev/null; then
        apt-get install -y policykit-1 2>/dev/null || log_warn "Could not install polkit"
    fi
    
    # Create polkit rule to allow power management without password
    mkdir -p /etc/polkit-1/rules.d
    cat > /etc/polkit-1/rules.d/50-miloOS-power.rules << 'EOF'
/* Allow users to shutdown, reboot, and suspend without password */
polkit.addRule(function(action, subject) {
    if ((action.id == "org.freedesktop.login1.power-off" ||
         action.id == "org.freedesktop.login1.power-off-multiple-sessions" ||
         action.id == "org.freedesktop.login1.reboot" ||
         action.id == "org.freedesktop.login1.reboot-multiple-sessions" ||
         action.id == "org.freedesktop.login1.suspend" ||
         action.id == "org.freedesktop.login1.suspend-multiple-sessions") &&
        subject.isInGroup("users")) {
        return polkit.Result.YES;
    }
});
EOF
    
    chmod 644 /etc/polkit-1/rules.d/50-miloOS-power.rules
    log_info "Polkit configuration for power management completed"
    
    log_info "Real-time audio optimization completed!"
    log_warn "Users have been added to audio group automatically"
    log_warn "Kernel parameters will be active after reboot"
}

install_audio_plugins() {
    log_step 10 $TOTAL_STEPS "Installing audio plugins..."
    
    log_info "Installing professional audio plugins for miloOS..."
    
    local PLUGIN_FAILED=""
    
    # Essential plugin suites
    log_info "Installing plugin suites..."
    for pkg in lsp-plugins lsp-plugins-lv2 lsp-plugins-vst \
               calf-plugins \
               x42-plugins \
               zam-plugins; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            PLUGIN_FAILED="$PLUGIN_FAILED $pkg"
        fi
    done
    
    # Synthesizers
    log_info "Installing synthesizers..."
    for pkg in zynaddsubfx zynaddsubfx-lv2 yoshimi; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            PLUGIN_FAILED="$PLUGIN_FAILED $pkg"
        fi
    done
    
    # Guitar processing
    log_info "Installing guitar processors..."
    for pkg in guitarix; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            PLUGIN_FAILED="$PLUGIN_FAILED $pkg"
        fi
    done
    
    # Drums and rhythm
    log_info "Installing drum machines..."
    for pkg in hydrogen drumgizmo; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            PLUGIN_FAILED="$PLUGIN_FAILED $pkg"
        fi
    done
    
    # Effects
    log_info "Installing effects..."
    for pkg in dragonfly-reverb eq10q; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            PLUGIN_FAILED="$PLUGIN_FAILED $pkg"
        fi
    done
    
    # Utilities
    log_info "Installing audio utilities..."
    for pkg in ardour qpwgraph; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            PLUGIN_FAILED="$PLUGIN_FAILED $pkg"
        fi
    done
    
    if [ -n "$PLUGIN_FAILED" ]; then
        log_warn "Some plugins failed to install:$PLUGIN_FAILED"
        log_warn "You can install them manually later"
    fi
    
    log_info "Audio plugins installation completed"
}

install_multimedia_apps() {
    log_step 11 $TOTAL_STEPS "Installing multimedia and productivity applications..."
    
    log_info "Installing multimedia applications for miloOS..."
    
    local APP_FAILED=""
    
    # Media players
    log_info "Installing media players..."
    for pkg in audacious audacious-plugins vlc; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            APP_FAILED="$APP_FAILED $pkg"
        fi
    done
    
    # Internet applications
    log_info "Installing internet applications..."
    for pkg in transmission filezilla; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            APP_FAILED="$APP_FAILED $pkg"
        fi
    done
    
    # Graphics applications
    log_info "Installing graphics applications..."
    for pkg in digikam gimp gimp-data-extras; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            APP_FAILED="$APP_FAILED $pkg"
        fi
    done
    
    # Video editor
    log_info "Installing video editor..."
    if apt-get install -y shotcut 2>/dev/null; then
        log_info "✓ shotcut installed"
    else
        log_warn "✗ shotcut failed to install"
        APP_FAILED="$APP_FAILED shotcut"
    fi
    
    # Compression tools
    log_info "Installing compression tools..."
    for pkg in p7zip-full p7zip-rar unrar rar unzip zip xz-utils bzip2 lzip lzop arj; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            APP_FAILED="$APP_FAILED $pkg"
        fi
    done
    
    # ISO mounting tools
    log_info "Installing ISO tools..."
    for pkg in fuseiso genisoimage; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            APP_FAILED="$APP_FAILED $pkg"
        fi
    done
    
    # Font manager
    log_info "Installing font manager..."
    if apt-get install -y font-manager 2>/dev/null; then
        log_info "✓ font-manager installed"
    else
        log_warn "✗ font-manager failed to install"
        APP_FAILED="$APP_FAILED font-manager"
    fi
    
    # FUSE and filesystem support
    log_info "Installing filesystem support..."
    for pkg in fuse3 ntfs-3g exfat-fuse exfatprogs hfsutils hfsprogs; do
        if apt-get install -y "$pkg" 2>/dev/null; then
            log_info "✓ $pkg installed"
        else
            log_warn "✗ $pkg failed to install"
            APP_FAILED="$APP_FAILED $pkg"
        fi
    done
    
    # Partition manager
    log_info "Installing partition manager..."
    if apt-get install -y gparted 2>/dev/null; then
        log_info "✓ gparted installed"
    else
        log_warn "✗ gparted failed to install"
        APP_FAILED="$APP_FAILED gparted"
    fi
    
    # Thunar archive plugin
    log_info "Installing Thunar archive plugin..."
    if apt-get install -y thunar-archive-plugin 2>/dev/null; then
        log_info "✓ thunar-archive-plugin installed"
    else
        log_warn "✗ thunar-archive-plugin failed to install"
        APP_FAILED="$APP_FAILED thunar-archive-plugin"
    fi
    
    # Upscayl - AI image upscaler
    log_info "Installing Upscayl..."
    local UPSCAYL_TEMP="/tmp/upscayl.deb"
    local UPSCAYL_URL=$(curl -s https://api.github.com/repos/upscayl/upscayl/releases/latest | grep "browser_download_url.*linux.deb" | cut -d '"' -f 4)
    
    if [ -n "$UPSCAYL_URL" ]; then
        log_info "Downloading Upscayl from GitHub..."
        if wget -q -O "$UPSCAYL_TEMP" "$UPSCAYL_URL" 2>/dev/null || curl -sL -o "$UPSCAYL_TEMP" "$UPSCAYL_URL" 2>/dev/null; then
            log_info "Installing Upscayl..."
            if dpkg -i "$UPSCAYL_TEMP" 2>/dev/null; then
                log_info "✓ upscayl installed"
                # Fix dependencies if needed
                apt-get install -f -y 2>/dev/null || true
            else
                log_warn "✗ upscayl failed to install"
                APP_FAILED="$APP_FAILED upscayl"
            fi
            rm -f "$UPSCAYL_TEMP"
        else
            log_warn "✗ Failed to download upscayl"
            APP_FAILED="$APP_FAILED upscayl"
        fi
    else
        log_warn "✗ Could not find upscayl download URL"
        APP_FAILED="$APP_FAILED upscayl"
    fi
    
    if [ -n "$APP_FAILED" ]; then
        log_warn "Some applications failed to install:$APP_FAILED"
        log_warn "You can install them manually later"
    fi
    
    log_info "Multimedia applications installation completed"
}

install_plymouth_theme() {
    log_step 12 $TOTAL_STEPS "Installing Plymouth boot theme..."
    
    # Install Plymouth if not present
    if ! command -v plymouth &> /dev/null; then
        log_info "Installing Plymouth..."
        apt-get install -y plymouth plymouth-themes 2>/dev/null || {
            log_warn "Failed to install Plymouth, skipping theme"
            return 0
        }
    fi
    
    # Download Apple Mac Plymouth theme by Navis Michael Bearly
    local TEMP_DIR="/tmp/plymouth-theme-$$"
    log_info "Downloading Apple Mac Plymouth theme..."
    
    mkdir -p "$TEMP_DIR" || {
        log_warn "Failed to create temp directory, skipping Plymouth theme"
        return 0
    }
    
    cd "$TEMP_DIR" || {
        log_warn "Failed to access temp directory, skipping Plymouth theme"
        rm -rf "$TEMP_DIR"
        return 0
    }
    
    # Download the theme from GitHub
    local DOWNLOAD_URL="https://github.com/navisjayaseelan/apple-mac-plymouth/archive/refs/heads/master.tar.gz"
    
    log_info "Downloading theme (this may take a moment)..."
    
    if command -v wget &> /dev/null; then
        wget -q --timeout=30 "$DOWNLOAD_URL" -O apple-mac-plymouth.tar.gz 2>/dev/null || {
            log_warn "Download failed with wget, trying curl..."
            if command -v curl &> /dev/null; then
                curl -L --max-time 30 -s "$DOWNLOAD_URL" -o apple-mac-plymouth.tar.gz 2>/dev/null || {
                    log_warn "Failed to download Plymouth theme, skipping"
                    cd "$CURRENT_DIR" || true
                    rm -rf "$TEMP_DIR"
                    return 0
                }
            else
                log_warn "Failed to download Plymouth theme, skipping"
                cd "$CURRENT_DIR" || true
                rm -rf "$TEMP_DIR"
                return 0
            fi
        }
    elif command -v curl &> /dev/null; then
        curl -L --max-time 30 -s "$DOWNLOAD_URL" -o apple-mac-plymouth.tar.gz 2>/dev/null || {
            log_warn "Failed to download Plymouth theme, skipping"
            cd "$CURRENT_DIR" || true
            rm -rf "$TEMP_DIR"
            return 0
        }
    else
        log_warn "Neither wget nor curl available, skipping Plymouth theme"
        cd "$CURRENT_DIR" || true
        rm -rf "$TEMP_DIR"
        return 0
    fi
    
    # Verify download
    if [ ! -f apple-mac-plymouth.tar.gz ] || [ ! -s apple-mac-plymouth.tar.gz ]; then
        log_warn "Downloaded file is empty or missing, skipping Plymouth theme"
        cd "$CURRENT_DIR" || true
        rm -rf "$TEMP_DIR"
        return 0
    fi
    
    log_info "Download completed"
    
    # Extract the theme
    log_info "Extracting Plymouth theme..."
    if tar -xzf apple-mac-plymouth.tar.gz 2>/dev/null; then
        # Try to find the extracted directory
        if [ -d "apple-mac-plymouth-master" ]; then
            cd apple-mac-plymouth-master || {
                log_warn "Failed to access extracted directory"
                cd "$CURRENT_DIR" || true
                rm -rf "$TEMP_DIR"
                return 0
            }
        else
            # Try wildcard match
            local EXTRACTED_DIR=$(ls -d apple-mac-plymouth-* 2>/dev/null | head -1)
            if [ -n "$EXTRACTED_DIR" ] && [ -d "$EXTRACTED_DIR" ]; then
                cd "$EXTRACTED_DIR" || {
                    log_warn "Failed to access extracted directory"
                    cd "$CURRENT_DIR" || true
                    rm -rf "$TEMP_DIR"
                    return 0
                }
            else
                log_warn "Could not find extracted Plymouth theme directory"
                cd "$CURRENT_DIR" || true
                rm -rf "$TEMP_DIR"
                return 0
            fi
        fi
        
        # Manual installation (faster and more reliable than running install.sh)
        log_info "Installing Apple Mac Plymouth theme..."
        
        if [ -d "apple-mac" ]; then
            # Copy theme files
            cp -R apple-mac /usr/share/plymouth/themes/ 2>/dev/null || {
                log_warn "Failed to copy Plymouth theme files"
                cd "$CURRENT_DIR" || true
                rm -rf "$TEMP_DIR"
                return 0
            }
            
            # Set permissions
            chmod -R 755 /usr/share/plymouth/themes/apple-mac 2>/dev/null || true
            
            # Set as default theme
            if command -v plymouth-set-default-theme &> /dev/null; then
                log_info "Setting apple-mac as default Plymouth theme..."
                plymouth-set-default-theme apple-mac 2>/dev/null && \
                    log_info "Plymouth theme set successfully" || \
                    log_warn "Failed to set Plymouth theme as default"
            fi
            
            # Update initramfs (this can take time)
            if command -v update-initramfs &> /dev/null; then
                log_info "Updating initramfs (this may take a minute)..."
                update-initramfs -u -k all 2>&1 | grep -v "^update-initramfs:" | head -20 || true
                log_info "Initramfs updated"
            fi
            
            log_info "Plymouth theme installed successfully"
        else
            log_warn "Theme directory 'apple-mac' not found in archive"
            log_warn "Skipping Plymouth theme installation"
        fi
    else
        log_warn "Failed to extract Plymouth theme archive"
    fi
    
    # Always return to original directory and cleanup
    cd "$CURRENT_DIR" || true
    rm -rf "$TEMP_DIR" 2>/dev/null || true
    
    log_info "Plymouth theme installation completed"
}

install_audio_config() {
    log_step 13 $TOTAL_STEPS "Installing AudioConfig tool..."
    
    if [ ! -d "$CURRENT_DIR/AudioConfig" ]; then
        log_warn "AudioConfig directory not found, skipping"
        return 0
    fi
    
    if [ ! -f "$CURRENT_DIR/AudioConfig/audio-config.py" ]; then
        log_warn "AudioConfig script not found, skipping"
        return 0
    fi
    
    # Install Python GTK dependencies if not present
    log_info "Checking Python GTK dependencies..."
    local MISSING_DEPS=""
    
    for pkg in python3-gi gir1.2-gtk-3.0; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            MISSING_DEPS="$MISSING_DEPS $pkg"
        fi
    done
    
    if [ -n "$MISSING_DEPS" ]; then
        log_info "Installing missing dependencies:$MISSING_DEPS"
        apt-get install -y $MISSING_DEPS 2>/dev/null || log_warn "Some dependencies failed to install"
    fi
    
    # Install the script
    log_info "Installing audio-config script..."
    install -m 755 "$CURRENT_DIR/AudioConfig/audio-config.py" /usr/local/bin/audio-config
    
    # Install icon
    if [ -f "$CURRENT_DIR/AudioConfig/audio-config.svg" ]; then
        log_info "Installing icon..."
        mkdir -p /usr/share/icons/hicolor/scalable/apps
        install -m 644 "$CURRENT_DIR/AudioConfig/audio-config.svg" /usr/share/icons/hicolor/scalable/apps/audio-config.svg
        
        # Update icon cache
        if command -v gtk-update-icon-cache &> /dev/null; then
            gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
        fi
    fi
    
    # Install desktop entry
    if [ -f "$CURRENT_DIR/AudioConfig/audio-config.desktop" ]; then
        log_info "Installing desktop entry..."
        install -m 644 "$CURRENT_DIR/AudioConfig/audio-config.desktop" /usr/share/applications/
        
        # Update desktop database
        if command -v update-desktop-database &> /dev/null; then
            update-desktop-database /usr/share/applications/ 2>/dev/null || true
        fi
    fi
    
    log_info "AudioConfig tool installed successfully"
    log_info "Users can run 'audio-config' or find it in the applications menu"
}

# Main execution
log_info "Starting miloOS resources installation..."
log_info "Current directory: $CURRENT_DIR"
echo ""

verify_system
install_debian_packages
install_gtk_themes
install_icon_themes
install_fonts
install_wallpaper
install_plank_theme
install_menus
rebrand_system
optimize_realtime_audio
install_audio_plugins
install_multimedia_apps
install_audio_config

# Disable Plymouth boot splash
log_info "Disabling Plymouth boot splash..."
systemctl disable plymouth.service 2>/dev/null || true
systemctl mask plymouth.service 2>/dev/null || true
log_info "Plymouth disabled"

echo ""
log_info "========================================="
log_info "All resources installed successfully!"
log_info "System has been rebranded to miloOS"
log_info "========================================="
log_warn "IMPORTANT: Please reboot your system for all changes to take effect"
echo ""
