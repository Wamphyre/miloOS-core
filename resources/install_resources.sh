#!/bin/bash
# Author: Wamphyre
# Description: Customized skinpack for XFCE4 to look like macOS
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
if [ "$EUID" -ne 0 ]; then
    error_exit "This script must be run as root (use sudo)"
fi

CURRENT_DIR="$PWD"
TOTAL_STEPS=10

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
    apt-get install -y \
        gtk2-engines-murrine gtk2-engines-pixbuf \
        plank catfish appmenu-gtk3-module dconf-cli \
        vala-panel-appmenu xfce4-appmenu-plugin \
        xfce4-notifyd \
        cifs-utils smbclient || {
        log_error "Package installation failed"
        log_error "Please check the error messages above"
        log_error "Common issues:"
        log_error "  - No internet connection"
        log_error "  - Repository not available"
        log_error "  - Package name changed in Debian 13"
        return 1
    }
    
    # Check and install PipeWire if not present
    log_info "Checking PipeWire installation..."
    if ! command -v pipewire &> /dev/null; then
        log_info "PipeWire not found, installing complete PipeWire stack..."
        apt-get install -y \
            pipewire pipewire-audio-client-libraries \
            pipewire-pulse pipewire-alsa pipewire-jack \
            wireplumber libspa-0.2-bluetooth libspa-0.2-jack \
            rtkit || {
            log_warn "PipeWire installation failed, continuing anyway"
        }
    else
        log_info "PipeWire already installed"
        # Ensure all components are installed
        apt-get install -y \
            pipewire-pulse pipewire-alsa pipewire-jack \
            wireplumber rtkit 2>/dev/null || true
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
    
    if [ -d "resources/milk" ]; then
        mkdir -p /usr/share/slim/themes
        cp -R resources/milk /usr/share/slim/themes/
        chown -R root:root /usr/share/slim/themes/milk
        log_info "SLiM theme installed"
    else
        log_warn "SLiM theme directory not found, skipping"
    fi
}

install_icon_themes() {
    log_step 3 $TOTAL_STEPS "Installing icon themes..."
    
    if [ ! -d "resources/icons/Cocoa" ]; then
        error_exit "Icon theme resources/icons/Cocoa not found!"
    fi
    
    cp -R resources/icons/Cocoa /usr/share/icons/
    chown -R root:root /usr/share/icons/Cocoa/
    
    if [ -f "resources/icons/catfish-symbolic.png" ]; then
        cp resources/icons/catfish-symbolic.png /usr/share/pixmaps/
        log_info "Catfish icon installed"
    fi
    
    # Update icon cache at the end
    if command -v gtk-update-icon-cache &> /dev/null; then
        log_info "Updating icon cache..."
        gtk-update-icon-cache -f /usr/share/icons/Cocoa/ 2>/dev/null || log_warn "Icon cache update failed"
    fi
    
    log_info "Icon themes installed"
}

install_fonts() {
    log_step 4 $TOTAL_STEPS "Installing fonts..."
    
    if [ ! -d "resources/fonts/Inter-Desktop" ]; then
        error_exit "Font directory resources/fonts/Inter-Desktop not found!"
    fi
    
    cp -R resources/fonts/Inter-Desktop /usr/share/fonts/
    chown -R root:root /usr/share/fonts/Inter-Desktop/
    
    # Update font cache
    if command -v fc-cache &> /dev/null; then
        log_info "Updating font cache..."
        fc-cache -f 2>/dev/null || log_warn "Font cache update failed"
    fi
    
    log_info "Fonts installed"
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
            ((installed_count++))
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
}

rebrand_system() {
    log_step 8 $TOTAL_STEPS "Rebranding system from Debian to miloOS..."
    
    # Backup original files
    local backup_dir="/root/debian-backup-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$backup_dir"
    log_info "Creating backup at: $backup_dir"
    
    # Save backup location for later reference
    echo "$backup_dir" > /root/.miloOS-last-backup
    
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
    
    # 5. /etc/debian_version - Keep for compatibility
    if [ -f "/etc/debian_version" ]; then
        cp /etc/debian_version "$backup_dir/debian_version.bak"
        echo "miloOS/1.0" > /etc/debian_version
        log_info "Updated /etc/debian_version"
    fi
    
    # 6. MOTD (Message of the Day)
    if [ -d "/etc/update-motd.d" ]; then
        # Backup existing MOTD scripts
        mkdir -p "$backup_dir/update-motd.d"
        cp -R /etc/update-motd.d/* "$backup_dir/update-motd.d/" 2>/dev/null || true
        
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
    
    # 7. Update GRUB bootloader (if exists)
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
    
    # 8. Plymouth theme (boot splash) - if plymouth is installed
    if command -v plymouth-set-default-theme &> /dev/null; then
        log_info "Plymouth detected, you may want to install a custom miloOS theme"
    fi
    
    # 9. LightDM/GDM greeter configuration
    if [ -f "/etc/lightdm/lightdm-gtk-greeter.conf" ]; then
        cp /etc/lightdm/lightdm-gtk-greeter.conf "$backup_dir/lightdm-gtk-greeter.conf.bak"
        
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
    log_info "Backup saved at: $backup_dir"
    log_warn "Note: Some changes require a reboot to take full effect"
}

optimize_realtime_audio() {
    log_step 9 $TOTAL_STEPS "Optimizing system for real-time audio..."
    
    local backup_dir=$(cat /root/.miloOS-last-backup 2>/dev/null || echo "/root/debian-backup-$(date +%Y%m%d-%H%M%S)")
    
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
    
    # Create WirePlumber low-latency configuration
    mkdir -p /etc/wireplumber/main.lua.d
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
        
        # Backup if not already backed up
        if [ ! -f "$backup_dir/grub.bak" ]; then
            cp /etc/default/grub "$backup_dir/grub.bak"
        fi
        
        # Audio profile: preempt=full nohz_full=all threadirqs
        # This provides fully preemptible kernel + no tick on all CPUs + threaded IRQs
        local AUDIO_PARAMS="preempt=full nohz_full=all threadirqs"
        
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
            update-grub 2>/dev/null || log_warn "Failed to update GRUB"
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
    
    # 6. Configure audio group limits
    log_info "Configuring audio group permissions..."
    
    # Ensure audio group exists
    groupadd -f audio 2>/dev/null || true
    
    log_info "Real-time audio optimization completed!"
    log_warn "IMPORTANT: Users need to be in the 'audio' group for real-time privileges"
    log_warn "Run: sudo usermod -aG audio \$USER (then logout/login)"
    log_warn "Kernel parameters will be active after reboot"
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

echo ""
log_info "========================================="
log_info "All resources installed successfully!"
log_info "System has been rebranded to miloOS"
log_info "========================================="
log_warn "IMPORTANT: Please reboot your system for all changes to take effect"
log_info "Backup location: $(cat /root/.miloOS-last-backup 2>/dev/null || echo 'Unknown')"
echo ""
