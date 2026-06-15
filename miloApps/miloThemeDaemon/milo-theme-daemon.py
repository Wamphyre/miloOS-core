#!/usr/bin/env python3
import os
import re
import sys
import subprocess
import threading
import gi
gi.require_version('Xfconf', '0')
gi.require_version('GLib', '2.0')
from gi.repository import GLib, Xfconf


def load_user_env():
    if os.environ.get("DISPLAY") and os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
        return
    try:
        import glob
        user_pids = []
        uid = os.getuid()
        for proc_path in glob.glob("/proc/[0-9]*/status"):
            try:
                with open(proc_path, "r") as f:
                    content = f.read()
                lines = content.split("\n")
                name = ""
                proc_uid = -1
                for line in lines:
                    if line.startswith("Name:"):
                        name = line.split()[1]
                    elif line.startswith("Uid:"):
                        proc_uid = int(line.split()[1])
                if proc_uid == uid and name in ["xfce4-session", "xfdesktop", "xfce4-panel", "xfsettingsd"]:
                    pid = proc_path.split("/")[2]
                    user_pids.append(pid)
            except Exception:
                continue
        for pid in user_pids:
            try:
                with open(f"/proc/{pid}/environ", "rb") as f:
                    env_data = f.read()
                env_dict = {}
                for item in env_data.split(b"\x00"):
                    if b"=" in item:
                        parts = item.split(b"=", 1)
                        key = parts[0].decode("utf-8", errors="ignore")
                        val = parts[1].decode("utf-8", errors="ignore")
                        env_dict[key] = val
                for key in ["DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "XDG_RUNTIME_DIR"]:
                    if key in env_dict and env_dict[key]:
                        os.environ[key] = env_dict[key]
                if os.environ.get("DISPLAY") and os.environ.get("DBUS_SESSION_BUS_ADDRESS"):
                    print(f"Successfully loaded session environment from PID {pid}", flush=True)
                    break
            except Exception:
                continue
    except Exception as e:
        print(f"Error loading user session environment: {e}", file=sys.stderr, flush=True)

_channels = {}
def get_channel(channel_name):
    if channel_name not in _channels:
        _channels[channel_name] = Xfconf.Channel.get(channel_name)
    return _channels[channel_name]

def get_menu_icon_properties():
    try:
        res = subprocess.run(["xfconf-query", "-c", "xfce4-panel", "-l"], capture_output=True, text=True, check=True)
        props = res.stdout.strip().split("\n")
        
        target_properties = []
        channel = get_channel("xfce4-panel")
        for prop in props:
            match = re.match(r"^/plugins/plugin-(\d+)$", prop)
            if match:
                plugin_id = match.group(1)
                if channel.has_property(prop):
                    plugin_type = channel.get_string(prop)
                    if plugin_type in ["applicationsmenu", "whiskermenu"]:
                        target_properties.append(f"/plugins/plugin-{plugin_id}/button-icon")
        return target_properties
    except Exception as e:
        print(f"Error finding applicationsmenu plugin: {e}", file=sys.stderr)
        return ["/plugins/plugin-1/button-icon"] # Fallback

def get_xfconf_property(channel_name, property_name):
    try:
        channel = get_channel(channel_name)
        if channel.has_property(property_name):
            return channel.get_string(property_name)
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
        channel = get_channel(channel_name)
        if channel.has_property(property_name):
            current = channel.get_string(property_name)
            if current == value:
                return False
        channel.set_string(property_name, value)
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
        print("Theme dependencies synchronized successfully.", flush=True)
        # Full desktop restart in background thread to avoid DBus deadlock
        def restart_desktop():
            import time
            time.sleep(0.1)
            load_user_env()
            try:
                # 1. Restart panel cleanly
                subprocess.run(["pkill", "-u", str(os.getuid()), "-x", "xfce4-panel"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                time.sleep(0.5)
                subprocess.Popen(["xfce4-panel"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                
                # 2. Reload or start xfdesktop
                xfdesktop_running = False
                try:
                    res = subprocess.run(["pgrep", "-u", str(os.getuid()), "-x", "xfdesktop"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    xfdesktop_running = (res.returncode == 0)
                except Exception:
                    pass
                
                if xfdesktop_running:
                    subprocess.run(["xfdesktop", "--reload"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                else:
                    subprocess.Popen(["xfdesktop"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                
                # 3. Kill xfce4-notifyd (will auto-spawn on demand)
                subprocess.run(["pkill", "-u", str(os.getuid()), "-x", "xfce4-notifyd"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                print("Graceful desktop restart/reload completed.", flush=True)
            except Exception as e:
                print(f"Error restarting/reloading desktop: {e}", file=sys.stderr, flush=True)
        t = threading.Thread(target=restart_desktop)
        t.daemon = True
        t.start()

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
    load_user_env()
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
