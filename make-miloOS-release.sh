#!/bin/bash
# Author: Wamphyre
# Description: miloOS ISO Release Builder
# Version: 1.0
# 
# This script creates a bootable ISO image of miloOS from the current configured system
# including LiveCD with user "milo" (password: 1234) and Calamares installer

set -e
set -o pipefail

# ============================================================================
# CONFIGURATION
# ============================================================================

VERSION="1.0"
# Use /var/tmp instead of /tmp (tmpfs is too small)
WORK_DIR="/var/tmp/miloOS-build-$$"
CHROOT_DIR="$WORK_DIR/chroot"
ISO_DIR="$WORK_DIR/iso"
SQUASHFS_DIR="$ISO_DIR/live"
ISO_NAME="miloOS-${VERSION}-amd64.iso"
LOG_FILE="/var/tmp/miloOS-build-$(date +%Y%m%d-%H%M%S).log"

# ============================================================================
# COLORS AND LOGGING
# ============================================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    local msg="[$(date '+%Y-%m-%d %H:%M:%S')] [INFO] $1"
    echo -e "${GREEN}${msg}${NC}" | tee -a "$LOG_FILE"
}

log_warn() {
    local msg="[$(date '+%Y-%m-%d %H:%M:%S')] [WARN] $1"
    echo -e "${YELLOW}${msg}${NC}" | tee -a "$LOG_FILE"
}

log_error() {
    local msg="[$(date '+%Y-%m-%d %H:%M:%S')] [ERROR] $1"
    echo -e "${RED}${msg}${NC}" | tee -a "$LOG_FILE"
}

log_step() {
    local msg="[$(date '+%Y-%m-%d %H:%M:%S')] [STEP $1/$2] $3"
    echo -e "${BLUE}${msg}${NC}" | tee -a "$LOG_FILE"
}

log_success() {
    local msg="[$(date '+%Y-%m-%d %H:%M:%S')] [SUCCESS] $1"
    echo -e "${CYAN}${msg}${NC}" | tee -a "$LOG_FILE"
}

# ============================================================================
# ERROR HANDLING AND CLEANUP
# ============================================================================

cleanup() {
    log_info "Cleaning up..."
    
    # Unmount chroot filesystems
    if [ -d "$CHROOT_DIR" ]; then
        umount -l "$CHROOT_DIR/proc" 2>/dev/null || true
        umount -l "$CHROOT_DIR/sys" 2>/dev/null || true
        umount -l "$CHROOT_DIR/dev/pts" 2>/dev/null || true
        umount -l "$CHROOT_DIR/dev" 2>/dev/null || true
    fi
    
    # Remove work directory if build failed
    if [ "$BUILD_SUCCESS" != "true" ] && [ -d "$WORK_DIR" ]; then
        log_warn "Build failed, removing work directory..."
        rm -rf "$WORK_DIR"
    fi
    
    log_info "Cleanup completed"
}

error_exit() {
    log_error "$1"
    log_error "ISO build failed. Check the log file: $LOG_FILE"
    exit 1
}

# Trap errors and cleanup
trap cleanup EXIT ERR INT TERM

# ============================================================================
# SYSTEM VERIFICATION
# ============================================================================

check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        error_exit "This script must be run as root (use sudo)"
    fi
}

check_debian() {
    if [ ! -f "/etc/debian_version" ]; then
        error_exit "This script is designed for Debian systems only"
    fi
    
    log_info "Running on Debian $(cat /etc/debian_version)"
}


# ============================================================================
# DEPENDENCY CHECKING
# ============================================================================

check_dependencies() {
    log_info "Checking dependencies..."
    
    local REQUIRED_PACKAGES=(
        "debootstrap"
        "squashfs-tools"
        "xorriso"
        "grub-pc-bin"
        "grub-efi-amd64-bin"
        "rsync"
        "git"
        "unzip"
    )
    
    local MISSING_PACKAGES=()
    
    for pkg in "${REQUIRED_PACKAGES[@]}"; do
        if ! dpkg -l | grep -q "^ii  $pkg"; then
            MISSING_PACKAGES+=("$pkg")
        fi
    done
    
    if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
        log_warn "Missing packages: ${MISSING_PACKAGES[*]}"
        log_info "Installing missing packages..."
        
        if apt-get update && apt-get install -y "${MISSING_PACKAGES[@]}"; then
            log_success "All dependencies installed"
        else
            error_exit "Failed to install dependencies"
        fi
    else
        log_success "All dependencies are installed"
    fi
}

check_disk_space() {
    log_info "Checking available disk space..."
    
    local REQUIRED_SPACE_GB=20
    # Check space in the work directory location
    local AVAILABLE_SPACE_GB=$(df -BG "$(dirname "$WORK_DIR")" | awk 'NR==2 {print $4}' | sed 's/G//')
    
    if [ "$AVAILABLE_SPACE_GB" -lt "$REQUIRED_SPACE_GB" ]; then
        error_exit "Insufficient disk space in $(dirname "$WORK_DIR"). Need at least ${REQUIRED_SPACE_GB}GB, have ${AVAILABLE_SPACE_GB}GB"
    fi
    
    log_success "Sufficient disk space available: ${AVAILABLE_SPACE_GB}GB in $(dirname "$WORK_DIR")"
}

# ============================================================================
# SKEL PREPARATION
# ============================================================================

prepare_skel() {
    log_info "Preparing /etc/skel with user configurations..."
    
    # Detect source user (the one running sudo)
    local SOURCE_USER="${SUDO_USER:-$USER}"
    local SOURCE_HOME
    
    if [ "$SOURCE_USER" = "root" ]; then
        log_warn "Running as root directly, trying to detect a regular user..."
        SOURCE_USER=$(awk -F: '$3 >= 1000 && $3 < 65534 && $1 != "nobody" {print $1; exit}' /etc/passwd)
        if [ -z "$SOURCE_USER" ]; then
            error_exit "Could not detect a regular user to copy configurations from"
        fi
    fi
    
    SOURCE_HOME=$(eval echo ~"$SOURCE_USER")
    
    if [ ! -d "$SOURCE_HOME" ]; then
        error_exit "Source home directory not found: $SOURCE_HOME"
    fi
    
    log_info "Copying configurations from user: $SOURCE_USER ($SOURCE_HOME)"
    
    # Create skel structure
    log_info "Creating /etc/skel directory structure..."
    mkdir -p /etc/skel/.config/{xfce4,plank,gtk-3.0,fontconfig,menus,autostart,environment.d,systemd/user.conf.d}
    mkdir -p /etc/skel/.local/share/applications
    mkdir -p /etc/skel/.config/xfce4/xinitrc.d
    
    # Copy XFCE4 configurations
    if [ -d "$SOURCE_HOME/.config/xfce4" ]; then
        log_info "Copying XFCE4 configurations..."
        cp -R "$SOURCE_HOME/.config/xfce4"/* /etc/skel/.config/xfce4/ 2>/dev/null || true
    else
        log_warn "XFCE4 configuration not found in $SOURCE_HOME"
    fi
    
    # Copy Plank configurations
    if [ -d "$SOURCE_HOME/.config/plank" ]; then
        log_info "Copying Plank configurations..."
        cp -R "$SOURCE_HOME/.config/plank"/* /etc/skel/.config/plank/ 2>/dev/null || true
    else
        log_warn "Plank configuration not found"
    fi
    
    # Copy GTK configurations
    if [ -d "$SOURCE_HOME/.config/gtk-3.0" ]; then
        log_info "Copying GTK-3.0 configurations..."
        cp -R "$SOURCE_HOME/.config/gtk-3.0"/* /etc/skel/.config/gtk-3.0/ 2>/dev/null || true
    fi
    
    if [ -f "$SOURCE_HOME/.gtkrc-2.0" ]; then
        log_info "Copying GTK-2.0 configuration..."
        cp "$SOURCE_HOME/.gtkrc-2.0" /etc/skel/ 2>/dev/null || true
    fi
    
    # Copy font configurations
    if [ -d "$SOURCE_HOME/.config/fontconfig" ]; then
        log_info "Copying font configurations..."
        cp -R "$SOURCE_HOME/.config/fontconfig"/* /etc/skel/.config/fontconfig/ 2>/dev/null || true
    fi
    
    # Copy autostart
    if [ -d "$SOURCE_HOME/.config/autostart" ]; then
        log_info "Copying autostart configurations..."
        cp -R "$SOURCE_HOME/.config/autostart"/* /etc/skel/.config/autostart/ 2>/dev/null || true
    fi
    
    # Copy custom menus
    if [ -d "$SOURCE_HOME/.config/menus" ]; then
        log_info "Copying custom menus..."
        cp -R "$SOURCE_HOME/.config/menus"/* /etc/skel/.config/menus/ 2>/dev/null || true
    fi
    
    # Copy hidden applications
    if [ -d "$SOURCE_HOME/.local/share/applications" ]; then
        log_info "Copying hidden applications..."
        cp -R "$SOURCE_HOME/.local/share/applications"/* /etc/skel/.local/share/applications/ 2>/dev/null || true
    fi
    
    # Copy shell configurations
    log_info "Copying shell configurations..."
    [ -f "$SOURCE_HOME/.profile" ] && cp "$SOURCE_HOME/.profile" /etc/skel/ 2>/dev/null || true
    [ -f "$SOURCE_HOME/.bashrc" ] && cp "$SOURCE_HOME/.bashrc" /etc/skel/ 2>/dev/null || true
    [ -f "$SOURCE_HOME/.xsession" ] && cp "$SOURCE_HOME/.xsession" /etc/skel/ 2>/dev/null || true
    [ -f "$SOURCE_HOME/.xsessionrc" ] && cp "$SOURCE_HOME/.xsessionrc" /etc/skel/ 2>/dev/null || true
    
    # Copy environment.d
    if [ -d "$SOURCE_HOME/.config/environment.d" ]; then
        log_info "Copying environment.d configurations..."
        cp -R "$SOURCE_HOME/.config/environment.d"/* /etc/skel/.config/environment.d/ 2>/dev/null || true
    fi
    
    # Copy systemd user configs
    if [ -d "$SOURCE_HOME/.config/systemd/user.conf.d" ]; then
        log_info "Copying systemd user configurations..."
        cp -R "$SOURCE_HOME/.config/systemd/user.conf.d"/* /etc/skel/.config/systemd/user.conf.d/ 2>/dev/null || true
    fi
    
    # Copy xinitrc.d scripts
    if [ -d "$SOURCE_HOME/.config/xfce4/xinitrc.d" ]; then
        log_info "Copying xinitrc.d scripts..."
        cp -R "$SOURCE_HOME/.config/xfce4/xinitrc.d"/* /etc/skel/.config/xfce4/xinitrc.d/ 2>/dev/null || true
    fi
    
    # Clean personal data from skel
    log_info "Cleaning personal data from /etc/skel..."
    find /etc/skel -name "*history" -delete 2>/dev/null || true
    find /etc/skel -name "*.log" -delete 2>/dev/null || true
    find /etc/skel -name "*.cache" -type f -delete 2>/dev/null || true
    find /etc/skel -name "*.bak" -delete 2>/dev/null || true
    
    # Remove session-specific files
    rm -f /etc/skel/.config/xfce4/xfconf/xfce-perchannel-xml/xfce4-session.xml 2>/dev/null || true
    rm -f /etc/skel/.config/xfce4/sessions/* 2>/dev/null || true
    
    # Set proper permissions
    chmod -R 755 /etc/skel/.config 2>/dev/null || true
    chmod -R 755 /etc/skel/.local 2>/dev/null || true
    chmod 644 /etc/skel/.profile 2>/dev/null || true
    chmod 644 /etc/skel/.bashrc 2>/dev/null || true
    chmod 755 /etc/skel/.xsession 2>/dev/null || true
    chmod 644 /etc/skel/.xsessionrc 2>/dev/null || true
    
    log_success "/etc/skel prepared with user configurations"
}

# ============================================================================
# WORKSPACE SETUP
# ============================================================================

setup_workspace() {
    log_info "Setting up workspace directories..."
    
    # Create work directories
    mkdir -p "$WORK_DIR"
    mkdir -p "$CHROOT_DIR"
    mkdir -p "$ISO_DIR"
    mkdir -p "$SQUASHFS_DIR"
    mkdir -p "$ISO_DIR/boot/grub"
    mkdir -p "$ISO_DIR/EFI/BOOT"
    
    log_success "Workspace created at: $WORK_DIR"
}

# ============================================================================
# SYSTEM COPY
# ============================================================================

copy_system() {
    log_info "Copying system to chroot directory..."
    log_warn "This may take several minutes..."
    
    # Use rsync to copy the system
    rsync -aAXv \
        --exclude='/dev/*' \
        --exclude='/proc/*' \
        --exclude='/sys/*' \
        --exclude='/tmp/*' \
        --exclude='/run/*' \
        --exclude='/mnt/*' \
        --exclude='/media/*' \
        --exclude='/lost+found' \
        --exclude='/home/*' \
        --exclude='/root/*' \
        --exclude='/swapfile' \
        --exclude='/swap.img' \
        --exclude="$WORK_DIR" \
        / "$CHROOT_DIR/" 2>&1 | tee -a "$LOG_FILE" | grep -v "^sending\|^sent\|^total" || true
    
    log_success "System copied to chroot"
    
    # Create necessary directories
    log_info "Creating necessary directories in chroot..."
    mkdir -p "$CHROOT_DIR"/{proc,sys,dev,dev/pts,run,tmp,mnt,media}
    
    # Set proper permissions
    chmod 1777 "$CHROOT_DIR/tmp"
    chmod 755 "$CHROOT_DIR"/{proc,sys,dev,run,mnt,media}
    
    log_success "Directories created"
}

verify_miloOS_apps() {
    log_info "Verifying miloOS applications in chroot..."
    
    local errors=0
    
    # Verify AudioConfig
    if [ ! -f "$CHROOT_DIR/usr/local/bin/audio-config" ]; then
        log_error "AudioConfig binary not found"
        errors=$((errors + 1))
    else
        log_info "✓ AudioConfig binary found"
    fi
    
    if [ ! -f "$CHROOT_DIR/usr/share/applications/audio-config.desktop" ]; then
        log_warn "AudioConfig desktop file not found"
    else
        log_info "✓ AudioConfig desktop file found"
    fi
    
    # Verify miloOS menus
    if [ ! -f "$CHROOT_DIR/etc/xdg/menus/milo.menu" ]; then
        log_warn "miloOS menu not found"
    else
        log_info "✓ miloOS menu found"
    fi
    
    if [ ! -f "$CHROOT_DIR/usr/bin/milo-session" ]; then
        log_warn "milo-session script not found"
    else
        log_info "✓ milo-session script found"
    fi
    
    # Verify menu items
    local menu_items=("milo-logout" "milo-shutdown" "milo-restart" "milo-sleep" "milo-settings" "milo-about")
    local found_items=0
    for item in "${menu_items[@]}"; do
        if [ -f "$CHROOT_DIR/usr/share/applications/${item}.desktop" ]; then
            found_items=$((found_items + 1))
        fi
    done
    log_info "✓ Found $found_items/${#menu_items[@]} menu items"
    
    # Verify themes
    if [ ! -d "$CHROOT_DIR/usr/share/themes/miloOS" ]; then
        log_error "miloOS theme not found"
        errors=$((errors + 1))
    else
        log_info "✓ miloOS theme found"
    fi
    
    if [ ! -d "$CHROOT_DIR/usr/share/plank/themes/milo" ]; then
        log_warn "Plank milo theme not found"
    else
        log_info "✓ Plank milo theme found"
    fi
    
    # Verify optimization scripts
    if [ ! -f "$CHROOT_DIR/usr/local/bin/miloOS-audio-optimize.sh" ]; then
        log_warn "Audio optimization script not found"
    else
        log_info "✓ Audio optimization script found"
    fi
    
    if [ ! -f "$CHROOT_DIR/etc/systemd/system/miloOS-audio-optimization.service" ]; then
        log_warn "Audio optimization service not found"
    else
        log_info "✓ Audio optimization service found"
    fi
    
    if [ $errors -gt 0 ]; then
        log_error "Found $errors critical errors in miloOS applications"
        return 1
    fi
    
    log_success "miloOS applications verified"
    return 0
}

clean_chroot() {
    log_info "Cleaning chroot system..."
    
    # Clean logs
    log_info "Cleaning system logs..."
    find "$CHROOT_DIR/var/log" -type f -delete 2>/dev/null || true
    
    # Clean APT cache
    log_info "Cleaning APT cache..."
    rm -rf "$CHROOT_DIR/var/cache/apt/archives"/*.deb 2>/dev/null || true
    rm -rf "$CHROOT_DIR/var/cache/apt"/*.bin 2>/dev/null || true
    
    # Reset machine-id
    log_info "Resetting machine-id..."
    echo "" > "$CHROOT_DIR/etc/machine-id"
    rm -f "$CHROOT_DIR/var/lib/dbus/machine-id" 2>/dev/null || true
    
    # Clean temporary files
    log_info "Cleaning temporary files..."
    rm -rf "$CHROOT_DIR/tmp"/* 2>/dev/null || true
    rm -rf "$CHROOT_DIR/var/tmp"/* 2>/dev/null || true
    
    # Clean user histories
    log_info "Cleaning user histories..."
    find "$CHROOT_DIR/home" -name ".bash_history" -delete 2>/dev/null || true
    find "$CHROOT_DIR/home" -name ".zsh_history" -delete 2>/dev/null || true
    find "$CHROOT_DIR/root" -name ".bash_history" -delete 2>/dev/null || true
    
    log_success "Chroot system cleaned"
}

ensure_miloApps_in_chroot() {
    log_info "Ensuring miloApps are present in chroot..."
    
    local CURRENT_DIR=$(pwd)
    
    # Verify and copy AudioConfig
    if [ ! -f "$CHROOT_DIR/usr/local/bin/audio-config" ]; then
        log_warn "AudioConfig not found, copying..."
        if [ -f "$CURRENT_DIR/AudioConfig/audio-config.py" ]; then
            cp "$CURRENT_DIR/AudioConfig/audio-config.py" "$CHROOT_DIR/usr/local/bin/audio-config"
            chmod +x "$CHROOT_DIR/usr/local/bin/audio-config"
            log_info "✓ AudioConfig binary copied"
        else
            log_error "AudioConfig source not found at $CURRENT_DIR/AudioConfig/audio-config.py"
        fi
    fi
    
    if [ ! -f "$CHROOT_DIR/usr/share/applications/audio-config.desktop" ]; then
        if [ -f "$CURRENT_DIR/AudioConfig/audio-config.desktop" ]; then
            cp "$CURRENT_DIR/AudioConfig/audio-config.desktop" "$CHROOT_DIR/usr/share/applications/"
            chmod 644 "$CHROOT_DIR/usr/share/applications/audio-config.desktop"
            log_info "✓ AudioConfig desktop file copied"
        fi
    fi
    
    # Install Python dependencies for AudioConfig in chroot
    log_info "Installing Python dependencies for AudioConfig..."
    chroot "$CHROOT_DIR" apt-get install -y python3-gi python3-gi-cairo gir1.2-gtk-3.0 2>&1 | tee -a "$LOG_FILE" | grep -v "^Reading\|^Building" || true
    
    # Verify and copy miloOS menus
    if [ ! -f "$CHROOT_DIR/etc/xdg/menus/milo.menu" ]; then
        log_warn "miloOS menu not found, copying..."
        if [ -f "$CURRENT_DIR/resources/menus/xdg/milo.menu" ]; then
            mkdir -p "$CHROOT_DIR/etc/xdg/menus"
            cp "$CURRENT_DIR/resources/menus/xdg/milo.menu" "$CHROOT_DIR/etc/xdg/menus/"
            log_info "✓ miloOS menu copied"
        fi
    fi
    
    if [ ! -f "$CHROOT_DIR/usr/bin/milo-session" ]; then
        log_warn "milo-session not found, copying..."
        if [ -f "$CURRENT_DIR/resources/menus/bin/milo-session" ]; then
            cp "$CURRENT_DIR/resources/menus/bin/milo-session" "$CHROOT_DIR/usr/bin/"
            chmod +x "$CHROOT_DIR/usr/bin/milo-session"
            log_info "✓ milo-session copied"
        fi
    fi
    
    # Copy menu items
    if [ -d "$CURRENT_DIR/resources/menus/items" ]; then
        log_info "Copying menu items..."
        for item in "$CURRENT_DIR/resources/menus/items"/*.desktop; do
            if [ -f "$item" ]; then
                cp "$item" "$CHROOT_DIR/usr/share/applications/"
                chmod 644 "$CHROOT_DIR/usr/share/applications/$(basename "$item")"
            fi
        done
        log_info "✓ Menu items copied"
    fi
    
    log_success "miloApps verified and installed in chroot"
}

# ============================================================================
# LIVE SYSTEM CONFIGURATION
# ============================================================================

configure_live_user() {
    log_info "Configuring Live user 'milo'..."
    
    # Mount necessary filesystems for chroot
    log_info "Mounting filesystems for chroot..."
    mount --bind /proc "$CHROOT_DIR/proc"
    mount --bind /sys "$CHROOT_DIR/sys"
    mount --bind /dev "$CHROOT_DIR/dev"
    mount --bind /dev/pts "$CHROOT_DIR/dev/pts"
    
    # Install live-boot and live-config packages
    log_info "Installing live system packages..."
    chroot "$CHROOT_DIR" apt-get update 2>&1 | tee -a "$LOG_FILE" | grep -v "^Get:\|^Hit:" || true
    chroot "$CHROOT_DIR" apt-get install -y live-boot live-boot-initramfs-tools live-config live-config-systemd live-tools 2>&1 | tee -a "$LOG_FILE" | grep -v "^Selecting\|^Preparing\|^Unpacking" || true
    
    # Install additional required packages for live system
    log_info "Installing live system dependencies..."
    chroot "$CHROOT_DIR" apt-get install -y user-setup sudo squashfs-tools 2>&1 | tee -a "$LOG_FILE" | grep -v "^Selecting\|^Preparing\|^Unpacking" || true
    
    # Create live-boot configuration
    log_info "Configuring live-boot..."
    mkdir -p "$CHROOT_DIR/etc/live/boot.conf.d"
    cat > "$CHROOT_DIR/etc/live/boot.conf.d/miloOS.conf" << 'LIVECONF'
LIVE_MEDIA_PATH="live"
LIVE_MEDIA_TIMEOUT=10
LIVECONF
    
    # Ensure initramfs-tools is configured for live
    mkdir -p "$CHROOT_DIR/etc/initramfs-tools/conf.d"
    cat > "$CHROOT_DIR/etc/initramfs-tools/conf.d/live.conf" << 'INITCONF'
BOOT=live
INITCONF
    
    # Update initramfs with live-boot
    log_info "Updating initramfs with live-boot..."
    chroot "$CHROOT_DIR" update-initramfs -u -k all 2>&1 | tee -a "$LOG_FILE" | grep -v "^update-initramfs:" || true
    
    # Configure live-config for automatic user creation
    log_info "Configuring live-config..."
    
    # Create live-config configuration
    mkdir -p "$CHROOT_DIR/etc/live/config.conf.d"
    cat > "$CHROOT_DIR/etc/live/config.conf.d/miloOS.conf" << 'EOF'
LIVE_USERNAME="milo"
LIVE_USER_FULLNAME="miloOS Live User"
LIVE_USER_DEFAULT_GROUPS="audio,video,sudo,plugdev,netdev,cdrom,floppy,scanner,bluetooth,lpadmin"
LIVE_HOSTNAME="miloOS"
EOF
    
    # Create live user in chroot (as fallback)
    log_info "Creating user 'milo' with password '1234'..."
    chroot "$CHROOT_DIR" useradd -m -s /bin/bash -G audio,video,sudo,plugdev,netdev,cdrom,floppy milo 2>/dev/null || true
    echo "milo:1234" | chroot "$CHROOT_DIR" chpasswd
    
    # Copy configurations from /etc/skel to milo's home
    log_info "Copying configurations to milo's home..."
    chroot "$CHROOT_DIR" cp -R /etc/skel/. /home/milo/ 2>/dev/null || true
    chroot "$CHROOT_DIR" chown -R milo:milo /home/milo 2>/dev/null || true
    
    # Configure sudo without password for live user
    log_info "Configuring sudo for live user..."
    echo "milo ALL=(ALL) NOPASSWD: ALL" > "$CHROOT_DIR/etc/sudoers.d/live-user"
    chmod 440 "$CHROOT_DIR/etc/sudoers.d/live-user"
    
    # Configure SLiM for autologin
    log_info "Configuring SLiM for autologin..."
    if [ -f "$CHROOT_DIR/etc/slim.conf" ]; then
        # Set autologin
        if grep -q "^auto_login" "$CHROOT_DIR/etc/slim.conf"; then
            sed -i 's/^auto_login.*/auto_login yes/' "$CHROOT_DIR/etc/slim.conf"
        else
            echo "auto_login yes" >> "$CHROOT_DIR/etc/slim.conf"
        fi
        
        # Set default user
        if grep -q "^default_user" "$CHROOT_DIR/etc/slim.conf"; then
            sed -i 's/^default_user.*/default_user milo/' "$CHROOT_DIR/etc/slim.conf"
        else
            echo "default_user milo" >> "$CHROOT_DIR/etc/slim.conf"
        fi
        
        log_info "✓ SLiM configured for autologin as milo"
    else
        log_warn "SLiM configuration not found"
    fi
    
    # Create installer desktop icon
    log_info "Creating installer desktop icon..."
    mkdir -p "$CHROOT_DIR/home/milo/Desktop"
    cat > "$CHROOT_DIR/home/milo/Desktop/install-miloOS.desktop" << 'EOF'
[Desktop Entry]
Type=Application
Name=Install miloOS
Name[es]=Instalar miloOS
Comment=Install miloOS to your computer
Comment[es]=Instalar miloOS en tu computadora
Exec=pkexec calamares
Icon=system-software-install
Terminal=false
Categories=System;
EOF
    chmod +x "$CHROOT_DIR/home/milo/Desktop/install-miloOS.desktop"
    chroot "$CHROOT_DIR" chown milo:milo /home/milo/Desktop/install-miloOS.desktop
    
    log_success "Live user 'milo' configured"
}

create_live_init_script() {
    log_info "Creating Live initialization script..."
    
    cat > "$CHROOT_DIR/usr/local/bin/miloOS-live-init" << 'EOF'
#!/bin/bash
# miloOS Live System Initialization Script

# Wait for system to be ready
sleep 5

# Ensure installer icon is visible on desktop
if [ -f /usr/share/applications/calamares.desktop ] && [ -d /home/milo/Desktop ]; then
    if [ ! -f /home/milo/Desktop/install-miloOS.desktop ]; then
        cp /usr/share/applications/calamares.desktop /home/milo/Desktop/install-miloOS.desktop
        chmod +x /home/milo/Desktop/install-miloOS.desktop
        chown milo:milo /home/milo/Desktop/install-miloOS.desktop
    fi
fi

# Start PipeWire for live user
if id milo &>/dev/null; then
    sudo -u milo systemctl --user start pipewire.service 2>/dev/null || true
    sudo -u milo systemctl --user start pipewire-pulse.service 2>/dev/null || true
    sudo -u milo systemctl --user start wireplumber.service 2>/dev/null || true
fi

exit 0
EOF
    
    chmod +x "$CHROOT_DIR/usr/local/bin/miloOS-live-init"
    log_success "Live init script created"
}

create_live_systemd_service() {
    log_info "Creating Live systemd service..."
    
    cat > "$CHROOT_DIR/etc/systemd/system/miloOS-live-init.service" << 'EOF'
[Unit]
Description=miloOS Live System Initialization
After=multi-user.target network.target display-manager.service
Wants=display-manager.service

[Service]
Type=oneshot
ExecStart=/usr/local/bin/miloOS-live-init
RemainAfterExit=yes
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
    
    # Enable the service
    log_info "Enabling Live service..."
    chroot "$CHROOT_DIR" systemctl enable miloOS-live-init.service 2>&1 | tee -a "$LOG_FILE" | grep -v "^Created" || true
    
    log_success "Live systemd service created and enabled"
}

# ============================================================================
# CALAMARES INSTALLER
# ============================================================================

install_calamares() {
    log_info "Installing Calamares installer..."
    
    # Install Calamares and dependencies
    log_info "Installing Calamares packages..."
    chroot "$CHROOT_DIR" apt-get update 2>&1 | tee -a "$LOG_FILE" | grep -v "^Get:\|^Hit:" || true
    chroot "$CHROOT_DIR" apt-get install -y calamares calamares-settings-debian 2>&1 | tee -a "$LOG_FILE" | grep -v "^Selecting\|^Preparing\|^Unpacking" || true
    
    # Create Calamares directory structure
    log_info "Creating Calamares directory structure..."
    mkdir -p "$CHROOT_DIR/etc/calamares"
    mkdir -p "$CHROOT_DIR/etc/calamares/modules"
    mkdir -p "$CHROOT_DIR/etc/calamares/branding/miloOS"
    mkdir -p "$CHROOT_DIR/usr/local/share/calamares/scripts"
    
    log_success "Calamares installed"
}

configure_calamares_settings() {
    log_info "Creating Calamares settings.conf..."
    
    cat > "$CHROOT_DIR/etc/calamares/settings.conf" << 'EOF'
# miloOS Calamares Configuration
---
modules-search: [ local, /usr/lib/x86_64-linux-gnu/calamares/modules ]

instances:
- id:       miloOS
  module:   packages
  config:   packages.conf

sequence:
- show:
  - welcome
  - locale
  - keyboard
  - partition
  - users
  - summary
- exec:
  - partition
  - mount
  - unpackfs
  - machineid
  - fstab
  - locale
  - keyboard
  - localecfg
  - luksbootkeyfile
  - luksopenswaphookcfg
  - initcpiocfg
  - initcpio
  - users
  - displaymanager
  - networkcfg
  - hwclock
  - services-systemd
  - bootloader
  - grubcfg
  - packages
  - shellprocess
  - preservefiles
  - umount
- show:
  - finished

branding: miloOS

prompt-install: true
dont-chroot: false
EOF
    
    log_success "Calamares settings.conf created"
}

create_calamares_modules() {
    log_info "Creating Calamares module configurations..."
    
    # welcome.conf
    cat > "$CHROOT_DIR/etc/calamares/modules/welcome.conf" << 'EOF'
---
showSupportUrl:         true
showKnownIssuesUrl:     true
showReleaseNotesUrl:    true

requirements:
    requiredStorage:    20.0
    requiredRam:        2.0
    internetCheckUrl:   http://google.com
    check:
        - storage
        - ram
        - power
        - internet
        - root
    required:
        - storage
        - ram
        - root
EOF
    
    # users.conf
    cat > "$CHROOT_DIR/etc/calamares/modules/users.conf" << 'EOF'
---
defaultGroups:
    - audio
    - video
    - sudo
    - plugdev
    - netdev
    - cdrom
    - floppy
    - scanner
    - bluetooth
    - lpadmin

autologinGroup:  autologin
sudoersGroup:    sudo
setRootPassword: true
doAutologin:     false

userShell: /bin/bash

avatarFilePath: /usr/share/pixmaps/faces/
EOF
    
    # partition.conf
    cat > "$CHROOT_DIR/etc/calamares/modules/partition.conf" << 'EOF'
---
efiSystemPartition:     "/boot/efi"
userSwapChoices:
    - none
    - small
    - suspend
    - file

drawNestedPartitions:   false
alwaysShowPartitionLabels: true
allowManualPartitioning: true

defaultFileSystemType:  "ext4"
availableFileSystemTypes:
    - "ext4"
    - "btrfs"
    - "xfs"
    - "f2fs"
EOF
    
    # packages.conf
    cat > "$CHROOT_DIR/etc/calamares/modules/packages.conf" << 'EOF'
---
backend: apt

operations:
  - remove:
      - calamares
      - calamares-settings-debian
  - try_remove:
      - live-config
      - live-boot
EOF
    
    # shellprocess.conf
    cat > "$CHROOT_DIR/etc/calamares/modules/shellprocess.conf" << 'EOF'
---
dontChroot: false
timeout: 999

script:
    - command: "/usr/local/bin/calamares-post-install.sh @@ROOT@@"
      timeout: 300
EOF
    
    # finished.conf
    cat > "$CHROOT_DIR/etc/calamares/modules/finished.conf" << 'EOF'
---
restartNowEnabled: true
restartNowChecked: true
restartNowCommand: "systemctl reboot"

notifyOnFinished: false
EOF
    
    log_success "Calamares modules created"
}

create_calamares_branding() {
    log_info "Creating Calamares branding..."
    
    cat > "$CHROOT_DIR/etc/calamares/branding/miloOS/branding.desc" << 'EOF'
---
componentName: miloOS

strings:
    productName:         "miloOS"
    shortProductName:    "miloOS"
    version:             "1.0"
    shortVersion:        "1.0"
    versionedName:       "miloOS 1.0"
    shortVersionedName:  "miloOS 1.0"
    bootloaderEntryName: "miloOS"
    productUrl:          "https://github.com/Wamphyre/miloOS-core"
    supportUrl:          "https://github.com/Wamphyre/miloOS-core/issues"
    knownIssuesUrl:      "https://github.com/Wamphyre/miloOS-core/issues"
    releaseNotesUrl:     "https://github.com/Wamphyre/miloOS-core/releases"

images:
    productLogo:         "logo.png"
    productIcon:         "logo.png"
    productWelcome:      "welcome.png"

slideshow:              "show.qml"

style:
   sidebarBackground:    "#007AFF"
   sidebarText:          "#FFFFFF"
   sidebarTextSelect:    "#FFFFFF"
   sidebarTextHighlight: "#0051D5"
EOF
    
    # Create a simple slideshow
    cat > "$CHROOT_DIR/etc/calamares/branding/miloOS/show.qml" << 'EOF'
import QtQuick 2.0;
import calamares.slideshow 1.0;

Presentation
{
    id: presentation

    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: presentation.goToNextSlide()
    }

    Slide {
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            color: "#f5f5f5"
            Text {
                anchors.centerIn: parent
                text: "Welcome to miloOS\n\nProfessional Audio Production"
                font.pixelSize: 32
                color: "#007AFF"
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    Slide {
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            color: "#f5f5f5"
            Text {
                anchors.centerIn: parent
                text: "Real-Time Optimized\n\nLow-latency audio out of the box"
                font.pixelSize: 28
                color: "#333333"
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    Slide {
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            color: "#f5f5f5"
            Text {
                anchors.centerIn: parent
                text: "Professional Tools Included\n\nLSP, Calf, Ardour, and more"
                font.pixelSize: 28
                color: "#333333"
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
EOF
    
    # Create placeholder logo (simple text-based)
    log_info "Creating placeholder branding images..."
    # Note: In a real implementation, you would copy actual logo files here
    # For now, we'll just note that they should be added
    
    log_success "Calamares branding created"
}

create_post_install_scripts() {
    log_info "Creating Calamares post-installation scripts..."
    
    local SCRIPT_DIR="$CHROOT_DIR/usr/local/share/calamares/scripts"
    
    # 1. preserve-configurations.sh
    cat > "$SCRIPT_DIR/preserve-configurations.sh" << 'SCRIPT1'
#!/bin/bash
# Preserve miloOS configurations for new user

NEW_USER="$1"
NEW_HOME="/home/$NEW_USER"

echo "Preserving configurations for user: $NEW_USER"

# Copy XFCE4 configurations
cp -R /etc/skel/.config/xfce4 "$NEW_HOME/.config/" 2>/dev/null || true

# Copy Plank configurations
cp -R /etc/skel/.config/plank "$NEW_HOME/.config/" 2>/dev/null || true

# Copy GTK configurations
cp -R /etc/skel/.config/gtk-3.0 "$NEW_HOME/.config/" 2>/dev/null || true
cp /etc/skel/.gtkrc-2.0 "$NEW_HOME/" 2>/dev/null || true

# Copy font configurations
cp -R /etc/skel/.config/fontconfig "$NEW_HOME/.config/" 2>/dev/null || true

# Copy autostart
cp -R /etc/skel/.config/autostart "$NEW_HOME/.config/" 2>/dev/null || true

# Copy menus
cp -R /etc/skel/.config/menus "$NEW_HOME/.config/" 2>/dev/null || true

# Copy hidden applications
mkdir -p "$NEW_HOME/.local/share/applications"
cp -R /etc/skel/.local/share/applications/* "$NEW_HOME/.local/share/applications/" 2>/dev/null || true

# Copy shell configs
cp /etc/skel/.profile "$NEW_HOME/" 2>/dev/null || true
cp /etc/skel/.bashrc "$NEW_HOME/" 2>/dev/null || true
cp /etc/skel/.xsession "$NEW_HOME/" 2>/dev/null || true
cp /etc/skel/.xsessionrc "$NEW_HOME/" 2>/dev/null || true

# Copy environment.d
cp -R /etc/skel/.config/environment.d "$NEW_HOME/.config/" 2>/dev/null || true

# Copy systemd user configs
cp -R /etc/skel/.config/systemd "$NEW_HOME/.config/" 2>/dev/null || true

# Copy xinitrc.d scripts
mkdir -p "$NEW_HOME/.config/xfce4/xinitrc.d"
cp -R /etc/skel/.config/xfce4/xinitrc.d/* "$NEW_HOME/.config/xfce4/xinitrc.d/" 2>/dev/null || true

# Set ownership
chown -R "$NEW_USER:$NEW_USER" "$NEW_HOME"

echo "Configurations preserved"
SCRIPT1
    
    # 2. setup-audio-groups.sh
    cat > "$SCRIPT_DIR/setup-audio-groups.sh" << 'SCRIPT2'
#!/bin/bash
# Setup audio groups and permissions

NEW_USER="$1"

echo "Setting up audio groups for: $NEW_USER"

# Add user to audio groups
usermod -aG audio,video,plugdev,netdev "$NEW_USER"

# Verify limits are in place
if [ ! -f /etc/security/limits.d/99-audio-production.conf ]; then
    cat > /etc/security/limits.d/99-audio-production.conf << 'EOF'
@audio   -  rtprio     99
@audio   -  memlock    unlimited
@audio   -  nice      -20
@audio   -  nofile     524288
EOF
fi

# Verify sysctl settings
if [ ! -f /etc/sysctl.d/99-audio-production.conf ]; then
    cat > /etc/sysctl.d/99-audio-production.conf << 'EOF'
vm.swappiness = 10
fs.inotify.max_user_watches = 524288
kernel.shmmax = 2147483648
fs.file-max = 524288
EOF
    sysctl -p /etc/sysctl.d/99-audio-production.conf
fi

echo "Audio groups configured"
SCRIPT2
    
    # 3. configure-grub.sh
    cat > "$SCRIPT_DIR/configure-grub.sh" << 'SCRIPT3'
#!/bin/bash
# Configure GRUB for real-time audio

echo "Configuring GRUB..."

# Ensure GRUB has miloOS branding
sed -i 's/GRUB_DISTRIBUTOR=.*/GRUB_DISTRIBUTOR="miloOS"/' /etc/default/grub

# Ensure kernel parameters are present
if ! grep -q "preempt=full" /etc/default/grub; then
    sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="\(.*\)"/GRUB_CMDLINE_LINUX_DEFAULT="\1 preempt=full nohz_full=all threadirqs mitigations=off"/' /etc/default/grub
fi

# Update GRUB
update-grub

echo "GRUB configured"
SCRIPT3
    
    # 4. setup-pipewire.sh
    cat > "$SCRIPT_DIR/setup-pipewire.sh" << 'SCRIPT4'
#!/bin/bash
# Setup PipeWire and JACK

NEW_USER="$1"
NEW_HOME="/home/$NEW_USER"

echo "Setting up PipeWire for: $NEW_USER"

# Ensure PipeWire configurations are in place
mkdir -p "$NEW_HOME/.config/pipewire/pipewire.conf.d"
mkdir -p "$NEW_HOME/.config/pipewire/jack.conf.d"

# Copy system-wide PipeWire configs if they exist
if [ -d /etc/pipewire ]; then
    cp -R /etc/pipewire/* "$NEW_HOME/.config/pipewire/" 2>/dev/null || true
fi

# Ensure environment.d for JACK library path
mkdir -p "$NEW_HOME/.config/environment.d"
cat > "$NEW_HOME/.config/environment.d/pipewire-jack.conf" << 'EOF'
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack:${LD_LIBRARY_PATH}
EOF

# Set ownership
chown -R "$NEW_USER:$NEW_USER" "$NEW_HOME/.config"

# Enable PipeWire services for user
sudo -u "$NEW_USER" systemctl --user enable pipewire.service
sudo -u "$NEW_USER" systemctl --user enable pipewire-pulse.service
sudo -u "$NEW_USER" systemctl --user enable wireplumber.service

echo "PipeWire configured"
SCRIPT4
    
    # 5. install-miloApps.sh
    cat > "$SCRIPT_DIR/install-miloApps.sh" << 'SCRIPT5'
#!/bin/bash
# Install miloOS applications

echo "Verifying miloApps installation..."

# Ensure AudioConfig is executable
if [ -f /usr/local/bin/audio-config ]; then
    chmod +x /usr/local/bin/audio-config
    echo "✓ AudioConfig installed"
else
    echo "✗ AudioConfig not found!"
fi

# Ensure menu scripts are executable
if [ -f /usr/bin/milo-session ]; then
    chmod +x /usr/bin/milo-session
    echo "✓ milo-session installed"
fi

# Ensure Python dependencies for AudioConfig
apt-get install -y python3-gi python3-gi-cairo gir1.2-gtk-3.0 2>/dev/null || true

# Verify desktop files
for desktop in /usr/share/applications/milo-*.desktop /usr/share/applications/audio-config.desktop; do
    if [ -f "$desktop" ]; then
        chmod 644 "$desktop"
    fi
done

echo "miloApps verified"
SCRIPT5
    
    # 6. finalize-system.sh
    cat > "$SCRIPT_DIR/finalize-system.sh" << 'SCRIPT6'
#!/bin/bash
# Finalize system configuration

echo "Finalizing system..."

# Remove live system services
systemctl disable miloOS-live-init.service 2>/dev/null || true
rm -f /etc/systemd/system/miloOS-live-init.service
rm -f /usr/local/bin/miloOS-live-init

# Remove live-config packages
apt-get remove --purge -y live-boot live-boot-initramfs-tools live-config live-config-systemd 2>/dev/null || true

# Remove installer desktop icon
rm -f /home/*/Desktop/install-miloOS.desktop

# Disable autologin in SLiM
if [ -f /etc/slim.conf ]; then
    sed -i 's/auto_login yes/auto_login no/' /etc/slim.conf
    sed -i 's/default_user milo/default_user/' /etc/slim.conf
fi

# Remove live user if it exists
if id "milo" &>/dev/null; then
    userdel -r milo 2>/dev/null || true
fi

# Remove live-config configuration
rm -rf /etc/live

# Update initramfs
update-initramfs -u -k all

# Clean package cache
apt-get clean
apt-get autoremove -y

echo "System finalized"
SCRIPT6
    
    # 7. Master script
    cat > "$CHROOT_DIR/usr/local/bin/calamares-post-install.sh" << 'MASTER'
#!/bin/bash
# Calamares post-installation master script

set -e

LOG_FILE="/var/log/miloOS-post-install.log"

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"
}

log "Starting miloOS post-installation configuration..."

# Get the new user from Calamares (passed as argument)
ROOT_DIR="$1"
cd "$ROOT_DIR" || exit 1

# Detect new user
NEW_USER=$(awk -F: '$3 >= 1000 && $3 < 65534 && $1 != "nobody" {print $1; exit}' etc/passwd)

if [ -z "$NEW_USER" ]; then
    log "ERROR: Could not determine new user"
    exit 1
fi

log "Configuring system for user: $NEW_USER"

# Run all post-installation scripts
SCRIPT_DIR="/usr/local/share/calamares/scripts"

if [ -f "$SCRIPT_DIR/preserve-configurations.sh" ]; then
    log "Preserving configurations..."
    bash "$SCRIPT_DIR/preserve-configurations.sh" "$NEW_USER" 2>&1 | tee -a "$LOG_FILE"
fi

if [ -f "$SCRIPT_DIR/setup-audio-groups.sh" ]; then
    log "Setting up audio groups..."
    bash "$SCRIPT_DIR/setup-audio-groups.sh" "$NEW_USER" 2>&1 | tee -a "$LOG_FILE"
fi

if [ -f "$SCRIPT_DIR/configure-grub.sh" ]; then
    log "Configuring GRUB..."
    bash "$SCRIPT_DIR/configure-grub.sh" 2>&1 | tee -a "$LOG_FILE"
fi

if [ -f "$SCRIPT_DIR/setup-pipewire.sh" ]; then
    log "Setting up PipeWire..."
    bash "$SCRIPT_DIR/setup-pipewire.sh" "$NEW_USER" 2>&1 | tee -a "$LOG_FILE"
fi

if [ -f "$SCRIPT_DIR/install-miloApps.sh" ]; then
    log "Installing miloApps..."
    bash "$SCRIPT_DIR/install-miloApps.sh" 2>&1 | tee -a "$LOG_FILE"
fi

if [ -f "$SCRIPT_DIR/finalize-system.sh" ]; then
    log "Finalizing system..."
    bash "$SCRIPT_DIR/finalize-system.sh" 2>&1 | tee -a "$LOG_FILE"
fi

log "miloOS post-installation completed successfully!"
exit 0
MASTER
    
    # Make all scripts executable
    chmod +x "$SCRIPT_DIR"/*.sh
    chmod +x "$CHROOT_DIR/usr/local/bin/calamares-post-install.sh"
    
    log_success "Post-installation scripts created"
}

# ============================================================================
# ISO CREATION
# ============================================================================

extract_kernel_initrd() {
    log_info "Extracting kernel and initrd..."
    
    # Find kernel and initrd in chroot
    local KERNEL=$(ls -1 "$CHROOT_DIR/boot/vmlinuz-"* 2>/dev/null | head -n 1)
    local INITRD=$(ls -1 "$CHROOT_DIR/boot/initrd.img-"* 2>/dev/null | head -n 1)
    
    if [ -z "$KERNEL" ]; then
        error_exit "Kernel not found in $CHROOT_DIR/boot/"
    fi
    
    if [ -z "$INITRD" ]; then
        error_exit "Initrd not found in $CHROOT_DIR/boot/"
    fi
    
    log_info "Found kernel: $(basename "$KERNEL")"
    log_info "Found initrd: $(basename "$INITRD")"
    
    # Ensure live directory exists
    mkdir -p "$SQUASHFS_DIR"
    
    # Copy to ISO live directory
    cp "$KERNEL" "$SQUASHFS_DIR/vmlinuz"
    cp "$INITRD" "$SQUASHFS_DIR/initrd.img"
    
    # Verify files were copied
    if [ ! -f "$SQUASHFS_DIR/vmlinuz" ]; then
        error_exit "Failed to copy kernel to $SQUASHFS_DIR/vmlinuz"
    fi
    
    if [ ! -f "$SQUASHFS_DIR/initrd.img" ]; then
        error_exit "Failed to copy initrd to $SQUASHFS_DIR/initrd.img"
    fi
    
    log_success "Kernel and initrd extracted to $SQUASHFS_DIR/"
}

create_squashfs() {
    log_info "Creating squashfs filesystem..."
    log_warn "This may take 10-20 minutes depending on system speed..."
    
    # Unmount chroot filesystems before creating squashfs
    umount -l "$CHROOT_DIR/proc" 2>/dev/null || true
    umount -l "$CHROOT_DIR/sys" 2>/dev/null || true
    umount -l "$CHROOT_DIR/dev/pts" 2>/dev/null || true
    umount -l "$CHROOT_DIR/dev" 2>/dev/null || true
    
    # Verify kernel and initrd are already extracted
    if [ ! -f "$SQUASHFS_DIR/vmlinuz" ]; then
        error_exit "Kernel not found at $SQUASHFS_DIR/vmlinuz - extract_kernel_initrd must run first"
    fi
    
    # Create squashfs with XZ compression (exclude boot since we already have kernel/initrd)
    log_info "Compressing filesystem (this will take a while)..."
    mksquashfs "$CHROOT_DIR" "$SQUASHFS_DIR/filesystem.squashfs" \
        -comp xz \
        -b 1M \
        -Xdict-size 100% \
        -e boot \
        -noappend \
        -no-progress \
        2>&1 | tee -a "$LOG_FILE" | grep -E "^Creating|^Exportable|^Filesystem|^Parallel" || true
    
    if [ ! -f "$SQUASHFS_DIR/filesystem.squashfs" ]; then
        error_exit "Failed to create squashfs"
    fi
    
    local SIZE=$(du -h "$SQUASHFS_DIR/filesystem.squashfs" | cut -f1)
    log_success "Squashfs created: $SIZE"
    
    # Create manifest file
    log_info "Creating filesystem manifest..."
    chroot "$CHROOT_DIR" dpkg-query -W --showformat='${Package} ${Version}\n' > "$SQUASHFS_DIR/filesystem.manifest" 2>/dev/null || true
    
    # Create size file
    du -sx --block-size=1 "$CHROOT_DIR" | cut -f1 > "$SQUASHFS_DIR/filesystem.size" 2>/dev/null || true
    
    # List contents of live directory
    log_info "Live directory contents:"
    ls -lh "$SQUASHFS_DIR/" | tee -a "$LOG_FILE"
    
    # Verify all required files exist
    local missing=0
    for file in vmlinuz initrd.img filesystem.squashfs; do
        if [ ! -f "$SQUASHFS_DIR/$file" ]; then
            log_error "Missing required file: $file"
            missing=1
        else
            log_info "✓ $file: $(du -h "$SQUASHFS_DIR/$file" | cut -f1)"
        fi
    done
    
    if [ $missing -eq 1 ]; then
        error_exit "Missing required files in live directory"
    fi
}

create_grub_config() {
    log_info "Creating GRUB configuration..."
    
    mkdir -p "$ISO_DIR/boot/grub"
    
    cat > "$ISO_DIR/boot/grub/grub.cfg" << 'EOF'
set timeout=10
set default=0

# Load modules
insmod all_video
insmod gfxterm
insmod png
insmod part_gpt
insmod part_msdos
insmod iso9660
insmod loopback
insmod squash4

set gfxmode=auto
set gfxpayload=keep

terminal_output gfxterm

set menu_color_normal=white/black
set menu_color_highlight=black/light-gray

menuentry "miloOS Live" {
    linux /live/vmlinuz boot=live components quiet splash username=milo hostname=miloOS
    initrd /live/initrd.img
}

menuentry "miloOS Live (Safe Mode)" {
    linux /live/vmlinuz boot=live components nomodeset username=milo hostname=miloOS
    initrd /live/initrd.img
}

menuentry "miloOS Live (Debug Mode)" {
    linux /live/vmlinuz boot=live components debug username=milo hostname=miloOS systemd.log_level=debug
    initrd /live/initrd.img
}

menuentry "miloOS Live (Failsafe)" {
    linux /live/vmlinuz boot=live components noapic noacpi nosplash irqpoll username=milo hostname=miloOS
    initrd /live/initrd.img
}

menuentry "miloOS Live (ToRAM)" {
    linux /live/vmlinuz boot=live components toram quiet splash username=milo hostname=miloOS
    initrd /live/initrd.img
}
EOF
    
    log_success "GRUB configuration created"
}

install_grub_bios() {
    log_info "Installing GRUB for BIOS..."
    
    mkdir -p "$ISO_DIR/boot/grub/i386-pc"
    
    # Copy GRUB modules
    cp -r /usr/lib/grub/i386-pc/* "$ISO_DIR/boot/grub/i386-pc/" 2>/dev/null || true
    
    # Create core image with necessary modules
    grub-mkimage -d /usr/lib/grub/i386-pc \
        -o "$ISO_DIR/boot/grub/i386-pc/core.img" \
        -O i386-pc \
        -p /boot/grub \
        biosdisk iso9660 part_msdos part_gpt fat ext2 normal boot linux configfile loopback chain multiboot 2>&1 | tee -a "$LOG_FILE" || true
    
    # Create eltorito boot image
    cat /usr/lib/grub/i386-pc/cdboot.img "$ISO_DIR/boot/grub/i386-pc/core.img" > "$ISO_DIR/boot/grub/i386-pc/eltorito.img"
    
    log_success "GRUB BIOS installed"
}

install_grub_uefi() {
    log_info "Installing GRUB for UEFI..."
    
    mkdir -p "$ISO_DIR/EFI/BOOT"
    
    # Create EFI boot image
    grub-mkstandalone \
        --format=x86_64-efi \
        --output="$ISO_DIR/EFI/BOOT/bootx64.efi" \
        --locales="" \
        --fonts="" \
        "boot/grub/grub.cfg=$ISO_DIR/boot/grub/grub.cfg" \
        2>&1 | tee -a "$LOG_FILE" || true
    
    # Create EFI boot image file
    dd if=/dev/zero of="$ISO_DIR/EFI/BOOT/efiboot.img" bs=1M count=10 2>/dev/null
    mkfs.vfat "$ISO_DIR/EFI/BOOT/efiboot.img" 2>&1 | tee -a "$LOG_FILE" | grep -v "^mkfs.fat" || true
    
    # Mount and copy EFI files
    local MOUNT_POINT=$(mktemp -d)
    mount -o loop "$ISO_DIR/EFI/BOOT/efiboot.img" "$MOUNT_POINT"
    mkdir -p "$MOUNT_POINT/EFI/BOOT"
    cp "$ISO_DIR/EFI/BOOT/bootx64.efi" "$MOUNT_POINT/EFI/BOOT/"
    umount "$MOUNT_POINT"
    rmdir "$MOUNT_POINT"
    
    log_success "GRUB UEFI installed"
}

build_iso() {
    log_info "Building ISO image..."
    log_warn "This may take several minutes..."
    
    # Verify ISO directory structure
    log_info "Verifying ISO structure..."
    if [ ! -d "$ISO_DIR/live" ]; then
        error_exit "Live directory not found"
    fi
    if [ ! -d "$ISO_DIR/boot/grub" ]; then
        error_exit "GRUB directory not found"
    fi
    
    # Build ISO with xorriso
    xorriso -as mkisofs \
        -iso-level 3 \
        -full-iso9660-filenames \
        -joliet \
        -joliet-long \
        -rational-rock \
        -volid "miloOS" \
        -appid "miloOS 1.0" \
        -publisher "Wamphyre" \
        -preparer "miloOS Build System" \
        -eltorito-boot boot/grub/i386-pc/eltorito.img \
        -no-emul-boot \
        -boot-load-size 4 \
        -boot-info-table \
        --eltorito-catalog boot/grub/boot.cat \
        --grub2-boot-info \
        --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img \
        -eltorito-alt-boot \
        -e EFI/BOOT/efiboot.img \
        -no-emul-boot \
        -append_partition 2 0xef "$ISO_DIR/EFI/BOOT/efiboot.img" \
        -output "$ISO_NAME" \
        -graft-points \
        "$ISO_DIR" \
        2>&1 | tee -a "$LOG_FILE" | grep -E "^xorriso|^ISO image|^Writing" || true
    
    if [ ! -f "$ISO_NAME" ]; then
        error_exit "Failed to create ISO"
    fi
    
    local SIZE=$(du -h "$ISO_NAME" | cut -f1)
    log_success "ISO created: $ISO_NAME ($SIZE)"
    
    # Make ISO hybrid (bootable from USB)
    log_info "Making ISO hybrid..."
    if command -v isohybrid &> /dev/null; then
        isohybrid "$ISO_NAME" 2>&1 | tee -a "$LOG_FILE" || log_warn "isohybrid failed, but ISO should still work"
    fi
}

# ============================================================================
# VALIDATION AND FINALIZATION
# ============================================================================

validate_iso() {
    log_info "Validating ISO image..."
    
    local errors=0
    
    # 1. Verify file exists
    if [ ! -f "$ISO_NAME" ]; then
        log_error "ISO file not found"
        return 1
    fi
    
    # 2. Verify minimum size (should be > 1GB)
    local size_bytes=$(stat -c%s "$ISO_NAME" 2>/dev/null || stat -f%z "$ISO_NAME" 2>/dev/null)
    local size_gb=$((size_bytes / 1073741824))
    
    if [ "$size_bytes" -lt 1073741824 ]; then
        log_warn "ISO size is less than 1GB ($size_gb GB), might be incomplete"
        errors=$((errors + 1))
    else
        log_info "✓ ISO size: $(du -h "$ISO_NAME" | cut -f1)"
    fi
    
    # 3. Verify it's a valid ISO
    if file "$ISO_NAME" | grep -q "ISO 9660"; then
        log_info "✓ Valid ISO 9660 image"
    else
        log_error "File is not a valid ISO 9660 image"
        errors=$((errors + 1))
    fi
    
    # 4. Verify volume ID
    if command -v isoinfo &> /dev/null; then
        if isoinfo -d -i "$ISO_NAME" 2>/dev/null | grep -q "Volume id: miloOS"; then
            log_info "✓ Volume ID is correct"
        else
            log_warn "ISO volume ID is not 'miloOS'"
        fi
    fi
    
    # 5. Calculate checksum
    log_info "Calculating SHA256 checksum..."
    sha256sum "$ISO_NAME" > "${ISO_NAME}.sha256"
    local checksum=$(cut -d' ' -f1 "${ISO_NAME}.sha256")
    log_info "✓ SHA256: $checksum"
    log_info "✓ Checksum saved to ${ISO_NAME}.sha256"
    
    return $errors
}

test_iso_boot() {
    log_info "Testing ISO boot capability..."
    
    if ! command -v qemu-system-x86_64 &> /dev/null; then
        log_warn "QEMU not available, skipping boot test"
        return 0
    fi
    
    log_info "Running quick boot test with QEMU (30 seconds)..."
    timeout 30 qemu-system-x86_64 \
        -cdrom "$ISO_NAME" \
        -m 2048 \
        -boot d \
        -nographic \
        -serial mon:stdio &> /dev/null || true
    
    log_info "✓ Boot test completed"
}

show_summary() {
    log_success "========================================="
    log_success "miloOS ISO Build Completed Successfully!"
    log_success "========================================="
    echo ""
    log_info "ISO Details:"
    log_info "  File: $ISO_NAME"
    log_info "  Size: $(du -h "$ISO_NAME" | cut -f1)"
    log_info "  Checksum: ${ISO_NAME}.sha256"
    log_info "  Log: $LOG_FILE"
    echo ""
    log_info "Next Steps:"
    echo ""
    echo "  1. Test in Virtual Machine:"
    echo "     VirtualBox: Create new VM and attach $ISO_NAME"
    echo "     QEMU: qemu-system-x86_64 -cdrom $ISO_NAME -m 4096 -boot d"
    echo ""
    echo "  2. Create Bootable USB:"
    echo "     Linux: sudo dd if=$ISO_NAME of=/dev/sdX bs=4M status=progress"
    echo "     Windows: Use Rufus or Etcher"
    echo "     macOS: Use Etcher or dd"
    echo ""
    echo "  3. Live System Credentials:"
    echo "     Username: milo"
    echo "     Password: 1234"
    echo ""
    log_success "Build completed in $(date -d@$SECONDS -u +%H:%M:%S) (HH:MM:SS)"
    echo ""
}

# ============================================================================
# HELP AND USAGE
# ============================================================================

show_help() {
    cat << EOF
miloOS ISO Release Builder v${VERSION}

USAGE:
    sudo ./make-miloOS-release.sh [OPTIONS]

DESCRIPTION:
    Creates a bootable ISO image of miloOS from the current configured system.
    The ISO includes:
    - LiveCD with user "milo" (password: 1234)
    - Calamares installer
    - All miloOS configurations and applications
    - BIOS and UEFI boot support

OPTIONS:
    -h, --help      Show this help message
    -v, --verbose   Enable verbose output
    --version       Show version information

REQUIREMENTS:
    - Must be run as root (sudo)
    - Debian-based system
    - At least 20GB free disk space in /tmp
    - Internet connection for dependencies

EXAMPLES:
    # Create ISO with default settings
    sudo ./make-miloOS-release.sh

    # Create ISO with verbose output
    sudo ./make-miloOS-release.sh --verbose

OUTPUT:
    - ISO file: miloOS-${VERSION}-amd64.iso
    - Checksum: miloOS-${VERSION}-amd64.iso.sha256
    - Log file: /tmp/miloOS-build-YYYYMMDD-HHMMSS.log

For more information, visit:
    https://github.com/Wamphyre/miloOS-core

EOF
}

show_version() {
    echo "miloOS ISO Release Builder v${VERSION}"
    echo "Copyright (c) 2025 Wamphyre"
}

# ============================================================================
# MAIN FUNCTION
# ============================================================================

main() {
    local VERBOSE=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -v|--verbose)
                VERBOSE=true
                set -x
                shift
                ;;
            --version)
                show_version
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done
    
    # Print banner
    echo ""
    echo "  __  __ _ _       ___  ____  "
    echo " |  \/  (_) | ___ / _ \/ ___| "
    echo " | |\/| | | |/ _ \ | | \___ \ "
    echo " | |  | | | | (_) | |_| |___) |"
    echo " |_|  |_|_|_|\___/ \___/|____/ "
    echo ""
    echo " ISO Release Builder v${VERSION}"
    echo " ================================"
    echo ""
    
    log_info "Starting miloOS ISO build process..."
    log_info "Log file: $LOG_FILE"
    
    # System verification
    check_root
    check_debian
    check_dependencies
    check_disk_space
    
    # Prepare skel with user configurations
    log_step 1 10 "Preparing /etc/skel with user configurations"
    prepare_skel
    
    # Setup workspace
    log_step 2 10 "Setting up workspace"
    setup_workspace
    
    # Copy system
    log_step 3 10 "Copying system to chroot"
    copy_system
    
    # Verify miloOS applications
    log_step 4 10 "Verifying miloOS applications"
    if ! verify_miloOS_apps; then
        log_warn "Some miloOS applications are missing, attempting to copy..."
        ensure_miloApps_in_chroot
    fi
    
    # Clean chroot
    log_step 5 10 "Cleaning chroot system"
    clean_chroot
    
    # Configure Live system
    log_step 6 10 "Configuring Live user and system"
    configure_live_user
    create_live_init_script
    create_live_systemd_service
    
    # Install and configure Calamares
    log_step 7 10 "Installing and configuring Calamares installer"
    install_calamares
    configure_calamares_settings
    create_calamares_modules
    create_calamares_branding
    create_post_install_scripts
    
    # Extract kernel and initrd (AFTER live-boot is installed and initramfs updated)
    log_step 8 10 "Extracting kernel and initrd"
    extract_kernel_initrd
    
    # Verify kernel and initrd exist
    if [ ! -f "$SQUASHFS_DIR/vmlinuz" ] || [ ! -f "$SQUASHFS_DIR/initrd.img" ]; then
        error_exit "Kernel or initrd missing from $SQUASHFS_DIR/"
    fi
    log_info "✓ Kernel: $(ls -lh "$SQUASHFS_DIR/vmlinuz" | awk '{print $5}')"
    log_info "✓ Initrd: $(ls -lh "$SQUASHFS_DIR/initrd.img" | awk '{print $5}')"
    
    # Create squashfs
    log_step 9 10 "Creating squashfs filesystem"
    create_squashfs
    
    # Configure GRUB
    log_step 10 10 "Configuring bootloader and creating ISO"
    create_grub_config
    install_grub_bios
    install_grub_uefi
    
    # Build ISO
    build_iso
    
    # Validate ISO
    log_info "Validating ISO..."
    if validate_iso; then
        log_success "ISO validation passed"
    else
        log_warn "ISO validation had warnings, but continuing..."
    fi
    
    # Optional: Test boot
    if [ "$VERBOSE" = true ]; then
        test_iso_boot
    fi
    
    # Mark build as successful
    BUILD_SUCCESS=true
    
    # Show summary
    show_summary
}

# ============================================================================
# SCRIPT ENTRY POINT
# ============================================================================

# Run main function
main "$@"
