#!/bin/bash
# Author: Wamphyre
# Description: Customized skinpack for XFCE4 to look like macOS
# Version: 2.0 (Fixed and improved)

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
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
if [ "$(id -u)" -eq 0 ]; then
    log_error "Don't run this script with root user"
    exit 1
fi

# Verify required scripts exist
if [ ! -f "resources/install_resources.sh" ]; then
    log_error "resources/install_resources.sh not found!"
    exit 1
fi

if [ ! -f "configurations/apply.sh" ]; then
    log_error "configurations/apply.sh not found!"
    exit 1
fi

# Script Settings (Global vars)
# SCRIPT_DIR: directory of this script (repo root)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "$HOME" ] && [ -n "$USER" ]; then
    EXEC_USER="$USER"
    USER_HOME="$HOME"
elif [ -n "$USER" ]; then
    EXEC_USER="$USER"
    USER_HOME=$(getent passwd "$USER" | cut -d: -f6)
else
    log_error "The user home directory was not found!"
    log_error "Please check USER and HOME environment vars"
    exit 1
fi

log_info "User: $EXEC_USER"
log_info "Home: $USER_HOME"

install_and_configure() {
    log_info "Starting miloOS installation..."
    
    log_info "The necessary resources will be installed."
    log_info "Please enter your sudo password!"
    
    if sudo bash "$SCRIPT_DIR/resources/install_resources.sh"; then
        log_info "Resources installed successfully"
    else
        log_error "Failed to install resources"
        exit 1
    fi
    
    log_info "Applying configurations..."
    if bash "$SCRIPT_DIR/configurations/apply.sh" "$USER_HOME" "$EXEC_USER"; then
        log_info "Configurations applied successfully"
    else
        log_error "Failed to apply configurations"
        exit 1
    fi
    
    log_info "Installation completed successfully!"
    log_info "Please log out and log back in for all changes to take effect."
}

show_help() {
    cat << EOF

miloOS Core Installation Script

Usage: $0 [command]

Available commands:
    1 | install     Install skinpack completely and configure it
    help            Show this help message

Examples:
    $0 install
    $0 1

EOF
}

# Subcommands and help 
case "$1" in
    1|install)
        install_and_configure
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        log_error "Invalid command: $1"
        show_help
        exit 1
        ;;
esac
