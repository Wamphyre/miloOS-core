#!/usr/bin/env python3
import os
import re
import sys
import subprocess
import gi
gi.require_version('Xfconf', '0')
gi.require_version('GLib', '2.0')
from gi.repository import GLib, Xfconf

def get_menu_icon_properties():
    try:
        res = subprocess.run(["xfconf-query", "-c", "xfce4-panel", "-l"], capture_output=True, text=True, check=True)
        props = res.stdout.strip().split("\n")
        
        target_properties = []
        for prop in props:
            match = re.match(r"^/plugins/plugin-(\d+)$", prop)
            if match:
                plugin_id = match.group(1)
                res_val = subprocess.run(["xfconf-query", "-c", "xfce4-panel", "-p", prop], capture_output=True, text=True)
                plugin_type = res_val.stdout.strip()
                if plugin_type in ["applicationsmenu", "whiskermenu"]:
                    target_properties.append(f"/plugins/plugin-{plugin_id}/button-icon")
        return target_properties
    except Exception as e:
        print(f"Error finding applicationsmenu plugin: {e}", file=sys.stderr)
        return ["/plugins/plugin-1/button-icon"] # Fallback

def get_xfconf_property(channel_name, property_name):
    try:
        res = subprocess.run(["xfconf-query", "-c", channel_name, "-p", property_name], capture_output=True, text=True)
        if res.returncode == 0:
            return res.stdout.strip()
    except Exception:
        pass
    return None

def set_plank_theme(theme_name):
    try:
        res = subprocess.run(["dconf", "read", "/net/launchpad/plank/docks/dock1/theme"], capture_output=True, text=True)
        current = res.stdout.strip().replace("'", "")
        if current != theme_name:
            subprocess.run(["dconf", "write", "/net/launchpad/plank/docks/dock1/theme", f"'{theme_name}'"], check=True)
            print(f"Successfully updated Plank theme to {theme_name}", flush=True)
            return True
    except Exception as e:
        print(f"Error setting Plank theme to {theme_name}: {e}", file=sys.stderr, flush=True)
    return False

def set_xfconf_property(channel_name, property_name, value):
    try:
        current = get_xfconf_property(channel_name, property_name)
        if current != value:
            subprocess.run(["xfconf-query", "-c", channel_name, "-p", property_name, "-s", value], check=True)
            print(f"Successfully updated {channel_name}:{property_name} to {value}", flush=True)
            return True
    except Exception as e:
        print(f"Error setting {channel_name}:{property_name} to {value}: {e}", file=sys.stderr, flush=True)
    return False

def apply_theme_dependencies(theme_name):
    if theme_name not in ["miloOS", "miloOS-Dark"]:
        return # Skip external/unhandled themes
        
    print(f"Applying theme dependencies for: {theme_name}", flush=True)
    
    updated = False
    
    # 1. Synchronize GTK Theme Name
    if set_xfconf_property("xsettings", "/Net/ThemeName", theme_name):
        updated = True
    
    # 2. Synchronize Window Manager Theme Name
    if set_xfconf_property("xfwm4", "/general/theme", theme_name):
        updated = True
    
    if theme_name == "miloOS-Dark":
        # 3. Synchronize Icon Theme Name
        if set_xfconf_property("xsettings", "/Net/IconThemeName", "WhiteSur-dark"):
            updated = True
        # 4. Synchronize Panel Menu Icon
        icon_props = get_menu_icon_properties()
        for prop in icon_props:
            if set_xfconf_property("xfce4-panel", prop, "/usr/share/themes/miloOS-Dark/logo.png"):
                updated = True
        # 5. Synchronize Plank Theme
        if set_plank_theme("milo-dark"):
            updated = True
    elif theme_name == "miloOS":
        # 3. Synchronize Icon Theme Name
        if set_xfconf_property("xsettings", "/Net/IconThemeName", "WhiteSur-light"):
            updated = True
        # 4. Synchronize Panel Menu Icon
        icon_props = get_menu_icon_properties()
        for prop in icon_props:
            if set_xfconf_property("xfce4-panel", prop, "/usr/share/themes/miloOS/logo.png"):
                updated = True
        # 5. Synchronize Plank Theme
        if set_plank_theme("milo"):
            updated = True

    if updated:
        # Restart panel to force notification-plugin (and any cached panel icons) to reload
        try:
            subprocess.run(["xfce4-panel", "-r"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("Flashed/reloaded panel to refresh all icons.", flush=True)
        except Exception as e:
            print(f"Error reloading panel: {e}", file=sys.stderr, flush=True)

def on_xsettings_changed(channel, property_name, value, user_data):
    if property_name == "/Net/ThemeName":
        theme_name = value
        print(f"GTK theme changed event received: {theme_name}", flush=True)
        apply_theme_dependencies(theme_name)

def on_xfwm4_changed(channel, property_name, value, user_data):
    if property_name == "/general/theme":
        theme_name = value
        print(f"XFWM4 theme changed event received: {theme_name}", flush=True)
        apply_theme_dependencies(theme_name)

if __name__ == "__main__":
    Xfconf.init()
    
    xsettings_channel = Xfconf.Channel.get("xsettings")
    xfwm4_channel = Xfconf.Channel.get("xfwm4")
    
    xsettings_channel.connect("property-changed", on_xsettings_changed, None)
    xfwm4_channel.connect("property-changed", on_xfwm4_changed, None)
    
    # Perform startup synchronization based on current GTK theme
    current_theme = xsettings_channel.get_string("/Net/ThemeName")
    if current_theme:
        apply_theme_dependencies(current_theme)
        
    print("miloOS Theme Daemon started successfully. Monitoring theme changes...", flush=True)
    loop = GLib.MainLoop()
    try:
        loop.run()
    except KeyboardInterrupt:
        print("miloOS Theme Daemon stopping...", flush=True)
