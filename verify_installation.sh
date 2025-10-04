#!/bin/bash
# Author: Wamphyre
# Description: Verify miloOS installation
# Version: 1.0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS=0
FAIL=0
WARN=0

check_pass() {
    echo -e "${GREEN}✓${NC} $1"
    ((PASS++))
}

check_fail() {
    echo -e "${RED}✗${NC} $1"
    ((FAIL++))
}

check_warn() {
    echo -e "${YELLOW}⚠${NC} $1"
    ((WARN++))
}

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}miloOS Installation Verification${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Check system branding
echo -e "${BLUE}[1] System Branding${NC}"
if grep -q "miloOS" /etc/os-release 2>/dev/null; then
    check_pass "System branded as miloOS"
else
    check_fail "System not branded as miloOS"
fi

if [ -f "/root/.miloOS-last-backup" ]; then
    BACKUP_DIR=$(cat /root/.miloOS-last-backup)
    if [ -d "$BACKUP_DIR" ]; then
        check_pass "Backup found at: $BACKUP_DIR"
    else
        check_warn "Backup directory not found"
    fi
else
    check_warn "No backup reference found"
fi
echo ""

# Check themes
echo -e "${BLUE}[2] Themes and Icons${NC}"
if [ -d "/usr/share/themes/miloOS" ]; then
    check_pass "miloOS GTK theme installed"
else
    check_fail "miloOS GTK theme not found"
fi

if [ -d "/usr/local/share/icons/WhiteSur" ] || [ -d "/usr/local/share/icons/WhiteSur-dark" ]; then
    check_pass "WhiteSur icon theme installed"
else
    check_fail "WhiteSur icon theme not found"
fi
echo ""

# Check fonts
echo -e "${BLUE}[3] Fonts${NC}"
if [ -d "/usr/share/fonts/truetype/san-francisco" ]; then
    check_pass "San Francisco Pro fonts installed"
    
    # Check specific font files
    if fc-list | grep -q "SF Pro"; then
        check_pass "SF Pro fonts available in system"
    else
        check_warn "SF Pro fonts not detected by fontconfig"
    fi
else
    check_fail "San Francisco Pro fonts not found"
fi
echo ""

# Check wallpapers
echo -e "${BLUE}[4] Wallpapers${NC}"
if [ -f "/usr/share/backgrounds/blue-mountain.jpg" ] || [ -f "/usr/share/backgrounds/miloOS/blue-mountain.jpg" ]; then
    check_pass "Wallpapers installed"
else
    check_fail "Wallpapers not found"
fi
echo ""

# Check Plank
echo -e "${BLUE}[5] Plank Dock${NC}"
if command -v plank &> /dev/null; then
    check_pass "Plank installed"
else
    check_fail "Plank not installed"
fi

if [ -d "/usr/share/plank/themes/milo" ]; then
    check_pass "Plank milo theme installed"
else
    check_fail "Plank milo theme not found"
fi
echo ""

# Check packages
echo -e "${BLUE}[6] Essential Packages${NC}"
REQUIRED_PKGS=("dconf-cli" "xfce4-appmenu-plugin" "plank" "catfish" "vala-panel-appmenu")
for pkg in "${REQUIRED_PKGS[@]}"; do
    if dpkg -l | grep -q "^ii  $pkg"; then
        check_pass "$pkg installed"
    else
        check_fail "$pkg not installed"
    fi
done
echo ""

# Check PipeWire
echo -e "${BLUE}[7] PipeWire Audio System${NC}"
if command -v pipewire &> /dev/null; then
    check_pass "PipeWire installed"
    
    # Check PipeWire components
    PIPEWIRE_PKGS=("pipewire-pulse" "pipewire-alsa" "wireplumber")
    for pkg in "${PIPEWIRE_PKGS[@]}"; do
        if dpkg -l | grep -q "^ii  $pkg"; then
            check_pass "$pkg installed"
        else
            check_warn "$pkg not installed"
        fi
    done
    
    # Check PipeWire configuration
    if [ -f "/etc/pipewire/pipewire.conf.d/99-lowlatency.conf" ]; then
        check_pass "Low-latency configuration present"
    else
        check_warn "Low-latency configuration not found"
    fi
else
    check_fail "PipeWire not installed"
fi
echo ""

# Check user configuration (if not root)
if [ "$EUID" -ne 0 ] && [ -n "$HOME" ]; then
    echo -e "${BLUE}[8] User Configuration${NC}"
    
    if [ -d "$HOME/.config/xfce4/xfconf" ]; then
        check_pass "XFCE4 configuration found"
    else
        check_warn "XFCE4 configuration not found"
    fi
    
    if [ -d "$HOME/.config/plank/dock1/launchers" ]; then
        LAUNCHER_COUNT=$(ls -1 "$HOME/.config/plank/dock1/launchers"/*.dockitem 2>/dev/null | wc -l)
        if [ "$LAUNCHER_COUNT" -gt 0 ]; then
            check_pass "Plank launchers configured ($LAUNCHER_COUNT items)"
        else
            check_warn "No Plank launchers found"
        fi
    else
        check_warn "Plank configuration not found"
    fi
    
    if [ -f "$HOME/.config/autostart/Dock.desktop" ]; then
        check_pass "Plank autostart configured"
    else
        check_warn "Plank autostart not configured"
    fi
    
    # Check xfconf settings
    if command -v xfconf-query &> /dev/null && [ -n "$DISPLAY" ]; then
        THEME=$(xfconf-query -c xfwm4 -p /general/theme 2>/dev/null)
        if [ "$THEME" = "miloOS" ]; then
            check_pass "Window manager theme set to miloOS"
        else
            check_warn "Window manager theme not set (current: $THEME)"
        fi
        
        ICONS=$(xfconf-query -c xsettings -p /Net/IconThemeName 2>/dev/null)
        if [ "$ICONS" = "Cocoa" ]; then
            check_pass "Icon theme set to Cocoa"
        else
            check_warn "Icon theme not set (current: $ICONS)"
        fi
    else
        check_warn "Cannot check xfconf settings (no DISPLAY or xfconf-query not found)"
    fi
    echo ""
fi

# Check menus
echo -e "${BLUE}[9] Custom Menus${NC}"
if [ -f "/usr/bin/milo-session" ]; then
    check_pass "milo-session binary installed"
else
    check_warn "milo-session binary not found"
fi

MENU_COUNT=$(ls -1 /usr/share/applications/milo-*.desktop 2>/dev/null | wc -l)
if [ "$MENU_COUNT" -gt 0 ]; then
    check_pass "Custom menu items installed ($MENU_COUNT items)"
else
    check_warn "No custom menu items found"
fi
echo ""

# Check real-time audio optimizations
echo -e "${BLUE}[10] Real-Time Audio Optimizations${NC}"
if [ -f "/etc/security/limits.d/audio.conf" ]; then
    check_pass "Audio limits configured"
else
    check_warn "Audio limits not configured"
fi

if [ -f "/usr/local/bin/miloOS-audio-optimize.sh" ]; then
    check_pass "Audio optimization script installed"
else
    check_warn "Audio optimization script not found"
fi

if systemctl is-enabled miloOS-audio-optimization.service &>/dev/null; then
    check_pass "Audio optimization service enabled"
else
    check_warn "Audio optimization service not enabled"
fi

if [ -f "/etc/default/grub" ]; then
    if grep -q "preempt=full" /etc/default/grub; then
        check_pass "Real-time kernel parameters configured"
    else
        check_warn "Real-time kernel parameters not found"
    fi
fi
echo ""

# Summary
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Summary${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}Passed:${NC} $PASS"
echo -e "${YELLOW}Warnings:${NC} $WARN"
echo -e "${RED}Failed:${NC} $FAIL"
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}✓ Installation appears to be successful!${NC}"
    if [ $WARN -gt 0 ]; then
        echo -e "${YELLOW}⚠ Some optional components have warnings${NC}"
    fi
    exit 0
else
    echo -e "${RED}✗ Installation has issues that need attention${NC}"
    exit 1
fi
