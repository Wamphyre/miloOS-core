#!/bin/bash
# miloOS Package Cleaner
# Removes unnecessary packages to keep the system clean and minimal

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

# Ensure execution as root
if [ "$(id -u)" -ne 0 ]; then
    log_error "This script must be run as root (use sudo)"
    exit 1
fi

log_info "miloOS Package Cleaner - Removing unnecessary packages..."
echo ""

# List of packages to remove
PACKAGES_TO_REMOVE=(
    # Package managers
    "synaptic"
    
    # Thunar plugins (keeping useful ones)
    "thunar-media-tags-plugin"
    
    # Notes applications
    "xfce4-notes"
    "xfce4-notes-plugin"
    "gnote"
    "tomboy"
    
    # CD/DVD burning
    "xfburn"
    
    # Sensors
    "xfce4-sensors-plugin"
    "lm-sensors"
    
    # Scanner
    "xsane"
    "xsane-common"
    
    # LibreOffice suite
    "libreoffice"
    "libreoffice-*"
    
    # Media players
    "xjadeo"
    "quodlibet"
    "exfalso"
    "parole"
    
    # Dictionary
    "gnome-dictionary"
    "xfce4-dict"
    
    # Terminal emulators (keeping xfce4-terminal)
    "uxterm"
    "xterm"
)

log_info "The following packages will be removed:"
echo ""
for pkg in "${PACKAGES_TO_REMOVE[@]}"; do
    echo "  - $pkg"
done
echo ""

read -p "Do you want to continue? (y/N): " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    log_warn "Operation cancelled by user"
    exit 0
fi

echo ""
log_info "Removing packages..."
echo ""

# Remove packages
REMOVED_COUNT=0
FAILED_COUNT=0

for pkg in "${PACKAGES_TO_REMOVE[@]}"; do
    # Check if package is installed
    if dpkg -l | grep -q "^ii.*$pkg"; then
        log_info "Removing $pkg..."
        if apt-get remove --purge -y "$pkg" 2>/dev/null; then
            REMOVED_COUNT=$((REMOVED_COUNT + 1))
        else
            log_warn "Failed to remove $pkg (may not be installed)"
            FAILED_COUNT=$((FAILED_COUNT + 1))
        fi
    else
        log_warn "$pkg is not installed, skipping"
    fi
done

echo ""
log_info "Cleaning up..."

# Remove orphaned packages
apt-get autoremove -y 2>/dev/null

# Clean package cache
apt-get autoclean -y 2>/dev/null
apt-get clean -y 2>/dev/null

echo ""
log_info "Package cleanup completed!"
log_info "Packages removed: $REMOVED_COUNT"
if [ $FAILED_COUNT -gt 0 ]; then
    log_warn "Packages failed: $FAILED_COUNT"
fi

echo ""
log_info "System is now cleaner and more minimal"
log_info "Disk space freed: $(du -sh /var/cache/apt/archives/ 2>/dev/null | awk '{print $1}')"

exit 0
