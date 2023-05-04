#!/bin/bash
# Author: Juan Lozano <libredeb@gmail.com>
# Description: Customized skinpack for XFCE4 to look like macOS

# Ensure execution as root
if [ "$EUID" -gt 0 ]
then
    echo "ERROR: This script must be run as root"
    exit
fi

CURRENT_DIR=$PWD

install_debian_packages() {
    echo -e "Installing required packages..."
    apt-get update
    apt-get install firmware-linux gmtp cifs-utils smbclient winbind gtk2-engines-murrine gtk2-engines-pixbuf gnome-icon-theme plank catfish appmenu-gtk2-module appmenu-gtk3-module vala-panel-appmenu xfce4-appmenu-plugin xfce4-statusnotifier-plugin xfce4-notifyd meson ninja-build libgee-0.8-dev libgnome-menu-3-dev cdbs valac git libglib2.0-dev libwnck-3-dev libgtk-3-dev xterm python3 python3-wheel python3-setuptools gnome-menus gnome-maps shotwell gnome-calendar gedit zenity
}


install_gtk_themes() {
    echo -e "Installing Gtk+ themes..."
    cp -R resources/theme/miloOS /usr/share/themes/
    chown root:root -R /usr/share/themes/miloOS/
    
    cp -R resources/milk /usr/share/slim/themes/
    chown root:root -R /usr/share/slim/themes/
}

install_icon_themes() {
    echo -e "Installing cursor and icon themes..."
    cp -R resources/icons/Cocoa /usr/share/icons/
    chown root:root -R /usr/share/icons/Cocoa/
    gtk-update-icon-cache -f /usr/share/icons/Cocoa/
    cp resources/icons/catfish-symbolic.png /usr/share/pixmaps/
}

install_fonts() {
    echo -e "Installing fonts..."
    cp -R resources/fonts/Inter-Desktop /usr/share/fonts/
    chown root:root -R /usr/share/fonts/Inter-Desktop/
    fc-cache -f
}

install_wallpaper() {
    cp resources/backgrounds/* /usr/share/backgrounds/
    chmod 644 /usr/share/backgrounds/*
    chown root:root /usr/share/backgrounds/*
}

install_plank_theme() {
    cp -R resources/plank/milo /usr/share/plank/themes/
    chmod 755 /usr/share/plank/themes/milo
    chmod 644 /usr/share/plank/themes/milo/*.theme
    chown root:root -R /usr/share/plank/themes/milo
}

install_menus() {
    # Menu binary
    cp resources/menus/bin/milo-session /usr/bin/
    chmod 755 /usr/bin/milo-session
    chown root:root /usr/bin/milo-session
    # Menu Items
    cp resources/menus/items/milo-logout.desktop /usr/share/applications/
    chmod 644 /usr/share/applications/milo-logout.desktop
    chown root:root /usr/share/applications/milo-logout.desktop
    cp resources/menus/items/milo-shutdown.desktop /usr/share/applications/
    chmod 644 /usr/share/applications/milo-shutdown.desktop
    chown root:root /usr/share/applications/milo-shutdown.desktop
    cp resources/menus/items/milo-restart.desktop /usr/share/applications/
    chmod 644 /usr/share/applications/milo-restart.desktop
    chown root:root /usr/share/applications/milo-restart.desktop
    cp resources/menus/items/milo-sleep.desktop /usr/share/applications/
    chmod 644 /usr/share/applications/milo-sleep.desktop
    chown root:root /usr/share/applications/milo-sleep.desktop
    cp resources/menus/items/milo-settings.desktop /usr/share/applications/
    chmod 644 /usr/share/applications/milo-settings.desktop
    chown root:root /usr/share/applications/milo-settings.desktop
    cp resources/menus/items/milo-about.desktop /usr/share/applications/
    chmod 644 /usr/share/applications/milo-about.desktop
    chown root:root /usr/share/applications/milo-about.desktop
    # Menu XDG for XFCE4
    cp resources/menus/xdg/milo.menu /etc/xdg/menus/
    chmod 644 /etc/xdg/menus/milo.menu
    chown root:root /etc/xdg/menus/milo.menu
}

install_debian_packages
install_and_compile_lightpad
install_gtk_themes
install_icon_themes
install_fonts
install_wallpaper
install_plank_theme
install_menus
