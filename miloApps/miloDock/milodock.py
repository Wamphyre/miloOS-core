#!/usr/bin/env python3
from __future__ import annotations
import argparse
import atexit
import configparser
import locale
import math
import os
import re
import shlex
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from string import Template

import gi

gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
gi.require_version("Gio", "2.0")
from gi.repository import Gdk, Gio, GLib, Gtk

try:
    gi.require_version("Wnck", "3.0")
    from gi.repository import Wnck
except (ImportError, ValueError):
    Wnck = None


APP_ID = "milodock"
APP_NAME = "miloDock"
SCRIPT_PATH = Path(__file__).resolve()
DEFAULT_DESKTOP_IDS = [
    "milofiles.desktop",
    "xfce4-terminal.desktop",
    "mousepad.desktop",
    "org.xfce.mousepad.desktop",
    "firefox-esr.desktop",
]
DEFAULT_ORDER = {desktop_id: index for index, desktop_id in enumerate(DEFAULT_DESKTOP_IDS)}
EFFECTS = ["magnify", "lift", "none"]
THEME_MODES = ["auto", "light", "dark"]
SIZE_PRESETS = {
    "small": 34,
    "medium": 38,
    "large": 44,
}
SPACING_PRESETS = {
    "compact": 1,
    "normal": 3,
    "wide": 7,
}
MAGNIFY_DELTA = 8
AUTOHIDE_DELAY_MS = 700
AUTOHIDE_TICK_MS = 180
REVEAL_EDGE_SIZE = 2
TARGET_INTERNAL = 1
TARGET_URI_LIST = 2
TARGET_TEXT = 3
DRAG_TARGETS = [
    Gtk.TargetEntry.new("application/x-milodock-item", Gtk.TargetFlags.SAME_APP, TARGET_INTERNAL),
    Gtk.TargetEntry.new("text/uri-list", 0, TARGET_URI_LIST),
    Gtk.TargetEntry.new("text/plain", 0, TARGET_TEXT),
]

BASE_CSS = """
window.milodock-window {
    background-color: transparent;
}

window.milodock-window menu,
window.milodock-window popover {
    border-radius: 8px;
}

.milodock-shell {
    background-image: linear-gradient(to bottom, $shell_top, $shell_mid 52%, $shell_bottom);
    border: 1px solid $shell_border;
    border-bottom-color: $shell_border_bottom;
    border-radius: ${top_radius}px ${top_radius}px ${bottom_radius}px ${bottom_radius}px;
    box-shadow:
        inset 0 1px $inner_highlight,
        inset 0 -1px $inner_shadow,
        0 1px 4px alpha(#000000, $outer_shadow);
    padding: ${pad_top}px ${pad_x}px ${pad_bottom}px ${pad_x}px;
}

.milodock-item {
    background-color: transparent;
    border-radius: ${item_radius}px;
    margin: 0 ${item_margin}px;
    padding: ${item_pad_top}px ${item_pad_x}px 0 ${item_pad_x}px;
    transition: 110ms ease-out;
}

.milodock-item:hover {
    background: $hover_bg;
    box-shadow:
        inset 0 1px $hover_highlight,
        0 1px 4px alpha(#000000, $hover_shadow);
}

.milodock-item-running {
    background: $running_bg;
}

.milodock-drop-before {
    box-shadow: inset 2px 0 $drop_color;
}

.milodock-drop-after {
    box-shadow: inset -2px 0 $drop_color;
}

.milodock-empty {
    color: $empty_color;
    padding: 5px 12px;
}
"""

I18N = {
    "en": {
        "reload": "Reload Dock",
        "launch": "Open",
        "new_window": "Open New Window",
        "quit": "Close Windows",
        "dock": "Dock",
        "settings": "Settings",
        "preferences": "Preferences...",
        "icon_size": "Icon Size",
        "launcher_spacing": "Launcher Spacing",
        "small": "Small",
        "medium": "Medium",
        "large": "Large",
        "compact": "Compact",
        "normal": "Normal",
        "wide": "Wide",
        "auto_hide": "Auto Hide",
        "effect": "Effect",
        "magnify": "Magnify",
        "lift": "Lift",
        "none": "None",
        "theme": "Theme",
        "theme_auto": "Follow System",
        "theme_light": "Light",
        "theme_dark": "Dark",
        "apply": "Apply",
        "cancel": "Cancel",
        "no_launchers": "No launchers",
        "already_running": "miloDock is already running",
        "add_to_launcher": "Add to Launcher",
        "remove_from_launcher": "Remove from Launcher",
    },
    "es": {
        "reload": "Recargar Dock",
        "launch": "Abrir",
        "new_window": "Abrir nueva ventana",
        "quit": "Cerrar ventanas",
        "dock": "Dock",
        "settings": "Configuracion",
        "preferences": "Preferencias...",
        "icon_size": "Tamano de iconos",
        "launcher_spacing": "Separacion entre lanzadores",
        "small": "Pequeno",
        "medium": "Mediano",
        "large": "Grande",
        "compact": "Compacta",
        "normal": "Normal",
        "wide": "Amplia",
        "auto_hide": "Ocultar automaticamente",
        "effect": "Efecto",
        "magnify": "Aumentar",
        "lift": "Elevar",
        "none": "Ninguno",
        "theme": "Tema",
        "theme_auto": "Seguir sistema",
        "theme_light": "Claro",
        "theme_dark": "Oscuro",
        "apply": "Aplicar",
        "cancel": "Cancelar",
        "no_launchers": "Sin lanzadores",
        "already_running": "miloDock ya se esta ejecutando",
        "add_to_launcher": "Anadir al lanzador",
        "remove_from_launcher": "Quitar del lanzador",
    },
}


def _(key: str) -> str:
    lang = (locale.getlocale()[0] or os.environ.get("LANG") or "en").lower()
    table = I18N["es"] if lang.startswith("es") else I18N["en"]
    return table.get(key, key)


@dataclass
class DockSettings:
    icon_size: int = 38
    launcher_spacing: int = 3
    auto_hide: bool = False
    effect: str = "magnify"
    theme: str = "auto"
    system_theme: str = "light"

    @classmethod
    def load(cls) -> "DockSettings":
        settings = cls()
        parser = configparser.ConfigParser(interpolation=None)
        path = settings_path()
        read_paths = ["/etc/xdg/miloDock/settings.ini"]
        if path.exists():
            read_paths.append(str(path))
        parser.read(read_paths, encoding="utf-8")
        section = parser["Dock"] if parser.has_section("Dock") else {}

        try:
            settings.icon_size = int(section.get("icon_size", settings.icon_size))
        except (TypeError, ValueError):
            settings.icon_size = 38
        settings.icon_size = max(28, min(64, settings.icon_size))

        try:
            settings.launcher_spacing = int(section.get("launcher_spacing", settings.launcher_spacing))
        except (TypeError, ValueError):
            settings.launcher_spacing = 3
        settings.launcher_spacing = max(0, min(16, settings.launcher_spacing))

        settings.auto_hide = str(section.get("auto_hide", settings.auto_hide)).lower() in {
            "1",
            "yes",
            "true",
            "on",
        }
        effect = section.get("effect", settings.effect)
        settings.effect = effect if effect in EFFECTS else "magnify"
        theme = section.get("theme", settings.theme)
        settings.theme = theme if theme in THEME_MODES else "auto"
        system_theme = section.get("system_theme", settings.system_theme)
        settings.system_theme = "dark" if system_theme == "dark" else "light"
        return settings

    def save(self) -> None:
        path = settings_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        parser = configparser.ConfigParser(interpolation=None)
        parser["Dock"] = {
            "icon_size": str(self.icon_size),
            "launcher_spacing": str(self.launcher_spacing),
            "auto_hide": "true" if self.auto_hide else "false",
            "effect": self.effect,
            "theme": self.theme,
            "system_theme": self.system_theme,
        }
        with path.open("w", encoding="utf-8") as handle:
            parser.write(handle)

    def is_dark(self) -> bool:
        if self.theme == "dark":
            return True
        if self.theme == "light":
            return False
        gtk_theme = Gtk.Settings.get_default().get_property("gtk-theme-name") or ""
        return "dark" in gtk_theme.lower() or self.system_theme == "dark"


def settings_path() -> Path:
    return xdg_config_home() / "miloDock/settings.ini"


def user_launcher_dir() -> Path:
    return xdg_config_home() / "miloDock/launchers"


def build_css(settings: DockSettings) -> bytes:
    icon_size = settings.icon_size
    values = {
        "top_radius": 32,
        "bottom_radius": 0,
        "pad_top": 0,
        "pad_bottom": max(5, int(icon_size * 0.15)),
        "pad_x": max(9, int(icon_size * 0.30)),
        "item_radius": max(8, int(icon_size * 0.24)),
        "item_margin": 0,
        "item_pad_top": 2,
        "item_pad_x": 1,
    }

    if settings.is_dark():
        colors = {
            "shell_top": "rgba(35, 35, 35, 0.90)",
            "shell_mid": "rgba(28, 28, 28, 0.87)",
            "shell_bottom": "rgba(20, 20, 20, 0.82)",
            "shell_border": "rgba(0, 0, 0, 0.47)",
            "shell_border_bottom": "rgba(0, 0, 0, 0.50)",
            "inner_highlight": "rgba(255, 255, 255, 0.12)",
            "inner_shadow": "rgba(0, 0, 0, 0.38)",
            "outer_shadow": "0.28",
            "hover_bg": "rgba(255, 255, 255, 0.00)",
            "hover_highlight": "rgba(255, 255, 255, 0.00)",
            "hover_shadow": "0.00",
            "running_bg": "rgba(255, 255, 255, 0.00)",
            "drop_color": "rgba(93, 180, 255, 0.96)",
            "empty_color": "alpha(#f2f2f2, 0.72)",
        }
    else:
        colors = {
            "shell_top": "rgba(241, 241, 241, 0.86)",
            "shell_mid": "rgba(222, 222, 222, 0.84)",
            "shell_bottom": "rgba(141, 141, 141, 0.78)",
            "shell_border": "rgba(0, 0, 0, 0.39)",
            "shell_border_bottom": "rgba(0, 0, 0, 0.42)",
            "inner_highlight": "rgba(255, 255, 255, 0.50)",
            "inner_shadow": "rgba(0, 0, 0, 0.12)",
            "outer_shadow": "0.22",
            "hover_bg": "rgba(255, 255, 255, 0.00)",
            "hover_highlight": "rgba(255, 255, 255, 0.00)",
            "hover_shadow": "0.00",
            "running_bg": "rgba(255, 255, 255, 0.00)",
            "drop_color": "rgba(37, 121, 216, 0.92)",
            "empty_color": "alpha(#111111, 0.64)",
        }

    values.update(colors)
    return Template(BASE_CSS).substitute(values).encode("utf-8")


def normalize_token(value: str) -> str:
    value = value.strip().lower()
    if value.endswith(".desktop"):
        value = value[:-8]
    value = value.replace("_", "-")
    return value


def token_variants(value: str) -> set[str]:
    token = normalize_token(value)
    variants = {token}
    if "." in token:
        variants.add(token.split(".")[-1])
    if token.endswith("-esr"):
        variants.add(token[:-4])
    if token.startswith("org.xfce."):
        variants.add(token.split(".")[-1])
    return {v for v in variants if v}


def rects_intersect(a: tuple[int, int, int, int], b: tuple[int, int, int, int]) -> bool:
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by


def xdg_config_home() -> Path:
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))


def xdg_cache_home() -> Path:
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))


def xdg_data_dirs() -> list[Path]:
    dirs = [Path.home() / ".local/share"]
    for raw in os.environ.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(":"):
        if raw:
            dirs.append(Path(raw))
    return dirs


def launcher_dirs() -> list[Path]:
    config_home = xdg_config_home()
    dirs = [
        config_home / "miloDock/launchers",
        Path("/etc/xdg/miloDock/launchers"),
    ]

    for parent in [SCRIPT_PATH.parent, *SCRIPT_PATH.parents]:
        repo_launchers = parent / "configurations/miloDock/launchers"
        if repo_launchers.exists():
            dirs.append(repo_launchers)
            break

    return dirs


def app_dirs() -> list[Path]:
    return [base / "applications" for base in xdg_data_dirs()]


def read_desktop_key(path: Path, key: str) -> str:
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.optionxform = str
    try:
        parser.read(path, encoding="utf-8")
        return parser.get("Desktop Entry", key, fallback="").strip()
    except configparser.Error:
        return ""


def desktop_id_from_path(path: Path) -> str:
    return path.name


def path_from_file_uri(uri: str) -> Path | None:
    gfile = Gio.File.new_for_uri(uri)
    path = gfile.get_path()
    return Path(path) if path else None


def find_desktop_path(ref: str) -> Path | None:
    ref = ref.strip()
    if not ref:
        return None

    if ref.startswith("file://"):
        path = path_from_file_uri(ref)
        return path if path and path.exists() else None

    path = Path(ref).expanduser()
    if path.is_absolute() and path.exists():
        return path

    desktop_id = path.name if path.suffix == ".desktop" else ref
    candidates = [desktop_id]
    if desktop_id == "mousepad.desktop":
        candidates.append("org.xfce.mousepad.desktop")

    for candidate in candidates:
        for app_dir in app_dirs():
            direct = app_dir / candidate
            if direct.exists():
                return direct

    stem = desktop_id[:-8] if desktop_id.endswith(".desktop") else desktop_id
    for app_dir in app_dirs():
        if not app_dir.exists():
            continue
        for match in app_dir.glob(f"*{stem}*.desktop"):
            return match

    return None


def launcher_ref_from_dockitem(path: Path) -> str:
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.optionxform = str
    try:
        parser.read(path, encoding="utf-8")
        return parser.get("miloDockItem", "Launcher", fallback="").strip()
    except configparser.Error:
        return ""


def launcher_file_sort_key(path: Path) -> tuple[int, str]:
    name = path.name.lower()
    numbered = re.match(r"^(\d+)-", name)
    if numbered:
        return (int(numbered.group(1)), name)
    desktop_equivalent = f"{path.stem.lower()}.desktop" if path.suffix == ".dockitem" else name
    if name == "org.xfce.mousepad.desktop":
        return (DEFAULT_ORDER.get("mousepad.desktop", 1000), name)
    return (DEFAULT_ORDER.get(desktop_equivalent, 1000), name)


def command_basename(commandline: str) -> str:
    if not commandline:
        return ""
    try:
        parts = shlex.split(commandline)
    except ValueError:
        parts = commandline.split()
    for part in parts:
        if not part.startswith("%"):
            return Path(part).name
    return ""


@dataclass
class Launcher:
    desktop_id: str
    desktop_path: Path
    app_info: Gio.DesktopAppInfo
    name: str
    icon: Gio.Icon | None
    match_tokens: set[str]


def launcher_from_desktop_path(path: Path) -> Launcher | None:
    try:
        app_info = Gio.DesktopAppInfo.new_from_filename(str(path))
    except (TypeError, GLib.Error):
        return None
    if not app_info or app_info.get_nodisplay():
        return None

    desktop_id = desktop_id_from_path(path)
    name = app_info.get_display_name() or app_info.get_name() or desktop_id
    command = ""
    if hasattr(app_info, "get_commandline"):
        command = app_info.get_commandline() or ""

    tokens = set()
    for value in [
        desktop_id,
        path.stem,
        name,
        read_desktop_key(path, "StartupWMClass"),
        command_basename(command),
    ]:
        if value:
            tokens.update(token_variants(value))

    return Launcher(
        desktop_id=desktop_id,
        desktop_path=path,
        app_info=app_info,
        name=name,
        icon=app_info.get_icon(),
        match_tokens=tokens,
    )


def load_launchers() -> tuple[list[Launcher], list[Path]]:
    launchers: list[Launcher] = []
    seen: set[str] = set()
    used_dirs: list[Path] = []

    for directory in launcher_dirs():
        if not directory.exists():
            continue

        files = sorted(
            [
                p
                for p in directory.iterdir()
                if p.is_file() and p.suffix in {".dockitem", ".desktop"}
            ],
            key=launcher_file_sort_key,
        )
        if not files:
            continue

        used_dirs.append(directory)
        for item in files:
            ref = launcher_ref_from_dockitem(item) if item.suffix == ".dockitem" else str(item)
            desktop_path = find_desktop_path(ref)
            if not desktop_path:
                continue
            launcher = launcher_from_desktop_path(desktop_path)
            if not launcher or launcher.desktop_id in seen:
                continue
            seen.add(launcher.desktop_id)
            launchers.append(launcher)

        if launchers:
            break

    if not launchers:
        for desktop_id in DEFAULT_DESKTOP_IDS:
            desktop_path = find_desktop_path(desktop_id)
            if not desktop_path:
                continue
            launcher = launcher_from_desktop_path(desktop_path)
            if launcher and launcher.desktop_id not in seen:
                seen.add(launcher.desktop_id)
                launchers.append(launcher)

    return launchers, used_dirs


def safe_launcher_name(launcher: Launcher, index: int) -> str:
    stem = launcher.desktop_id[:-8] if launcher.desktop_id.endswith(".desktop") else launcher.desktop_id
    stem = re.sub(r"[^A-Za-z0-9_.-]+", "-", stem).strip("-") or "launcher"
    return f"{index:02d}-{stem}.dockitem"


def save_launcher_order(launchers: list[Launcher]) -> None:
    directory = user_launcher_dir()
    directory.mkdir(parents=True, exist_ok=True)
    for old in directory.iterdir():
        if old.is_file() and old.suffix in {".dockitem", ".desktop"}:
            old.unlink()

    for index, launcher in enumerate(launchers, start=1):
        path = directory / safe_launcher_name(launcher, index)
        desktop_uri = Gio.File.new_for_path(str(launcher.desktop_path)).get_uri()
        path.write_text(
            "[miloDockItem]\n"
            f"Launcher={desktop_uri}\n",
            encoding="utf-8",
        )


def launchers_from_uri_list(uri_list: str) -> list[Launcher]:
    launchers: list[Launcher] = []
    for uri in uri_list.strip().splitlines():
        if not uri:
            continue
        path = path_from_file_uri(uri)
        if not path:
            continue
        desktop_path = path if path.suffix == ".desktop" else find_desktop_path(path.name)
        if not desktop_path:
            continue
        launcher = launcher_from_desktop_path(desktop_path)
        if launcher:
            launchers.append(launcher)
    return launchers


def launcher_from_text(value: str) -> Launcher | None:
    value = value.strip()
    if not value:
        return None
    desktop_path = find_desktop_path(value)
    return launcher_from_desktop_path(desktop_path) if desktop_path else None


def normalize_process_cwd() -> None:
    home = Path.home()
    try:
        os.chdir(home)
        os.environ["PWD"] = str(home)
    except OSError:
        pass


class DockItem(Gtk.EventBox):
    def __init__(self, dock: "MiloDock", launcher: Launcher, pinned: bool = True):
        super().__init__()
        self.dock = dock
        self.launcher = launcher
        self.pinned = pinned
        self.running = False
        self.hovered = False
        self.primary_press: tuple[float, float] | None = None
        self.drag_suppressed_launch = False
        self.drag_started = False

        self.set_visible_window(True)
        self.set_above_child(True)
        self.set_events(
            Gdk.EventMask.BUTTON_PRESS_MASK
            | Gdk.EventMask.BUTTON_RELEASE_MASK
            | Gdk.EventMask.POINTER_MOTION_MASK
            | Gdk.EventMask.ENTER_NOTIFY_MASK
            | Gdk.EventMask.LEAVE_NOTIFY_MASK
        )
        self.set_tooltip_text(launcher.name)
        self.get_style_context().add_class("milodock-item")

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        box.set_halign(Gtk.Align.CENTER)
        box.set_valign(Gtk.Align.CENTER)
        self.add(box)

        self.image = Gtk.Image()
        if launcher.icon:
            self.image.set_from_gicon(launcher.icon, Gtk.IconSize.DIALOG)
        else:
            self.image.set_from_icon_name("application-x-executable", Gtk.IconSize.DIALOG)
        self.image.set_pixel_size(self.dock.settings.icon_size)
        box.pack_start(self.image, False, False, 0)

        self.dot = Gtk.DrawingArea()
        self.dot.set_size_request(7, 5)
        self.dot.connect("draw", self.on_dot_draw)
        box.pack_start(self.dot, False, False, 0)

        self.connect("button-press-event", self.on_button_press)
        self.connect("button-release-event", self.on_button_release)
        self.connect("motion-notify-event", self.on_motion)
        self.connect("enter-notify-event", self.on_enter)
        self.connect("leave-notify-event", self.on_leave)

        self.connect("drag-begin", self.on_drag_begin)
        self.connect("drag-end", self.on_drag_end)
        self.connect("drag-data-get", self.on_drag_data_get)
        self.drag_dest_set(Gtk.DestDefaults.ALL, DRAG_TARGETS, Gdk.DragAction.MOVE | Gdk.DragAction.COPY)
        self.connect("drag-motion", self.on_drag_motion)
        self.connect("drag-leave", self.on_drag_leave)
        self.connect("drag-data-received", self.on_drag_data_received)
        self.apply_settings()

    def apply_settings(self) -> None:
        base_size = self.dock.settings.icon_size
        reserved_size = base_size
        if self.hovered and self.dock.settings.effect == "magnify":
            reserved_size = min(64, base_size + MAGNIFY_DELTA)
        elif self.dock.settings.effect == "magnify":
            reserved_size = min(64, base_size + MAGNIFY_DELTA)

        size = base_size
        if self.hovered and self.dock.settings.effect == "magnify":
            size = reserved_size

        self.image.set_size_request(reserved_size, reserved_size)
        self.image.set_pixel_size(size)

    def on_enter(self, *_args) -> bool:
        self.hovered = True
        if self.dock.settings.effect == "lift":
            self.set_margin_top(0)
            self.set_margin_bottom(4)
        self.apply_settings()
        self.dock.show_from_autohide()
        return False

    def on_leave(self, *_args) -> bool:
        self.hovered = False
        self.set_margin_top(0)
        self.set_margin_bottom(0)
        self.apply_settings()
        return False

    def on_drag_begin(self, *_args) -> None:
        self.drag_suppressed_launch = True
        self.drag_started = True

    def on_drag_end(self, *_args) -> None:
        GLib.timeout_add(120, self.reset_drag_suppression)

    def reset_drag_suppression(self) -> bool:
        self.drag_suppressed_launch = False
        self.drag_started = False
        return False

    def on_drag_data_get(self, _widget, _context, data, _info, _time) -> None:
        data.set_text(self.launcher.desktop_id, -1)

    def drop_side(self, x: int) -> str:
        return "before" if x < max(1, self.get_allocated_width() // 2) else "after"

    def on_drag_motion(self, _widget, _context, x, _y, _time) -> bool:
        context = self.get_style_context()
        context.remove_class("milodock-drop-before")
        context.remove_class("milodock-drop-after")
        context.add_class("milodock-drop-before" if self.drop_side(x) == "before" else "milodock-drop-after")
        return True

    def on_drag_leave(self, *_args) -> None:
        context = self.get_style_context()
        context.remove_class("milodock-drop-before")
        context.remove_class("milodock-drop-after")

    def on_drag_data_received(self, widget, context, x, _y, data, info, time_) -> None:
        self.on_drag_leave()
        insert_index = self.dock.items.index(self)
        if self.drop_side(x) == "after":
            insert_index += 1
        handled = self.dock.handle_drop(data, info, insert_index)
        context.finish(handled, handled and info == TARGET_INTERNAL, time_)

    def on_dot_draw(self, widget, cr) -> bool:
        if not self.running:
            return False
        allocation = widget.get_allocation()
        radius = 2.1
        if self.dock.settings.is_dark():
            cr.set_source_rgba(0.88, 0.93, 1.0, 0.86)
        else:
            cr.set_source_rgba(0.12, 0.16, 0.20, 0.78)
        cr.arc(allocation.width / 2, allocation.height / 2, radius, 0, math.tau)
        cr.fill()
        return False

    def set_running(self, running: bool) -> None:
        if self.running == running:
            return
        self.running = running
        context = self.get_style_context()
        if running:
            context.add_class("milodock-item-running")
        else:
            context.remove_class("milodock-item-running")
        self.dot.queue_draw()

    def on_button_press(self, _widget: Gtk.Widget, event: Gdk.EventButton) -> bool:
        if event.button == 1:
            self.primary_press = (event.x, event.y)
            self.drag_suppressed_launch = False
            self.drag_started = False
            return True
        if event.button == 2:
            self.dock.launch(self.launcher)
            return True
        if event.button == 3:
            self.show_menu(event)
            return True
        return False

    def on_motion(self, _widget: Gtk.Widget, event: Gdk.EventMotion) -> bool:
        if not self.pinned or not self.primary_press:
            return False
        if not event.state & Gdk.ModifierType.BUTTON1_MASK:
            return False

        if self.drag_started:
            self.dock.update_reorder_preview(int(event.x_root))
            return True

        press_x, press_y = self.primary_press
        if not self.drag_check_threshold(int(press_x), int(press_y), int(event.x), int(event.y)):
            return False

        self.drag_suppressed_launch = True
        self.drag_started = True
        self.dock.begin_reorder(self, int(event.x_root))
        return True

    def on_button_release(self, _widget: Gtk.Widget, event: Gdk.EventButton) -> bool:
        if event.button != 1:
            return False

        press = self.primary_press
        self.primary_press = None
        if self.drag_started:
            self.dock.finish_reorder(self, int(event.x_root))
            self.drag_started = False
            self.drag_suppressed_launch = False
            return True
        if self.drag_suppressed_launch:
            return True

        if press and self.drag_check_threshold(
            int(press[0]),
            int(press[1]),
            int(event.x),
            int(event.y),
        ):
            return True

        self.dock.activate_or_launch(self.launcher, force_new=False)
        return True

    def show_menu(self, event: Gdk.EventButton) -> None:
        menu = Gtk.Menu()

        title = Gtk.MenuItem(label=self.launcher.name)
        title.set_sensitive(False)
        menu.append(title)

        open_item = Gtk.MenuItem(label=_("launch"))
        open_item.connect("activate", lambda _i: self.dock.activate_or_launch(self.launcher, False))
        menu.append(open_item)

        new_item = Gtk.MenuItem(label=_("new_window"))
        new_item.connect("activate", lambda _i: self.dock.launch(self.launcher))
        menu.append(new_item)

        windows = self.dock.running_windows_for(self.launcher)
        if windows:
            quit_item = Gtk.MenuItem(label=_("quit"))
            quit_item.connect("activate", lambda _i: self.dock.close_windows(windows))
            menu.append(quit_item)

        menu.append(Gtk.SeparatorMenuItem())

        add_item = Gtk.MenuItem(label=_("add_to_launcher"))
        add_item.set_sensitive(not self.dock.launcher_is_persisted(self.launcher))
        add_item.connect("activate", lambda _i: self.dock.add_launcher_to_user_config(self.launcher))
        menu.append(add_item)

        remove_item = Gtk.MenuItem(label=_("remove_from_launcher"))
        remove_item.set_sensitive(self.pinned and len(self.dock.current_launchers()) > 1)
        remove_item.connect("activate", lambda _i: self.dock.remove_launcher_from_user_config(self.launcher))
        menu.append(remove_item)

        menu.append(Gtk.SeparatorMenuItem())
        preferences_item = Gtk.MenuItem(label=_("preferences"))
        preferences_item.connect("activate", lambda _i: show_preferences_dialog(self.dock))
        menu.append(preferences_item)

        reload_item = Gtk.MenuItem(label=_("reload"))
        reload_item.connect("activate", lambda _i: self.dock.reload_launchers())
        menu.append(reload_item)

        menu.show_all()
        menu.popup_at_pointer(event)


class MiloDock(Gtk.Window):
    def __init__(self, app: Gtk.Application | None = None):
        super().__init__(type=Gtk.WindowType.TOPLEVEL)
        self.app = app
        if self.app:
            self.set_application(self.app)
        self.settings = DockSettings.load()
        self.items: list[DockItem] = []
        self.pinned_launchers: list[Launcher] = []
        self.desktop_launcher_cache: list[Launcher] | None = None
        self.reorder_source: DockItem | None = None
        self.reorder_insert_index = 0
        self.launcher_monitors: list[Gio.FileMonitor] = []
        self.settings_monitor: Gio.FileMonitor | None = None
        self.reload_source_id = 0
        self.hide_source_id = 0
        self.autohide_tick_source_id = 0
        self.hidden = False
        self.wnck_screen = None
        self.css_provider = Gtk.CssProvider()

        self.set_title(APP_NAME)
        self.set_name("milodock-window")
        self.set_decorated(False)
        self.set_resizable(False)
        self.set_skip_taskbar_hint(True)
        self.set_skip_pager_hint(True)
        self.set_keep_above(True)
        self.stick()
        self.set_type_hint(Gdk.WindowTypeHint.DOCK)
        self.set_app_paintable(True)

        screen = self.get_screen()
        visual = screen.get_rgba_visual()
        if visual:
            self.set_visual(visual)

        self.apply_css()
        Gtk.StyleContext.add_provider_for_screen(
            screen, self.css_provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
        self.get_style_context().add_class("milodock-window")

        self.shell = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=self.settings.launcher_spacing)
        self.shell.get_style_context().add_class("milodock-shell")
        self.add(self.shell)

        self.add_events(Gdk.EventMask.ENTER_NOTIFY_MASK | Gdk.EventMask.LEAVE_NOTIFY_MASK)
        self.connect("enter-notify-event", self.on_pointer_enter)
        self.connect("leave-notify-event", self.on_pointer_leave)
        self.shell.drag_dest_set(Gtk.DestDefaults.ALL, DRAG_TARGETS, Gdk.DragAction.MOVE | Gdk.DragAction.COPY)
        self.shell.connect("drag-data-received", self.on_shell_drag_data_received)

        gtk_settings = Gtk.Settings.get_default()
        gtk_settings.connect("notify::gtk-theme-name", lambda *_args: self.on_theme_changed())

        self.monitor_settings_file()
        self.connect("destroy", lambda *_args: self.app.quit() if self.app else Gtk.main_quit())
        self.connect("size-allocate", lambda *_args: GLib.idle_add(self.reposition))
        screen.connect("size-changed", lambda *_args: GLib.idle_add(self.reposition))

        self.setup_wnck()
        self.reload_launchers()
        GLib.timeout_add_seconds(2, self.update_running_state)
        self.autohide_tick_source_id = GLib.timeout_add(AUTOHIDE_TICK_MS, self.autohide_tick)

    def apply_css(self) -> None:
        self.css_provider.load_from_data(build_css(self.settings))

    def apply_spacing(self) -> None:
        if hasattr(self, "shell"):
            self.shell.set_spacing(self.settings.launcher_spacing)

    def on_theme_changed(self) -> None:
        self.settings = DockSettings.load()
        self.apply_css()
        self.apply_spacing()
        self.apply_settings_to_items()
        GLib.idle_add(self.reposition)

    def monitor_settings_file(self) -> None:
        path = settings_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            gfile = Gio.File.new_for_path(str(path))
            monitor = gfile.monitor_file(Gio.FileMonitorFlags.NONE, None)
            monitor.connect("changed", lambda *_args: GLib.timeout_add(120, self.reload_settings))
            self.settings_monitor = monitor
        except GLib.Error:
            pass

    def reload_settings(self) -> bool:
        self.settings = DockSettings.load()
        self.apply_css()
        self.apply_spacing()
        self.apply_settings_to_items()
        self.apply_autohide_state()
        if self.app:
            update_action_states(self.app, self.settings)
        GLib.idle_add(self.reposition)
        return False

    def apply_settings_to_items(self) -> None:
        for item in self.items:
            item.apply_settings()

    def set_icon_size(self, size: int) -> None:
        self.settings.icon_size = max(28, min(64, int(size)))
        self.settings.save()
        self.reload_settings()

    def set_launcher_spacing(self, spacing: int) -> None:
        self.settings.launcher_spacing = max(0, min(16, int(spacing)))
        self.settings.save()
        self.reload_settings()

    def set_effect(self, effect: str) -> None:
        if effect in EFFECTS:
            self.settings.effect = effect
            self.settings.save()
            self.reload_settings()

    def set_theme_mode(self, mode: str) -> None:
        if mode in THEME_MODES:
            self.settings.theme = mode
            self.settings.save()
            self.reload_settings()

    def set_auto_hide(self, enabled: bool) -> None:
        self.settings.auto_hide = enabled
        self.settings.save()
        self.apply_autohide_state()
        if self.app:
            update_action_states(self.app, self.settings)

    def apply_autohide_state(self) -> None:
        if self.settings.auto_hide and self.should_autohide_now():
            self.schedule_autohide()
        else:
            self.show_from_autohide()

    def schedule_autohide(self) -> bool:
        if not self.settings.auto_hide:
            return False
        if not self.should_autohide_now():
            self.show_from_autohide()
            return False
        if self.hide_source_id:
            return False
        self.hide_source_id = GLib.timeout_add(AUTOHIDE_DELAY_MS, self.hide_for_autohide)
        return False

    def hide_for_autohide(self) -> bool:
        self.hide_source_id = 0
        if not self.settings.auto_hide or not self.should_autohide_now() or self.pointer_over_dock():
            return False
        self.hidden = True
        self.hide()
        return False

    def show_from_autohide(self) -> bool:
        if self.hide_source_id:
            GLib.source_remove(self.hide_source_id)
            self.hide_source_id = 0
        if self.hidden:
            self.hidden = False
            self.reposition()
            self.show_all()
        elif not self.get_visible():
            self.show_all()
            self.reposition()
        return False

    def on_pointer_enter(self, *_args) -> bool:
        self.show_from_autohide()
        return False

    def on_pointer_leave(self, *_args) -> bool:
        self.schedule_autohide()
        return False

    def autohide_tick(self) -> bool:
        if not self.settings.auto_hide:
            if self.hidden:
                self.show_from_autohide()
            return True

        if not self.should_autohide_now():
            self.show_from_autohide()
            return True

        if self.hidden:
            if self.pointer_near_reveal_edge():
                self.show_from_autohide()
            return True

        if not self.pointer_over_dock():
            self.schedule_autohide()
        return True

    def monitor_geometry(self) -> Gdk.Rectangle | None:
        display = Gdk.Display.get_default()
        monitor = display.get_primary_monitor() if display else None
        if not monitor and display and display.get_n_monitors() > 0:
            monitor = display.get_monitor(0)
        return monitor.get_geometry() if monitor else None

    def dock_rect(self, geometry: Gdk.Rectangle | None = None) -> tuple[int, int, int, int] | None:
        geometry = geometry or self.monitor_geometry()
        if not geometry:
            return None
        width = max(self.get_allocated_width(), self.get_preferred_width()[1])
        height = max(self.get_allocated_height(), self.get_preferred_height()[1])
        margin_bottom = 0
        x = geometry.x + max(0, (geometry.width - width) // 2)
        y = geometry.y + max(0, geometry.height - height - margin_bottom)
        return (x, y, width, height)

    def pointer_position(self) -> tuple[int, int] | None:
        display = Gdk.Display.get_default()
        if not display:
            return None
        seat = display.get_default_seat()
        pointer = seat.get_pointer() if seat else None
        if not pointer:
            return None
        try:
            _screen, x, y = pointer.get_position()
            return (x, y)
        except TypeError:
            return None

    def pointer_over_dock(self) -> bool:
        if self.hidden:
            return False
        pointer = self.pointer_position()
        rect = self.dock_rect()
        if not pointer or not rect:
            return False
        x, y = pointer
        rx, ry, rw, rh = rect
        return rx <= x <= rx + rw and ry <= y <= ry + rh

    def pointer_near_reveal_edge(self) -> bool:
        pointer = self.pointer_position()
        geometry = self.monitor_geometry()
        rect = self.dock_rect(geometry)
        if not pointer or not geometry or not rect:
            return False
        x, y = pointer
        rx, _ry, rw, _rh = rect
        return (
            rx - 80 <= x <= rx + rw + 80
            and y >= geometry.y + geometry.height - REVEAL_EDGE_SIZE
        )

    def window_on_active_workspace(self, window) -> bool:
        if not self.wnck_screen:
            return True
        workspace = self.wnck_screen.get_active_workspace()
        if not workspace:
            return True
        if window.is_pinned():
            return True
        return window.is_visible_on_workspace(workspace) or window.is_on_workspace(workspace)

    def window_rect(self, window) -> tuple[int, int, int, int] | None:
        try:
            x, y, width, height = window.get_geometry()
        except (TypeError, ValueError):
            return None
        return (x, y, width, height)

    def window_should_autohide_dock(
        self,
        window,
        dock_rect: tuple[int, int, int, int],
        monitor_rect: tuple[int, int, int, int],
    ) -> bool:
        if window.is_skip_tasklist() or window.is_minimized() or not self.window_on_active_workspace(window):
            return False

        rect = self.window_rect(window)
        if not rect or not rects_intersect(rect, monitor_rect):
            return False

        if window.is_fullscreen() or window.is_maximized():
            return True

        return rects_intersect(rect, dock_rect)

    def should_autohide_now(self) -> bool:
        if not self.settings.auto_hide or not self.wnck_screen:
            return False

        geometry = self.monitor_geometry()
        dock_rect = self.dock_rect(geometry)
        if not geometry or not dock_rect:
            return False

        monitor_rect = (geometry.x, geometry.y, geometry.width, geometry.height)
        for window in self.wnck_screen.get_windows():
            if self.window_should_autohide_dock(window, dock_rect, monitor_rect):
                return True
        return False

    def setup_wnck(self) -> None:
        if Wnck is None:
            return
        self.wnck_screen = Wnck.Screen.get_default()
        if not self.wnck_screen:
            return
        self.wnck_screen.force_update()
        self.wnck_screen.connect("window-opened", lambda *_args: self.update_running_state())
        self.wnck_screen.connect("window-closed", lambda *_args: self.update_running_state())
        self.wnck_screen.connect("active-window-changed", lambda *_args: self.update_running_state())

    def monitor_launcher_dirs(self, dirs: list[Path]) -> None:
        for monitor in self.launcher_monitors:
            monitor.cancel()
        self.launcher_monitors.clear()

        for directory in dirs:
            try:
                gfile = Gio.File.new_for_path(str(directory))
                monitor = gfile.monitor_directory(Gio.FileMonitorFlags.NONE, None)
                monitor.connect("changed", self.on_launcher_dir_changed)
                self.launcher_monitors.append(monitor)
            except GLib.Error:
                continue

    def on_launcher_dir_changed(self, *_args) -> None:
        if self.reload_source_id:
            GLib.source_remove(self.reload_source_id)
        self.reload_source_id = GLib.timeout_add(250, self._reload_from_timeout)

    def _reload_from_timeout(self) -> bool:
        self.reload_source_id = 0
        self.reload_launchers()
        return False

    def reload_launchers(self) -> None:
        launchers, dirs = load_launchers()
        self.pinned_launchers = launchers
        self.monitor_launcher_dirs(dirs)
        self.render_launchers(self.launchers_with_running_apps())

    def render_launchers(self, launchers: list[Launcher]) -> None:
        for child in self.shell.get_children():
            self.shell.remove(child)
        self.items.clear()

        pinned_ids = {launcher.desktop_id for launcher in self.pinned_launchers}

        if not launchers:
            label = Gtk.Label(label=_("no_launchers"))
            label.get_style_context().add_class("milodock-empty")
            self.shell.pack_start(label, False, False, 0)
        else:
            for launcher in launchers:
                item = DockItem(self, launcher, pinned=launcher.desktop_id in pinned_ids)
                self.items.append(item)
                self.shell.pack_start(item, False, False, 0)

        self.shell.show_all()
        self.update_item_running_indicators()
        GLib.idle_add(self.reposition)

    def current_launchers(self) -> list[Launcher]:
        return [item.launcher for item in self.items if item.pinned]

    def persist_and_reload(self, launchers: list[Launcher]) -> None:
        save_launcher_order(launchers)
        self.reload_launchers()

    def persisted_launcher_ids(self) -> set[str]:
        directory = user_launcher_dir()
        if not directory.exists():
            return set()

        launcher_ids: set[str] = set()
        for item in directory.iterdir():
            if not item.is_file() or item.suffix not in {".dockitem", ".desktop"}:
                continue
            ref = launcher_ref_from_dockitem(item) if item.suffix == ".dockitem" else str(item)
            desktop_path = find_desktop_path(ref)
            if not desktop_path:
                continue
            launcher = launcher_from_desktop_path(desktop_path)
            if launcher:
                launcher_ids.add(launcher.desktop_id)
        return launcher_ids

    def launcher_is_persisted(self, launcher: Launcher) -> bool:
        return launcher.desktop_id in self.persisted_launcher_ids()

    def add_launcher_to_user_config(self, launcher: Launcher) -> None:
        launchers = self.current_launchers()
        if launcher.desktop_id not in {item.desktop_id for item in launchers}:
            launchers.append(launcher)
        self.persist_and_reload(launchers)

    def remove_launcher_from_user_config(self, launcher: Launcher) -> None:
        launchers = [item for item in self.current_launchers() if item.desktop_id != launcher.desktop_id]
        if not launchers:
            return
        self.persist_and_reload(launchers)

    def item_root_bounds(self, item: DockItem) -> tuple[int, int] | None:
        window = item.get_window()
        if not window:
            return None
        origin = window.get_origin()
        if len(origin) == 3:
            _ok, x, _y = origin
        else:
            x, _y = origin
        return (int(x), item.get_allocated_width())

    def clear_reorder_preview(self) -> None:
        for item in self.items:
            context = item.get_style_context()
            context.remove_class("milodock-drop-before")
            context.remove_class("milodock-drop-after")

    def reorder_insert_index_for_x(self, root_x: int) -> int:
        pinned_items = [item for item in self.items if item.pinned]
        for index, item in enumerate(pinned_items):
            bounds = self.item_root_bounds(item)
            if not bounds:
                continue
            item_x, item_width = bounds
            if root_x < item_x + item_width / 2:
                return index
        return len(pinned_items)

    def begin_reorder(self, item: DockItem, root_x: int) -> None:
        self.reorder_source = item
        item.get_style_context().add_class("milodock-item-running")
        self.update_reorder_preview(root_x)

    def update_reorder_preview(self, root_x: int) -> None:
        if not self.reorder_source:
            return
        pinned_items = [item for item in self.items if item.pinned]
        self.reorder_insert_index = self.reorder_insert_index_for_x(root_x)
        self.clear_reorder_preview()

        if not pinned_items:
            return
        if self.reorder_insert_index >= len(pinned_items):
            pinned_items[-1].get_style_context().add_class("milodock-drop-after")
        else:
            pinned_items[self.reorder_insert_index].get_style_context().add_class("milodock-drop-before")

    def finish_reorder(self, item: DockItem, root_x: int) -> None:
        if self.reorder_source is not item:
            self.clear_reorder_preview()
            self.reorder_source = None
            return

        self.update_reorder_preview(root_x)
        launchers = self.current_launchers()
        current_index = next((i for i, launcher in enumerate(launchers) if launcher.desktop_id == item.launcher.desktop_id), -1)
        insert_index = max(0, min(self.reorder_insert_index, len(launchers)))
        self.clear_reorder_preview()
        item.get_style_context().remove_class("milodock-item-running")
        self.reorder_source = None

        if current_index < 0:
            return
        launcher = launchers.pop(current_index)
        if current_index < insert_index:
            insert_index -= 1
        if current_index == insert_index:
            return
        launchers.insert(insert_index, launcher)
        self.persist_and_reload(launchers)

    def handle_drop(self, data, info: int, insert_index: int) -> bool:
        launchers = self.current_launchers()
        insert_index = max(0, min(insert_index, len(launchers)))

        if info == TARGET_INTERNAL:
            desktop_id = data.get_text()
            if not desktop_id:
                return False
            current_index = next((i for i, launcher in enumerate(launchers) if launcher.desktop_id == desktop_id), -1)
            if current_index < 0:
                return False
            launcher = launchers.pop(current_index)
            if current_index < insert_index:
                insert_index -= 1
            launchers.insert(insert_index, launcher)
            self.persist_and_reload(launchers)
            return True

        new_launchers: list[Launcher] = []
        if info == TARGET_URI_LIST:
            raw = data.get_uris()
            if raw:
                new_launchers = launchers_from_uri_list("\n".join(raw))
        elif info == TARGET_TEXT:
            text = data.get_text()
            launcher = launcher_from_text(text or "")
            if launcher:
                new_launchers = [launcher]

        added = False
        seen = {launcher.desktop_id for launcher in launchers}
        for launcher in new_launchers:
            if launcher.desktop_id in seen:
                continue
            launchers.insert(insert_index, launcher)
            insert_index += 1
            seen.add(launcher.desktop_id)
            added = True

        if added:
            self.persist_and_reload(launchers)
        return added

    def on_shell_drag_data_received(self, _widget, context, _x, _y, data, info, time_) -> None:
        handled = self.handle_drop(data, info, len(self.items))
        context.finish(handled, handled and info == TARGET_INTERNAL, time_)

    def reposition(self) -> bool:
        geometry = self.monitor_geometry()
        rect = self.dock_rect(geometry)
        if not geometry or not rect:
            return False

        x, y, _width, _height = rect
        if self.hidden and self.settings.auto_hide:
            y = geometry.y + geometry.height + 2
        self.move(x, y)
        return False

    def launch_context(self) -> Gio.AppLaunchContext:
        display = Gdk.Display.get_default()
        if display:
            context = display.get_app_launch_context()
            context.set_timestamp(Gtk.get_current_event_time())
            return context
        return Gio.AppLaunchContext()

    def launch(self, launcher: Launcher) -> None:
        try:
            launcher.app_info.launch([], self.launch_context())
        except GLib.Error:
            command = ""
            if hasattr(launcher.app_info, "get_commandline"):
                command = launcher.app_info.get_commandline() or ""
            if command:
                subprocess.Popen(shlex.split(command), cwd=str(Path.home()))

    def activate_or_launch(self, launcher: Launcher, force_new: bool = False) -> None:
        windows = self.running_windows_for(launcher)
        if windows and not force_new:
            window = windows[0]
            timestamp = Gtk.get_current_event_time()
            if hasattr(window, "unminimize") and window.is_minimized():
                window.unminimize(timestamp)
            window.activate(timestamp)
            return
        self.launch(launcher)

    def close_windows(self, windows) -> None:
        timestamp = Gtk.get_current_event_time()
        for window in windows:
            window.close(timestamp)

    def all_desktop_launchers(self) -> list[Launcher]:
        if self.desktop_launcher_cache is not None:
            return self.desktop_launcher_cache

        launchers: list[Launcher] = []
        seen: set[str] = set()
        for directory in app_dirs():
            if not directory.exists():
                continue
            for desktop_path in directory.glob("*.desktop"):
                launcher = launcher_from_desktop_path(desktop_path)
                if not launcher or launcher.desktop_id in seen:
                    continue
                seen.add(launcher.desktop_id)
                launchers.append(launcher)

        self.desktop_launcher_cache = launchers
        return launchers

    def window_tokens(self, window, include_title: bool = True) -> set[str]:
        values = set()
        app = window.get_application()
        class_group = window.get_class_group()

        raw_values = [
            app.get_name() if app else "",
            class_group.get_id() if class_group else "",
            class_group.get_name() if class_group else "",
        ]
        if include_title:
            raw_values.append(window.get_name() or "")

        for value in raw_values:
            if value:
                values.update(token_variants(value))
        return values

    def launcher_for_window(self, window) -> Launcher | None:
        tokens = self.window_tokens(window, include_title=False)
        if not tokens:
            return None
        for launcher in self.all_desktop_launchers():
            if tokens & launcher.match_tokens:
                return launcher
        return None

    def launchers_with_running_apps(self) -> list[Launcher]:
        launchers = list(self.pinned_launchers)
        seen = {launcher.desktop_id for launcher in launchers}
        if not self.wnck_screen:
            return launchers

        for window in self.wnck_screen.get_windows():
            if window.is_skip_tasklist() or not self.window_on_active_workspace(window):
                continue
            if any(self.window_matches_launcher(window, launcher) for launcher in launchers):
                continue

            launcher = self.launcher_for_window(window)
            if not launcher or launcher.desktop_id in seen:
                continue
            seen.add(launcher.desktop_id)
            launchers.append(launcher)
        return launchers

    def running_windows_for(self, launcher: Launcher):
        if not self.wnck_screen:
            return []
        matches = []
        for window in self.wnck_screen.get_windows():
            if window.is_skip_tasklist():
                continue
            if self.window_matches_launcher(window, launcher):
                matches.append(window)
        return matches

    def window_matches_launcher(self, window, launcher: Launcher) -> bool:
        return bool(self.window_tokens(window) & launcher.match_tokens)

    def update_running_state(self) -> bool:
        desired_launchers = self.launchers_with_running_apps()
        desired_ids = [launcher.desktop_id for launcher in desired_launchers]
        current_ids = [item.launcher.desktop_id for item in self.items]
        if desired_ids != current_ids:
            self.render_launchers(desired_launchers)
            return True

        self.update_item_running_indicators()
        return True

    def update_item_running_indicators(self) -> None:
        for item in self.items:
            item.set_running(bool(self.running_windows_for(item.launcher)))


def get_dock(app: Gtk.Application) -> MiloDock | None:
    return getattr(app, "dock_window", None)


def update_action_states(app: Gtk.Application, settings: DockSettings) -> None:
    action = app.lookup_action("autohide")
    if action:
        action.set_state(GLib.Variant.new_boolean(settings.auto_hide))


def show_preferences_dialog(dock: MiloDock) -> None:
    dialog = Gtk.Dialog(title=APP_NAME, transient_for=dock, modal=True)
    dialog.set_default_size(380, 260)
    dialog.add_button(_("cancel"), Gtk.ResponseType.CANCEL)
    dialog.add_button(_("apply"), Gtk.ResponseType.OK)

    area = dialog.get_content_area()
    grid = Gtk.Grid(column_spacing=14, row_spacing=12, margin=16)
    area.pack_start(grid, True, True, 0)

    size_label = Gtk.Label(label=_("icon_size"), xalign=0)
    size_scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 28, 64, 1)
    size_scale.set_value(dock.settings.icon_size)
    size_scale.set_digits(0)
    size_scale.set_hexpand(True)
    grid.attach(size_label, 0, 0, 1, 1)
    grid.attach(size_scale, 1, 0, 1, 1)

    spacing_label = Gtk.Label(label=_("launcher_spacing"), xalign=0)
    spacing_scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 16, 1)
    spacing_scale.set_value(dock.settings.launcher_spacing)
    spacing_scale.set_digits(0)
    spacing_scale.set_hexpand(True)
    grid.attach(spacing_label, 0, 1, 1, 1)
    grid.attach(spacing_scale, 1, 1, 1, 1)

    autohide_label = Gtk.Label(label=_("auto_hide"), xalign=0)
    autohide_switch = Gtk.Switch()
    autohide_switch.set_active(dock.settings.auto_hide)
    grid.attach(autohide_label, 0, 2, 1, 1)
    grid.attach(autohide_switch, 1, 2, 1, 1)

    effect_label = Gtk.Label(label=_("effect"), xalign=0)
    effect_combo = Gtk.ComboBoxText()
    for effect in EFFECTS:
        effect_combo.append(effect, _(effect))
    effect_combo.set_active_id(dock.settings.effect)
    grid.attach(effect_label, 0, 3, 1, 1)
    grid.attach(effect_combo, 1, 3, 1, 1)

    theme_label = Gtk.Label(label=_("theme"), xalign=0)
    theme_combo = Gtk.ComboBoxText()
    theme_combo.append("auto", _("theme_auto"))
    theme_combo.append("light", _("theme_light"))
    theme_combo.append("dark", _("theme_dark"))
    theme_combo.set_active_id(dock.settings.theme)
    grid.attach(theme_label, 0, 4, 1, 1)
    grid.attach(theme_combo, 1, 4, 1, 1)

    dialog.show_all()
    response = dialog.run()
    if response == Gtk.ResponseType.OK:
        dock.settings.icon_size = int(size_scale.get_value())
        dock.settings.launcher_spacing = int(spacing_scale.get_value())
        dock.settings.auto_hide = autohide_switch.get_active()
        dock.settings.effect = effect_combo.get_active_id() or "magnify"
        dock.settings.theme = theme_combo.get_active_id() or "auto"
        dock.settings.save()
        dock.reload_settings()
    dialog.destroy()


def setup_application_menu(app: Gtk.Application) -> None:
    menubar = Gio.Menu()

    dock_menu = Gio.Menu()
    dock_menu.append(_("preferences"), "app.preferences")
    dock_menu.append(_("reload"), "app.reload")
    menubar.append_submenu(_("dock"), dock_menu)

    settings_menu = Gio.Menu()
    size_menu = Gio.Menu()
    size_menu.append(_("small"), "app.size-small")
    size_menu.append(_("medium"), "app.size-medium")
    size_menu.append(_("large"), "app.size-large")
    settings_menu.append_submenu(_("icon_size"), size_menu)

    spacing_menu = Gio.Menu()
    spacing_menu.append(_("compact"), "app.spacing-compact")
    spacing_menu.append(_("normal"), "app.spacing-normal")
    spacing_menu.append(_("wide"), "app.spacing-wide")
    settings_menu.append_submenu(_("launcher_spacing"), spacing_menu)

    settings_menu.append(_("auto_hide"), "app.autohide")

    effect_menu = Gio.Menu()
    effect_menu.append(_("magnify"), "app.effect-magnify")
    effect_menu.append(_("lift"), "app.effect-lift")
    effect_menu.append(_("none"), "app.effect-none")
    settings_menu.append_submenu(_("effect"), effect_menu)

    theme_menu = Gio.Menu()
    theme_menu.append(_("theme_auto"), "app.theme-auto")
    theme_menu.append(_("theme_light"), "app.theme-light")
    theme_menu.append(_("theme_dark"), "app.theme-dark")
    settings_menu.append_submenu(_("theme"), theme_menu)

    menubar.append_submenu(_("settings"), settings_menu)
    app.set_menubar(menubar)


def add_simple_action(app: Gtk.Application, name: str, callback) -> None:
    action = Gio.SimpleAction.new(name, None)
    action.connect("activate", callback)
    app.add_action(action)


def setup_application_actions(app: Gtk.Application) -> None:
    add_simple_action(app, "preferences", lambda *_args: show_preferences_dialog(get_dock(app)) if get_dock(app) else None)
    add_simple_action(app, "reload", lambda *_args: get_dock(app).reload_launchers() if get_dock(app) else None)

    for preset, size in SIZE_PRESETS.items():
        add_simple_action(
            app,
            f"size-{preset}",
            lambda _action, _param, s=size: get_dock(app).set_icon_size(s) if get_dock(app) else None,
        )

    for preset, spacing in SPACING_PRESETS.items():
        add_simple_action(
            app,
            f"spacing-{preset}",
            lambda _action, _param, s=spacing: get_dock(app).set_launcher_spacing(s) if get_dock(app) else None,
        )

    autohide_action = Gio.SimpleAction.new_stateful("autohide", None, GLib.Variant.new_boolean(DockSettings.load().auto_hide))
    autohide_action.connect(
        "activate",
        lambda action, _param: get_dock(app).set_auto_hide(not action.get_state().get_boolean()) if get_dock(app) else None,
    )
    app.add_action(autohide_action)

    for effect in EFFECTS:
        add_simple_action(
            app,
            f"effect-{effect}",
            lambda _action, _param, e=effect: get_dock(app).set_effect(e) if get_dock(app) else None,
        )

    for theme in THEME_MODES:
        add_simple_action(
            app,
            f"theme-{theme}",
            lambda _action, _param, t=theme: get_dock(app).set_theme_mode(t) if get_dock(app) else None,
        )


def on_application_activate(app: Gtk.Application) -> None:
    dock = get_dock(app)
    if dock:
        dock.show_from_autohide()
        dock.present()
        return
    dock = MiloDock(app)
    app.dock_window = dock
    update_action_states(app, dock.settings)
    dock.show_all()
    GLib.idle_add(dock.reposition)


def on_application_startup(app: Gtk.Application) -> None:
    setup_application_actions(app)
    setup_application_menu(app)


def process_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def claim_single_instance(replace: bool) -> None:
    runtime_dir = Path(os.environ.get("XDG_RUNTIME_DIR", xdg_cache_home()))
    state_dir = runtime_dir / APP_ID
    state_dir.mkdir(parents=True, exist_ok=True)
    pidfile = state_dir / f"{APP_ID}.pid"

    if pidfile.exists():
        try:
            old_pid = int(pidfile.read_text().strip())
        except (ValueError, OSError):
            old_pid = 0

        if old_pid and old_pid != os.getpid() and process_alive(old_pid):
            if not replace:
                print(_("already_running"), file=sys.stderr)
                sys.exit(0)
            try:
                os.kill(old_pid, signal.SIGTERM)
                for _ in range(30):
                    if not process_alive(old_pid):
                        break
                    time.sleep(0.1)
                if process_alive(old_pid):
                    os.kill(old_pid, signal.SIGKILL)
            except OSError:
                pass

    pidfile.write_text(str(os.getpid()), encoding="utf-8")

    def cleanup() -> None:
        try:
            if pidfile.read_text(encoding="utf-8").strip() == str(os.getpid()):
                pidfile.unlink()
        except OSError:
            pass

    atexit.register(cleanup)


def main() -> int:
    parser = argparse.ArgumentParser(description="miloOS native Python dock prototype")
    parser.add_argument("--replace", action="store_true", help="replace any running miloDock instance")
    args = parser.parse_args()

    locale.setlocale(locale.LC_ALL, "")
    GLib.set_prgname(APP_ID)
    GLib.set_application_name(APP_NAME)
    normalize_process_cwd()
    claim_single_instance(args.replace)

    app = Gtk.Application(application_id="org.miloOS.miloDock", flags=Gio.ApplicationFlags.FLAGS_NONE)
    app.connect("startup", on_application_startup)
    app.connect("activate", on_application_activate)

    signal.signal(signal.SIGTERM, lambda *_args: app.quit())
    signal.signal(signal.SIGINT, lambda *_args: app.quit())
    if hasattr(signal, "SIGUSR1"):
        signal.signal(
            signal.SIGUSR1,
            lambda *_args: GLib.idle_add(lambda: get_dock(app).reload_settings() if get_dock(app) else False),
        )

    return app.run([sys.argv[0]])


if __name__ == "__main__":
    raise SystemExit(main())
