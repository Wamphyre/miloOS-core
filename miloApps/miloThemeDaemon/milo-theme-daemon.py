#!/usr/bin/env python3
import os
import sys
import signal
import fcntl
import configparser
import subprocess
import gi
gi.require_version('Xfconf', '0')
gi.require_version('GLib', '2.0')
from gi.repository import GLib, Xfconf

_lock_file = None
_desktop_reload_source = 0

THEME_DEPENDENCIES = {
    "miloOS": {"icon_theme": "WhiteSur-light"},
    "miloOS-Dark": {"icon_theme": "WhiteSur-dark"},
}


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
                if proc_uid == uid and name in ["xfce4-session", "xfdesktop", "xfsettingsd"]:
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

def claim_single_instance():
    global _lock_file
    cache_dir = os.path.expanduser("~/.cache")
    os.makedirs(cache_dir, exist_ok=True)
    lock_path = os.path.join(cache_dir, "milo-theme-daemon.lock")
    _lock_file = open(lock_path, "w", encoding="utf-8")
    try:
        fcntl.flock(_lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print("miloOS Theme Daemon already running.", flush=True)
        sys.exit(0)
    _lock_file.write(f"{os.getpid()}\n")
    _lock_file.flush()

_channels = {}
def get_channel(channel_name):
    if channel_name not in _channels:
        _channels[channel_name] = Xfconf.Channel.get(channel_name)
    return _channels[channel_name]

def set_miloapp_system_theme(theme_name, app_name, config_dir, section, process_name):
    mode = "dark" if theme_name == "miloOS-Dark" else "light"
    config_path = os.path.expanduser(f"~/.config/{config_dir}/settings.ini")
    config_updated = False
    try:
        parser = configparser.ConfigParser(interpolation=None)
        if os.path.exists(config_path):
            parser.read(config_path, encoding="utf-8")
        if not parser.has_section(section):
            parser.add_section(section)
            config_updated = True
        if not parser.has_option(section, "theme"):
            parser.set(section, "theme", "auto")
            config_updated = True
        current = parser.get(section, "system_theme", fallback="")
        if current != mode:
            parser.set(section, "system_theme", mode)
            config_updated = True
        if config_updated:
            os.makedirs(os.path.dirname(config_path), exist_ok=True)
            with open(config_path, "w", encoding="utf-8") as f:
                parser.write(f)
            print(f"Successfully updated {app_name} system theme to {mode}", flush=True)
    except Exception as e:
        print(f"Error setting {app_name} system theme to {mode}: {e}", file=sys.stderr, flush=True)

    if not config_updated:
        return False

    try:
        res = subprocess.run(
            ["pgrep", "-u", str(os.getuid()), "-x", process_name],
            capture_output=True,
            text=True,
        )
        for raw_pid in res.stdout.split():
            try:
                os.kill(int(raw_pid), signal.SIGUSR1)
            except Exception:
                pass
    except Exception as e:
        print(f"Error notifying {app_name} about theme change: {e}", file=sys.stderr, flush=True)

    return True

def set_milodock_system_theme(theme_name):
    return set_miloapp_system_theme(theme_name, "miloDock", "miloDock", "Dock", "milodock")

def set_milopanel_system_theme(theme_name):
    return set_miloapp_system_theme(theme_name, "miloPanel", "miloPanel", "Panel", "milopanel")

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

def reload_desktop():
    global _desktop_reload_source
    _desktop_reload_source = 0
    load_user_env()
    try:
        res = subprocess.run(
            ["xfdesktop", "--reload"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if res.returncode != 0:
            subprocess.Popen(
                ["xfdesktop"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        print("Desktop theme reload completed.", flush=True)
    except FileNotFoundError:
        print("xfdesktop is not installed or not in PATH.", file=sys.stderr, flush=True)
    except Exception as e:
        print(f"Error reloading desktop: {e}", file=sys.stderr, flush=True)
    return False

def schedule_desktop_reload():
    global _desktop_reload_source
    if _desktop_reload_source:
        return
    _desktop_reload_source = GLib.timeout_add(250, reload_desktop)

def apply_theme_dependencies(theme_name):
    dependencies = THEME_DEPENDENCIES.get(theme_name)
    if not dependencies:
        return # Skip external/unhandled themes
        
    print(f"Applying theme dependencies for: {theme_name}", flush=True)
    
    updated = False
    miloapps_updated = False
    
    if set_xfconf_property("xsettings", "/Net/ThemeName", theme_name):
        updated = True
    
    if set_xfconf_property("xfwm4", "/general/theme", theme_name):
        updated = True
    
    if set_xfconf_property("xsettings", "/Net/IconThemeName", dependencies["icon_theme"]):
        updated = True

    if set_milodock_system_theme(theme_name):
        miloapps_updated = True
    if set_milopanel_system_theme(theme_name):
        miloapps_updated = True

    if updated:
        print("Theme dependencies synchronized successfully.", flush=True)
        schedule_desktop_reload()
    elif miloapps_updated:
        print("Native miloApps theme synchronized successfully.", flush=True)

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
    claim_single_instance()
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
