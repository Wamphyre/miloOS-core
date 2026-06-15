#!/usr/bin/env python3
"""
miloFiles - miloOS Finder-style File Manager
Hybrid design between Snow Leopard classic Finder and modern miloOS styles.
"""

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('Gdk', '3.0')
gi.require_version('GdkPixbuf', '2.0')
gi.require_version('Gio', '2.0')
from gi.repository import Gtk, Gdk, GLib, GdkPixbuf, Gio

import os
import sys
import shutil
import locale
import subprocess
import datetime
import mimetypes
import pwd
import grp
import threading
import time

# Translations
TRANSLATIONS = {
    'en': {
        'title': 'miloFiles',
        'devices': 'DEVICES',
        'favorites': 'FAVORITES',
        'name': 'Name',
        'size': 'Size',
        'type': 'Type',
        'modified': 'Modified',
        'items': 'items',
        'free': 'free',
        'search': 'Search',
        'open': 'Open',
        'properties': 'Get Info',
        'cut': 'Cut',
        'copy': 'Copy',
        'paste': 'Paste',
        'rename': 'Rename',
        'trash_action': 'Move to Trash',
        'trash': 'Trash',
        'delete': 'Delete Permanently',
        'file_system': 'File System',
        'go_location': 'Go to Location...',
        'new_folder': 'New Folder',
        'new_file': 'New File',
        'properties_title': 'Get Info',
        'general': 'General',
        'permissions': 'Permissions',
        'location': 'Location',
        'owner': 'Owner',
        'folder': 'Folder',
        'file': 'File',
        'cannot_read_dir': 'Cannot read directory',
        'new_folder_name': 'New Folder',
        'new_file_name': 'New File.txt',
        'enter_new_name': 'Enter new name:',
        'confirm_delete': 'Are you sure you want to permanently delete these items?',
        'delete_title': 'Confirm Delete',
        'paste_error': 'Paste Error',
        'cannot_create_folder': 'Cannot create folder',
        'open_error': 'Cannot open file',
        'group': 'Group',
        'octal': 'Octal',
        'apply': 'Apply',
        'file_menu': 'File',
        'edit_menu': 'Edit',
        'view_menu': 'View',
        'go_menu': 'Go',
        'close': 'Close',
        'view_icon': 'as Icons',
        'view_list': 'as List',
        'show_hidden_menu': 'Show Hidden Files',
        'go_back': 'Back',
        'go_forward': 'Forward',
        'go_up': 'Enclosing Folder',
        'connect_to_server': 'Connect to Server',
        'enter_server_address': 'Enter server address:',
        'connect': 'Connect',
        'connection_error': 'Connection Error',
        'compress': 'Compress...',
        'extract_here': 'Extract Here',
        'compress_error': 'Compression Error',
        'extract_error': 'Extraction Error',
        'eject_error': 'Eject Error',
        'archive_name': 'Archive name:',
        'format': 'Format:',
        'calculating': 'Calculating...',
        'dangerous_archive_path': 'Dangerous path traversal detected in archive!',
        'open_with': 'Open with',
        'other_application': 'Other Application...',
        'connect_placeholder': 'smb://server/share or ftp://server/folder',
        'open_terminal': 'Open Terminal',
        'cancel': 'Cancel',
        'compressing': 'Compressing files...',
        'extracting': 'Extracting archive...',
        'pasting': 'Copying/Moving files...',
        'empty_trash': 'Empty Trash',
        'rename_favorite': 'Rename Favorite...',
        'remove_favorite': 'Remove from Favorites',
        'confirm_empty_trash': 'Are you sure you want to empty the Trash?',
        'add_to_favorites': 'Add to Favorites',
        'mount_volume': 'Mount Volume',
        'unmount_volume': 'Unmount Volume',
    },
    'es': {
        'title': 'miloFiles',
        'devices': 'DISPOSITIVOS',
        'favorites': 'FAVORITOS',
        'name': 'Nombre',
        'size': 'Tamaño',
        'type': 'Tipo',
        'modified': 'Modificado',
        'items': 'elementos',
        'free': 'libres',
        'search': 'Buscar',
        'open': 'Abrir',
        'properties': 'Obtener Información',
        'cut': 'Cortar',
        'copy': 'Copiar',
        'paste': 'Pegar',
        'rename': 'Renombrar',
        'trash_action': 'Mover a la Papelera',
        'trash': 'Papelera',
        'delete': 'Eliminar Permanentemente',
        'file_system': 'Sistema de archivos',
        'go_location': 'Ir a la ubicación...',
        'new_folder': 'Nueva Carpeta',
        'new_file': 'Nuevo Archivo',
        'properties_title': 'Obtener Información',
        'general': 'General',
        'permissions': 'Permisos',
        'location': 'Ubicación',
        'owner': 'Propietario',
        'folder': 'Carpeta',
        'file': 'Archivo',
        'cannot_read_dir': 'No se puede leer el directorio',
        'new_folder_name': 'Nueva Carpeta',
        'new_file_name': 'Nuevo Archivo.txt',
        'enter_new_name': 'Introduzca el nuevo nombre:',
        'confirm_delete': '¿Está seguro de que desea eliminar permanentemente estos elementos?',
        'delete_title': 'Confirmar Eliminación',
        'paste_error': 'Error al Pegar',
        'cannot_create_folder': 'No se pudo crear la carpeta',
        'open_error': 'No se pudo abrir el archivo',
        'group': 'Grupo',
        'octal': 'Octal',
        'apply': 'Aplicar',
        'file_menu': 'Archivo',
        'edit_menu': 'Editar',
        'view_menu': 'Ver',
        'go_menu': 'Ir',
        'close': 'Cerrar',
        'view_icon': 'como Iconos',
        'view_list': 'como Lista',
        'show_hidden_menu': 'Mostrar archivos ocultos',
        'go_back': 'Atrás',
        'go_forward': 'Adelante',
        'go_up': 'Carpeta contenedora',
        'connect_to_server': 'Conectarse al servidor',
        'enter_server_address': 'Introduzca la dirección del servidor:',
        'connect': 'Conectar',
        'connection_error': 'Error de conexión',
        'compress': 'Comprimir...',
        'extract_here': 'Extraer aquí',
        'compress_error': 'Error de compresión',
        'extract_error': 'Error de extracción',
        'eject_error': 'Error al expulsar',
        'archive_name': 'Nombre del archivo:',
        'format': 'Formato:',
        'calculating': 'Calculando...',
        'dangerous_archive_path': '¡Ruta de extracción peligrosa detectada en el archivo!',
        'open_with': 'Abrir con',
        'other_application': 'Otra aplicación...',
        'connect_placeholder': 'smb://servidor/carpeta o ftp://servidor/carpeta',
        'open_terminal': 'Abrir terminal',
        'cancel': 'Cancelar',
        'compressing': 'Comprimiendo archivos...',
        'extracting': 'Extrayendo archivo...',
        'pasting': 'Copiando/Moviendo archivos...',
        'empty_trash': 'Vaciar papelera',
        'rename_favorite': 'Renombrar favorito...',
        'remove_favorite': 'Quitar de favoritos',
        'confirm_empty_trash': '¿Está seguro de que desea vaciar la papelera?',
        'add_to_favorites': 'Añadir a favoritos',
        'mount_volume': 'Montar volumen',
        'unmount_volume': 'Desmontar volumen',
    }
}

def get_language():
    try:
        lang = locale.getlocale()[0]
        if lang and lang.startswith('es'):
            return 'es'
    except:
        pass
    return 'en'

def _(key):
    lang = get_language()
    return TRANSLATIONS.get(lang, TRANSLATIONS['en']).get(key, key)

def format_bytes(bytes_val):
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if bytes_val < 1024.0:
            return f"{bytes_val:.1f} {unit}"
        bytes_val /= 1024.0
    return f"{bytes_val:.1f} PB"

def get_file_type_desc(path, is_dir=False):
    if is_dir:
        return _("folder")
    mime, _unused = mimetypes.guess_type(path)
    if mime:
        return mime.split('/')[-1].upper() + " " + _("file")
    ext = os.path.splitext(path)[1]
    if ext:
        return ext[1:].upper() + " " + _("file")
    return _("file")

_icon_pixbuf_cache = {}
def get_icon_pixbuf(icon_name, size=48):
    key = (icon_name, size)
    if key in _icon_pixbuf_cache:
        return _icon_pixbuf_cache[key]
    theme = Gtk.IconTheme.get_default()
    pb = None
    try:
        pb = theme.load_icon(icon_name, size, Gtk.IconLookupFlags.FORCE_SIZE)
    except Exception:
        try:
            pb = theme.load_icon("folder" if icon_name == "folder" else "text-x-generic", size, Gtk.IconLookupFlags.FORCE_SIZE)
        except Exception:
            pb = None
    _icon_pixbuf_cache[key] = pb
    return pb

_icon_cache = {}
def get_file_icon(path, size=48, is_dir=None):
    if is_dir is None:
        is_dir = os.path.isdir(path)
        
    if is_dir:
        name = os.path.basename(path.rstrip('/')).lower()
        if path == os.path.expanduser('~'):
            cache_key = ("dir", "home", size)
        elif name in ["downloads", "documents", "desktop", "music", "pictures", "videos"]:
            cache_key = ("dir", name, size)
        else:
            cache_key = ("dir", "generic", size)
    else:
        ext = os.path.splitext(path)[1].lower()
        mime, _ = mimetypes.guess_type(path)
        cache_key = ("file", mime, ext, size)

    if cache_key in _icon_cache:
        return _icon_cache[cache_key]

    pb = None
    theme = Gtk.IconTheme.get_default()
    if is_dir:
        name = os.path.basename(path.rstrip('/')).lower()
        if path == os.path.expanduser('~'):
            pb = get_icon_pixbuf("user-home", size)
        elif name == "downloads":
            pb = get_icon_pixbuf("folder-download", size)
        elif name == "documents":
            pb = get_icon_pixbuf("folder-documents", size)
        elif name == "desktop":
            pb = get_icon_pixbuf("folder-desktop", size)
        elif name == "music":
            pb = get_icon_pixbuf("folder-music", size)
        elif name == "pictures":
            pb = get_icon_pixbuf("folder-pictures", size)
        elif name == "videos":
            pb = get_icon_pixbuf("folder-videos", size)
        else:
            pb = get_icon_pixbuf("folder", size)
    else:
        mime, _unused = mimetypes.guess_type(path)
        if mime:
            icon_name = mime.replace('/', '-')
            pb = get_icon_pixbuf(icon_name, size)
            if not pb:
                type_prefix = mime.split('/')[0]
                pb = get_icon_pixbuf(f"{type_prefix}-x-generic", size)
        if not pb:
            try:
                gfile = Gio.File.new_for_path(path)
                info = gfile.query_info("standard::icon", Gio.FileQueryInfoFlags.NONE, None)
                icon = info.get_icon()
                if icon:
                    icon_info = theme.lookup_by_gicon(icon, size, Gtk.IconLookupFlags.FORCE_SIZE)
                    if icon_info:
                        pb = icon_info.load_icon()
            except:
                pass
        if not pb:
            pb = get_icon_pixbuf("text-x-generic", size)

    _icon_cache[cache_key] = pb
    return pb

def get_custom_default_command(path):
    ext = os.path.splitext(path)[1].lower()
    mime, _ = mimetypes.guess_type(path)
    if not mime:
        mime = ""
    
    # 1. PDF files: open with firefox or chrome
    if ext == '.pdf' or mime == 'application/pdf':
        for browser in ['firefox', 'google-chrome', 'chrome', 'chromium']:
            if shutil.which(browser):
                return [browser]
        return None
        
    # 2. Audio files compatible with VLC
    audio_extensions = {'.mp3', '.wav', '.ogg', '.flac', '.m4a', '.aac', '.wma', '.opus', '.mid', '.midi', '.mka'}
    if ext in audio_extensions or mime.startswith('audio/'):
        if shutil.which('vlc'):
            return ['vlc']
            
    # 3. Video files: VLC
    video_extensions = {'.mp4', '.mkv', '.avi', '.mov', '.wmv', '.flv', '.webm', '.mpeg', '.mpg', '.m4v'}
    if ext in video_extensions or mime.startswith('video/'):
        if shutil.which('vlc'):
            return ['vlc']
            
    # 4. Image files: ristretto
    image_extensions = {'.png', '.jpg', '.jpeg', '.gif', '.webp', '.bmp', '.svg', '.tiff', '.ico'}
    if ext in image_extensions or mime.startswith('image/'):
        if shutil.which('ristretto'):
            return ['ristretto']
            
    # 5. Text files: mousepad (xfce text editor)
    text_extensions = {'.txt', '.md', '.py', '.sh', '.json', '.xml', '.cfg', '.conf', '.ini', '.yaml', '.yml', '.log', '.js', '.css', '.html', '.c', '.cpp', '.h', '.hpp', '.java', '.go', '.rs'}
    if ext in text_extensions or mime.startswith('text/') or mime in ['application/x-shellscript', 'application/javascript', 'application/json']:
        if shutil.which('mousepad'):
            return ['mousepad']
            
    return None


class OperationProgressDialog(Gtk.Window):
    def __init__(self, parent, title, message, cancel_callback=None):
        super().__init__(title=title)
        self.set_transient_for(parent)
        self.set_modal(True)
        self.set_destroy_with_parent(True)
        self.set_default_size(350, 110)
        self.set_resizable(False)
        self.set_position(Gtk.WindowPosition.CENTER_ON_PARENT)
        
        vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        vbox.set_border_width(14)
        self.add(vbox)
        
        self.lbl_msg = Gtk.Label(label=message)
        self.lbl_msg.set_alignment(0.0, 0.5)
        self.lbl_msg.set_line_wrap(True)
        self.lbl_msg.set_max_width_chars(45)
        vbox.pack_start(self.lbl_msg, False, False, 0)
        
        self.pbar = Gtk.ProgressBar()
        vbox.pack_start(self.pbar, False, False, 0)
        
        self.pulse_timeout_id = GLib.timeout_add(100, self.pulse_progressbar)
        
        self.cancel_callback = cancel_callback
        if cancel_callback:
            hbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
            btn_cancel = Gtk.Button(label=_("cancel"))
            btn_cancel.connect("clicked", self.on_cancel)
            hbox.pack_end(btn_cancel, False, False, 0)
            vbox.pack_start(hbox, False, False, 0)
            
        self.show_all()
        
    def pulse_progressbar(self):
        self.pbar.pulse()
        return True
        
    def update_message(self, message):
        self.lbl_msg.set_text(message)
        
    def on_cancel(self, button):
        if self.cancel_callback:
            self.cancel_callback()
        self.close_dialog()
        
    def close_dialog(self):
        if self.pulse_timeout_id:
            GLib.source_remove(self.pulse_timeout_id)
            self.pulse_timeout_id = None
        self.destroy()


class miloFilesWindow(Gtk.Window):
    def __init__(self, initial_dir=None):
        super().__init__(title=_('title'))
        self.set_name("milofiles-window")
        self.set_icon_name("milofiles")
        self.set_default_size(950, 620)
        self.set_position(Gtk.WindowPosition.CENTER)
        
        # Clipboard management
        self.clipboard_files = []
        self.clipboard_action = None  # 'copy' or 'cut'
        
        # Navigation History
        self.history = []
        self.history_index = -1
        
        # Hidden files setting
        self.show_hidden = False
        
        # Thumbnail cache and cancel-aware tracking ID
        self.thumbnail_cache = {}
        self.current_thumbnail_load_id = 0
        
        # Main layout container
        main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        self.add(main_box)
        
        # Setup MenuBar (for Global Menu integration)
        self.setup_menu_bar()
        main_box.pack_start(self.menu_bar, False, False, 0)
        
        # Build Top Toolbar
        self.toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        self.toolbar.get_style_context().add_class("main-toolbar")
        main_box.pack_start(self.toolbar, False, False, 0)
        
        # Navigation buttons: Back / Forward / Up
        self.back_btn = Gtk.Button()
        self.back_btn.set_image(Gtk.Image.new_from_icon_name("go-previous", Gtk.IconSize.BUTTON))
        self.back_btn.connect("clicked", self.on_back_clicked)
        self.toolbar.pack_start(self.back_btn, False, False, 0)
        
        self.forward_btn = Gtk.Button()
        self.forward_btn.set_image(Gtk.Image.new_from_icon_name("go-next", Gtk.IconSize.BUTTON))
        self.forward_btn.connect("clicked", self.on_forward_clicked)
        self.toolbar.pack_start(self.forward_btn, False, False, 0)
        
        self.up_btn = Gtk.Button()
        self.up_btn.set_image(Gtk.Image.new_from_icon_name("go-up", Gtk.IconSize.BUTTON))
        self.up_btn.connect("clicked", self.on_up_clicked)
        self.toolbar.pack_start(self.up_btn, False, False, 0)
        
        # Path breadcrumb bar + Entry (Stack)
        self.path_stack = Gtk.Stack()
        self.path_stack.set_transition_type(Gtk.StackTransitionType.CROSSFADE)
        self.path_stack.set_transition_duration(150)
        
        path_scroll = Gtk.ScrolledWindow()
        path_scroll.set_shadow_type(Gtk.ShadowType.NONE)
        path_scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.NEVER)
        path_scroll.get_style_context().add_class("path-scroll")
        path_scroll.set_hexpand(True)
        path_scroll.set_events(Gdk.EventMask.BUTTON_PRESS_MASK)
        path_scroll.connect("button-press-event", self.on_path_scroll_clicked)
        
        self.path_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=4)
        self.path_box.set_valign(Gtk.Align.CENTER)
        path_scroll.add(self.path_box)
        
        self.path_entry = Gtk.Entry()
        self.path_entry.get_style_context().add_class("path-entry")
        self.path_entry.set_hexpand(True)
        self.path_entry.connect("activate", self.on_path_entry_activated)
        self.path_entry.connect("key-press-event", self.on_path_entry_key_press)
        self.path_entry.connect("focus-out-event", lambda w, e: self.show_path_breadcrumbs())
        
        # Setup path entry completion
        completion = Gtk.EntryCompletion()
        self.path_completion_model = Gtk.ListStore(str)
        completion.set_model(self.path_completion_model)
        completion.set_text_column(0)
        completion.set_inline_completion(True)
        completion.set_popup_completion(True)
        self.path_entry.set_completion(completion)
        self.update_path_recommendations()
        
        self.path_stack.add_named(path_scroll, "buttons")
        self.path_stack.add_named(self.path_entry, "entry")
        
        self.toolbar.pack_start(self.path_stack, True, True, 6)
        
        # View Switcher (Segmented Control style)
        self.view_switch_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        self.view_switch_box.get_style_context().add_class("segmented-control")
        
        self.icon_view_btn = Gtk.RadioButton.new(None)
        self.icon_view_btn.set_mode(False)
        img_grid = Gtk.Image.new_from_icon_name("view-grid-symbolic", Gtk.IconSize.BUTTON)
        self.icon_view_btn.add(img_grid)
        self.icon_view_btn.get_style_context().add_class("segmented-btn")
        self.icon_view_btn.set_active(True)
        self.icon_view_btn.connect("toggled", self.on_view_changed, "icon")
        self.view_switch_box.pack_start(self.icon_view_btn, False, False, 0)
        
        self.list_view_btn = Gtk.RadioButton.new_from_widget(self.icon_view_btn)
        self.list_view_btn.set_mode(False)
        img_list = Gtk.Image.new_from_icon_name("view-list-symbolic", Gtk.IconSize.BUTTON)
        self.list_view_btn.add(img_list)
        self.list_view_btn.get_style_context().add_class("segmented-btn")
        self.list_view_btn.connect("toggled", self.on_view_changed, "list")
        self.view_switch_box.pack_start(self.list_view_btn, False, False, 0)
        
        self.toolbar.pack_start(self.view_switch_box, False, False, 6)
        
        # Real-time search entry
        self.search_entry = Gtk.SearchEntry()
        self.search_entry.set_placeholder_text(_("search") + "...")
        self.search_entry.get_style_context().add_class("search-entry")
        self.search_entry.connect("search-changed", self.on_search_changed)
        self.toolbar.pack_start(self.search_entry, False, False, 0)
        
        # Body Paned layout (Sidebar + Files Panel)
        body_paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        body_paned.set_position(220)
        main_box.pack_start(body_paned, True, True, 0)
        
        # 1. Left Scrollable Sidebar
        sidebar_scroll = Gtk.ScrolledWindow()
        sidebar_scroll.set_shadow_type(Gtk.ShadowType.NONE)
        sidebar_scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        sidebar_scroll.get_style_context().add_class("sidebar-scroll")
        
        self.sidebar_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.sidebar_box.set_margin_top(12)
        self.sidebar_box.set_margin_bottom(12)
        sidebar_scroll.add(self.sidebar_box)
        body_paned.pack1(sidebar_scroll, False, False)
        
        # Sidebar Lists Tracker
        self.sidebar_lists = []
        
        # Setup Volume Monitor for dynamic devices sidebar updates
        self.volume_monitor = Gio.VolumeMonitor.get()
        self.volume_monitor.connect("mount-added", self.on_mounts_changed)
        self.volume_monitor.connect("mount-removed", self.on_mounts_changed)
        self.volume_monitor.connect("volume-added", self.on_mounts_changed)
        self.volume_monitor.connect("volume-removed", self.on_mounts_changed)
        
        self.setup_sidebar()
        
        # 2. Right File List Card
        file_card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        file_card.get_style_context().add_class("file-card")
        body_paned.pack2(file_card, True, True)
        
        # View stack
        self.view_stack = Gtk.Stack()
        self.view_stack.set_transition_type(Gtk.StackTransitionType.CROSSFADE)
        self.view_stack.set_transition_duration(150)
        file_card.pack_start(self.view_stack, True, True, 0)
        
        # View stores initialization
        # Icon view store: [Pixbuf, name, path, is_dir]
        self.icon_store = Gtk.ListStore(GdkPixbuf.Pixbuf, str, str, bool)
        
        # Filtered Icon Store for search / hidden
        self.icon_filter = self.icon_store.filter_new()
        self.icon_filter.set_visible_func(self.filter_visible_func)
        
        # List view store: [Pixbuf, name, size, type, modified, path, is_dir]
        self.list_store = Gtk.ListStore(GdkPixbuf.Pixbuf, str, str, str, str, str, bool)
        
        self.list_filter = self.list_store.filter_new()
        self.list_filter.set_visible_func(self.filter_visible_func)
        
        # Setup views
        self.setup_icon_view()
        self.setup_list_view()
        
        # Statusbar
        self.statusbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        self.statusbar.get_style_context().add_class("statusbar")
        self.status_lbl = Gtk.Label(label="")
        self.statusbar.pack_start(self.status_lbl, True, True, 0)
        main_box.pack_start(self.statusbar, False, False, 0)
        
        # Apply CSS style theme
        self.apply_theme()
        Gtk.Settings.get_default().connect("notify::gtk-theme-name", lambda s, p: self.apply_theme())
        
        # Key bindings
        self.connect("key-press-event", self.on_key_press)
        
        # Initial Directory Loading
        if initial_dir and os.path.isdir(initial_dir):
            self.load_directory(initial_dir)
        else:
            self.load_directory(os.path.expanduser('~'))

    def setup_menu_bar(self):
        self.menu_bar = Gtk.MenuBar()
        
        # File Menu
        file_menu = Gtk.Menu()
        file_mitem = Gtk.MenuItem(label=_("file_menu"))
        file_mitem.set_submenu(file_menu)
        
        new_folder_mitem = Gtk.MenuItem(label=_("new_folder"))
        new_folder_mitem.connect("activate", self.on_menu_new_folder)
        file_menu.append(new_folder_mitem)
        
        new_file_mitem = Gtk.MenuItem(label=_("new_file"))
        new_file_mitem.connect("activate", self.on_menu_new_file)
        file_menu.append(new_file_mitem)
        
        file_menu.append(Gtk.SeparatorMenuItem())
        
        close_mitem = Gtk.MenuItem(label=_("close"))
        close_mitem.connect("activate", lambda w: self.close())
        file_menu.append(close_mitem)
        
        self.menu_bar.append(file_mitem)
        
        # Edit Menu
        edit_menu = Gtk.Menu()
        edit_mitem = Gtk.MenuItem(label=_("edit_menu"))
        edit_mitem.set_submenu(edit_menu)
        
        cut_mitem = Gtk.MenuItem(label=_("cut"))
        cut_mitem.connect("activate", self.on_menu_cut)
        edit_menu.append(cut_mitem)
        
        copy_mitem = Gtk.MenuItem(label=_("copy"))
        copy_mitem.connect("activate", self.on_menu_copy)
        edit_menu.append(copy_mitem)
        
        paste_mitem = Gtk.MenuItem(label=_("paste"))
        paste_mitem.connect("activate", self.on_menu_paste)
        edit_menu.append(paste_mitem)
        
        edit_menu.append(Gtk.SeparatorMenuItem())
        
        rename_mitem = Gtk.MenuItem(label=_("rename"))
        rename_mitem.connect("activate", self.on_menu_rename)
        edit_menu.append(rename_mitem)
        
        trash_mitem = Gtk.MenuItem(label=_("trash_action"))
        trash_mitem.connect("activate", self.on_menu_trash)
        edit_menu.append(trash_mitem)
        
        delete_mitem = Gtk.MenuItem(label=_("delete"))
        delete_mitem.connect("activate", self.on_menu_delete)
        edit_menu.append(delete_mitem)
        
        edit_menu.append(Gtk.SeparatorMenuItem())
        
        compress_mitem = Gtk.MenuItem(label=_("compress"))
        compress_mitem.connect("activate", self.on_menu_compress)
        edit_menu.append(compress_mitem)
        
        extract_mitem = Gtk.MenuItem(label=_("extract_here"))
        extract_mitem.connect("activate", self.on_menu_extract)
        edit_menu.append(extract_mitem)
        
        self.menu_bar.append(edit_mitem)
        
        # View Menu
        view_menu = Gtk.Menu()
        view_mitem = Gtk.MenuItem(label=_("view_menu"))
        view_mitem.set_submenu(view_menu)
        
        icon_view_mitem = Gtk.MenuItem(label=_("view_icon"))
        icon_view_mitem.connect("activate", lambda w: self.icon_view_btn.set_active(True))
        view_menu.append(icon_view_mitem)
        
        list_view_mitem = Gtk.MenuItem(label=_("view_list"))
        list_view_mitem.connect("activate", lambda w: self.list_view_btn.set_active(True))
        view_menu.append(list_view_mitem)
        
        view_menu.append(Gtk.SeparatorMenuItem())
        
        self.hidden_mitem = Gtk.CheckMenuItem(label=_("show_hidden_menu"))
        self.hidden_mitem.set_active(self.show_hidden)
        self.hidden_mitem.connect("toggled", self.on_toggle_hidden_menu)
        view_menu.append(self.hidden_mitem)
        
        self.menu_bar.append(view_mitem)
        
        # Go Menu
        go_menu = Gtk.Menu()
        go_mitem = Gtk.MenuItem(label=_("go_menu"))
        go_mitem.set_submenu(go_menu)
        
        back_mitem = Gtk.MenuItem(label=_("go_back"))
        back_mitem.connect("activate", lambda w: self.on_back_clicked(None))
        go_menu.append(back_mitem)
        
        forward_mitem = Gtk.MenuItem(label=_("go_forward"))
        forward_mitem.connect("activate", lambda w: self.on_forward_clicked(None))
        go_menu.append(forward_mitem)
        
        up_mitem = Gtk.MenuItem(label=_("go_up"))
        up_mitem.connect("activate", lambda w: self.on_up_clicked(None))
        go_menu.append(up_mitem)
        
        go_menu.append(Gtk.SeparatorMenuItem())
        
        go_location_mitem = Gtk.MenuItem(label=_("go_location"))
        go_location_mitem.connect("activate", lambda w: self.show_path_entry())
        go_menu.append(go_location_mitem)
        
        go_menu.append(Gtk.SeparatorMenuItem())
        
        connect_mitem = Gtk.MenuItem(label=_("connect_to_server"))
        connect_mitem.connect("activate", self.on_menu_connect_server)
        go_menu.append(connect_mitem)
        
        self.menu_bar.append(go_mitem)

    def on_toggle_hidden_menu(self, widget):
        self.show_hidden = widget.get_active()
        self.load_directory(self.current_dir)

    def apply_theme(self):
        settings = Gtk.Settings.get_default()
        theme_name = settings.get_property("gtk-theme-name")
        is_dark = (theme_name == "miloOS-Dark")
        
        # Colors based on light/dark mode
        if is_dark:
            bg_window = "#18181a"
            border_window = "#2d2d30" # Match the window manager header/dark border
            bg_sidebar = "#1f1f21"
            border_sidebar = "#2a2a2c"
            bg_card = "#242426"
            border_card = "#2d2d30"
            text_color = "#f5f6fa"
            text_muted = "#a0a0a2"
            bg_toolbar = "linear-gradient(to bottom, #2d2d30, #1c1c1e)"
            border_toolbar = "#141416"
            bg_selected = "#007AFF"
            text_selected = "#ffffff"
            bg_segmented = "#2d2d30"
            bg_row_hover = "#2d2d30"
            bg_row_selected = "#007AFF"
        else:
            bg_window = "#f1f2f6"
            border_window = "#c8c8cc" # Match light border
            bg_sidebar = "#eef0f4"
            border_sidebar = "#dcdde1"
            bg_card = "#ffffff"
            border_card = "#e3e4e9"
            text_color = "#2c3e50"
            text_muted = "#7f8c8d"
            bg_toolbar = "linear-gradient(to bottom, #f6f6f8, #e5e5e9)"
            border_toolbar = "#c8c8cc"
            bg_selected = "#007AFF"
            text_selected = "#ffffff"
            bg_segmented = "#e3e4e9"
            bg_row_hover = "#f1f2f6"
            bg_row_selected = "#007AFF"

        css_provider = Gtk.CssProvider()
        css = f"""
            window#milofiles-window,
            window#milofiles-window.background,
            window#milofiles-window.ssd,
            window#milofiles-window.ssd.background,
            window#milofiles-window .background,
            window#milofiles-window.backdrop,
            window#milofiles-window.backdrop.background {{
                border-style: none;
                border-width: 0px;
                border-color: transparent;
                box-shadow: none;
                background-color: {bg_window};
            }}
            window#milofiles-window decoration,
            window#milofiles-window decoration:backdrop,
            window#milofiles-window.ssd decoration,
            window#milofiles-window.csd decoration,
            window#milofiles-window.background decoration,
            window#milofiles-window.ssd.background decoration,
            window#milofiles-window.csd.background decoration,
            window#milofiles-window.backdrop decoration,
            window#milofiles-window decoration * {{
                background-color: transparent;
                border-style: none;
                border-width: 0px;
                border-color: transparent;
                box-shadow: none;
                padding: 0px;
                margin: 0px;
            }}
            window#milofiles-window menubar,
            window#milofiles-window menubar * {{
                border-style: none;
                border-width: 0px;
                border-color: transparent;
                box-shadow: none;
                background-color: transparent;
                margin: 0px;
                padding: 0px;
                min-height: 0px;
            }}
            window#milofiles-window scrolledwindow,
            window#milofiles-window viewport,
            window#milofiles-window treeview,
            window#milofiles-window iconview,
            window#milofiles-window frame,
            window#milofiles-window .frame,
            window#milofiles-window viewport.frame,
            window#milofiles-window paned {{
                border-style: none;
                border-width: 0px;
                border-color: transparent;
                box-shadow: none;
                background-color: transparent;
            }}
            window#milofiles-window treeview button,
            window#milofiles-window treeview button:hover,
            window#milofiles-window treeview button:backdrop {{
                background-color: {bg_card};
                background-image: none;
                border-style: solid;
                border-width: 0px 1px 1px 0px;
                border-color: {border_card};
                color: {text_color};
                font-weight: bold;
                text-shadow: none;
                box-shadow: none;
                padding: 6px;
            }}
            window#milofiles-window paned > separator {{
                background-color: {border_window};
                border-style: none;
                border-width: 0px;
                background-image: none;
            }}
            window#milofiles-window .main-toolbar {{
                background-image: {bg_toolbar};
                border-style: solid;
                border-width: 0px 0px 1px 0px;
                border-color: {border_toolbar};
                padding: 6px 12px;
            }}
            window#milofiles-window .sidebar-scroll {{
                background-color: {bg_sidebar};
                border-style: solid;
                border-width: 0px 1px 0px 0px;
                border-color: {border_sidebar};
                box-shadow: none;
            }}
            window#milofiles-window .sidebar-list {{
                background-color: transparent;
            }}
            window#milofiles-window .sidebar-row {{
                background-color: transparent;
                padding: 4px 8px;
                border-radius: 6px;
                color: {text_color};
            }}
            window#milofiles-window .sidebar-row:hover {{
                background-color: {bg_row_hover};
            }}
            window#milofiles-window .sidebar-row:selected {{
                background-color: {bg_row_selected};
                color: {text_selected};
            }}
            window#milofiles-window .sidebar-label {{
                font-size: 13px;
                font-weight: 500;
            }}
            window#milofiles-window .sidebar-eject-btn {{
                background: transparent;
                border-style: none;
                box-shadow: none;
                padding: 0;
                margin: 0;
            }}
            window#milofiles-window .sidebar-eject-btn:hover {{
                color: #ff3b30;
            }}
            window#milofiles-window .file-card {{
                background-color: {bg_card};
                border-radius: 12px;
                border-style: solid;
                border-width: 1px;
                border-color: {border_card};
                margin: 12px;
                box-shadow: none;
            }}
            window#milofiles-window .file-view {{
                background-color: transparent;
                color: {text_color};
            }}
            window#milofiles-window .file-view:selected {{
                background-color: {bg_selected};
                color: {text_selected};
            }}
            window#milofiles-window .path-scroll {{
                background-color: {bg_card};
                border-radius: 6px;
                border-style: solid;
                border-width: 1px;
                border-color: {border_card};
                padding: 2px 8px;
            }}
            window#milofiles-window .path-entry {{
                background-color: {bg_card};
                border-radius: 6px;
                border-style: solid;
                border-width: 1px;
                border-color: {border_card};
                padding: 4px 8px;
                color: {text_color};
            }}
            window#milofiles-window .path-btn {{
                background: transparent;
                border-style: none;
                box-shadow: none;
                color: {text_color};
                font-size: 12px;
                padding: 2px 4px;
            }}
            window#milofiles-window .path-btn:hover {{
                color: #007AFF;
                background-color: {bg_row_hover};
                border-radius: 4px;
            }}
            window#milofiles-window .path-separator {{
                color: {text_muted};
                font-size: 10px;
            }}
            window#milofiles-window .segmented-control {{
                background-color: {bg_segmented};
                border-radius: 6px;
                padding: 2px;
            }}
            window#milofiles-window .segmented-btn {{
                background-color: transparent;
                border-style: none;
                box-shadow: none;
                color: {text_color};
                padding: 4px 8px;
                border-radius: 4px;
            }}
            window#milofiles-window .segmented-btn:checked {{
                background-color: {bg_card};
                color: {text_color};
                box-shadow: 0 1px 3px rgba(0,0,0,0.12), 0 1px 2px rgba(0,0,0,0.08);
            }}
            window#milofiles-window .search-entry {{
                border-radius: 6px;
                padding: 4px 8px;
                font-size: 13px;
            }}
            window#milofiles-window .statusbar {{
                background-color: {bg_sidebar};
                border-style: solid;
                border-width: 1px 0px 0px 0px;
                border-color: {border_sidebar};
                padding: 4px 12px;
                font-size: 11px;
                color: {text_muted};
            }}
        """
        css_provider.load_from_data(css.encode('utf-8'))
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            css_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )

    def load_favorites(self):
        bookmarks_file = os.path.expanduser("~/.config/gtk-3.0/bookmarks")
        favorites = []
        if not os.path.exists(bookmarks_file) or os.path.getsize(bookmarks_file) == 0:
            def get_special(special_dir_enum):
                return GLib.get_user_special_dir(special_dir_enum)
                
            defaults = [
                (GLib.UserDirectory.DIRECTORY_DESKTOP, "user-desktop"),
                (GLib.UserDirectory.DIRECTORY_DOCUMENTS, "folder-documents"),
                (GLib.UserDirectory.DIRECTORY_DOWNLOAD, "folder-download"),
                (GLib.UserDirectory.DIRECTORY_MUSIC, "folder-music"),
                (GLib.UserDirectory.DIRECTORY_PICTURES, "folder-pictures"),
                (GLib.UserDirectory.DIRECTORY_VIDEOS, "folder-videos")
            ]
            for special_enum, icon_name in defaults:
                path = get_special(special_enum)
                if path and os.path.isdir(path):
                    favorites.append((os.path.basename(path), path, icon_name, None))
            self.save_favorites_to_disk(favorites)
        else:
            try:
                with open(bookmarks_file, "r") as f:
                    lines = f.readlines()
                for line in lines:
                    line = line.strip()
                    if not line:
                        continue
                    parts = line.split(" ", 1)
                    uri = parts[0]
                    label = parts[1] if len(parts) > 1 else None
                    
                    gfile = Gio.File.new_for_uri(uri)
                    path = gfile.get_path()
                    if path and os.path.isdir(path):
                        icon_name = "folder"
                        basename = os.path.basename(path).lower()
                        if basename == "desktop":
                            icon_name = "user-desktop"
                        elif basename == "documents":
                            icon_name = "folder-documents"
                        elif basename == "downloads":
                            icon_name = "folder-download"
                        elif basename == "music":
                            icon_name = "folder-music"
                        elif basename == "pictures":
                            icon_name = "folder-pictures"
                        elif basename == "videos":
                            icon_name = "folder-videos"
                            
                        name = label if label else os.path.basename(path)
                        favorites.append((name, path, icon_name, None))
            except Exception:
                pass
        return favorites

    def save_favorites_to_disk(self, favorites):
        bookmarks_file = os.path.expanduser("~/.config/gtk-3.0/bookmarks")
        try:
            os.makedirs(os.path.dirname(bookmarks_file), exist_ok=True)
            with open(bookmarks_file, "w") as f:
                for name, path, unused_icon, unused_vol in favorites:
                    if name == _("trash") or path == os.path.expanduser('~/.local/share/Trash/files'):
                        continue
                    gfile = Gio.File.new_for_path(path)
                    uri = gfile.get_uri()
                    if name != os.path.basename(path):
                        f.write(f"{uri} {name}\n")
                    else:
                        f.write(f"{uri}\n")
        except Exception:
            pass

    def reload_sidebar(self):
        for child in self.sidebar_box.get_children():
            self.sidebar_box.remove(child)
        self.sidebar_lists = []
        self.setup_sidebar()
        self.sidebar_box.show_all()

    def setup_sidebar(self):
        # Create a size group for icons to align item labels perfectly
        self.sidebar_icon_group = Gtk.SizeGroup(mode=Gtk.SizeGroupMode.HORIZONTAL)
        
        # DEVICES section
        devices = []
        # 1. User Home Folder
        devices.append((GLib.get_user_name().capitalize(), os.path.expanduser('~'), "user-home", None))
        # 2. Root Filesystem
        devices.append((_("file_system"), "/", "drive-harddisk", None))
        
        # 3. Connected Volumes/Disks (mounted and unmounted)
        try:
            volumes = self.volume_monitor.get_volumes()
            for volume in volumes:
                name = volume.get_name()
                if not name:
                    continue
                icon = volume.get_icon()
                icon_name = "drive-harddisk"
                if icon:
                    icon_str = icon.to_string()
                    if icon_str:
                        icon_name = icon_str.split()[-1]
                
                mount = volume.get_mount()
                if mount:
                    path = mount.get_default_location().get_path()
                    if path and path not in [os.path.expanduser('~'), "/"]:
                        devices.append((name, path, icon_name, None))
                else:
                    devices.append((name, None, icon_name, volume))
        except Exception:
            pass
            
        # 4. Other custom mounts (network shares, remote locations) without volumes
        try:
            mounts = self.volume_monitor.get_mounts()
            for mount in mounts:
                if mount.get_volume():
                    continue
                loc = mount.get_default_location()
                path = loc.get_path()
                # GVfs virtual mounts return None for get_path(); resolve via root
                if not path:
                    root = mount.get_root()
                    path = root.get_path()
                if path and path not in [os.path.expanduser('~'), "/"]:
                    name = mount.get_name()
                    icon = mount.get_icon()
                    icon_name = "folder-remote"
                    if icon:
                        icon_str = icon.to_string()
                        if icon_str:
                            icon_name = icon_str.split()[-1]
                    devices.append((name, path, icon_name, None))
        except Exception:
            pass
            
        self.add_sidebar_section(_("devices"), devices)
        
        # FAVORITES section
        favorites = self.load_favorites()
        
        # Add Trash dynamically to Favorites
        favorites.append((_("trash"), os.path.expanduser('~/.local/share/Trash/files'), "user-trash", None))
        
        self.add_sidebar_section(_("favorites"), favorites)

    def add_sidebar_section(self, title, items):
        # Header title
        lbl = Gtk.Label()
        lbl.set_markup(f"<span size='8500' weight='bold' color='#7f8c8d'>{title}</span>")
        lbl.set_halign(Gtk.Align.START)
        lbl.set_margin_start(16)
        lbl.set_margin_top(8)
        lbl.set_margin_bottom(2)
        self.sidebar_box.pack_start(lbl, False, False, 0)
        
        # Section listbox
        listbox = Gtk.ListBox()
        listbox.get_style_context().add_class("sidebar-list")
        listbox.set_selection_mode(Gtk.SelectionMode.SINGLE)
        
        for name, path, icon_name, volume in items:
            # Only display directories that actually exist or are unmounted volumes
            if path is not None:
                if not os.path.isdir(path) and name != _("trash"):
                    continue
                
            row = Gtk.ListBoxRow()
            row.get_style_context().add_class("sidebar-row")
            row.path = path
            row.volume = volume
            
            box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
            box.set_margin_start(12)
            box.set_margin_end(12)
            box.set_margin_top(4)
            box.set_margin_bottom(4)
            
            img = Gtk.Image.new_from_icon_name(icon_name, Gtk.IconSize.MENU)
            img.get_style_context().add_class("sidebar-icon")
            self.sidebar_icon_group.add_widget(img)
            box.pack_start(img, False, False, 0)
            
            name_lbl = Gtk.Label(label=name)
            name_lbl.set_halign(Gtk.Align.START)
            name_lbl.set_valign(Gtk.Align.CENTER)
            name_lbl.get_style_context().add_class("sidebar-label")
            box.pack_start(name_lbl, True, True, 0)
            
            # If it's a dynamic mount (not Home, Root or Trash), add an eject button on the right
            if title == _("devices") and path is not None and path not in [os.path.expanduser('~'), "/"] and name != _("trash"):
                eject_btn = Gtk.Button()
                eject_btn.set_relief(Gtk.ReliefStyle.NONE)
                eject_btn.set_image(Gtk.Image.new_from_icon_name("media-eject-symbolic", Gtk.IconSize.MENU))
                eject_btn.get_style_context().add_class("sidebar-eject-btn")
                eject_btn.connect("clicked", self.on_eject_clicked, path)
                box.pack_end(eject_btn, False, False, 0)
                
            row.add(box)
            listbox.add(row)
            
        listbox.connect("row-activated", self.on_sidebar_row_activated)
        listbox.connect("button-press-event", self.on_sidebar_button_press, title)
        self.sidebar_box.pack_start(listbox, False, False, 0)
        self.sidebar_lists.append(listbox)

    def on_sidebar_row_activated(self, listbox, row):
        if row:
            if hasattr(row, 'path') and row.path:
                self.load_directory(row.path)
            elif hasattr(row, 'volume') and row.volume:
                self.mount_and_navigate_volume(row.volume)

    def on_sidebar_button_press(self, listbox, event, section_title):
        if event.button == 3: # Right click
            row = listbox.get_row_at_y(event.y)
            if row:
                listbox.select_row(row)
                self.show_sidebar_context_menu(row, event, section_title)
                return True
        return False

    def show_sidebar_context_menu(self, row, event, section_title):
        menu = Gtk.Menu()
        
        name = ""
        box = row.get_child()
        if box:
            for child in box.get_children():
                if isinstance(child, Gtk.Label):
                    name = child.get_text()
                    break
        
        path = row.path
        is_trash = (name == _("trash") or path == os.path.expanduser('~/.local/share/Trash/files'))
        
        if is_trash:
            item_empty = Gtk.MenuItem(label=_("empty_trash"))
            item_empty.connect("activate", lambda w: self.on_empty_trash())
            menu.append(item_empty)
        elif section_title == _("favorites"):
            item_rename = Gtk.MenuItem(label=_("rename_favorite"))
            item_rename.connect("activate", lambda w: self.on_rename_favorite(row, name, path))
            menu.append(item_rename)
            
            item_remove = Gtk.MenuItem(label=_("remove_favorite"))
            item_remove.connect("activate", lambda w: self.on_remove_favorite(path))
            menu.append(item_remove)
        elif section_title == _("devices"):
            if hasattr(row, 'volume') and row.volume is not None and (not hasattr(row, 'path') or row.path is None):
                item_mount = Gtk.MenuItem(label=_("mount_volume"))
                item_mount.connect("activate", lambda w: self.mount_and_navigate_volume(row.volume))
                menu.append(item_mount)
            elif hasattr(row, 'path') and row.path is not None and row.path not in [os.path.expanduser('~'), "/"]:
                item_unmount = Gtk.MenuItem(label=_("unmount_volume"))
                item_unmount.connect("activate", lambda w: self.on_eject_clicked(None, row.path))
                menu.append(item_unmount)
            else:
                return
            
        menu.show_all()
        menu.popup(None, None, None, None, event.button, event.time)

    def on_empty_trash(self):
        dialog = Gtk.MessageDialog(
            transient_for=self,
            flags=0,
            message_type=Gtk.MessageType.QUESTION,
            buttons=Gtk.ButtonsType.YES_NO,
            text=_("confirm_empty_trash")
        )
        response = dialog.run()
        dialog.destroy()
        if response == Gtk.ResponseType.YES:
            try:
                trash_dir = os.path.expanduser('~/.local/share/Trash')
                files_dir = os.path.join(trash_dir, 'files')
                info_dir = os.path.join(trash_dir, 'info')
                for d in [files_dir, info_dir]:
                    if os.path.exists(d):
                        for item in os.listdir(d):
                            path = os.path.join(d, item)
                            try:
                                if os.path.isdir(path) and not os.path.islink(path):
                                    shutil.rmtree(path)
                                else:
                                    os.remove(path)
                            except Exception:
                                pass
                if self.current_dir == files_dir:
                    self.load_directory(self.current_dir)
            except Exception as e:
                self.show_error_dialog(_("trash"), str(e))

    def on_rename_favorite(self, row, name, path):
        dialog = Gtk.Dialog(title=_("rename_favorite"), transient_for=self, flags=0)
        dialog.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL, _("apply"), Gtk.ResponseType.OK)
        dialog.set_default_response(Gtk.ResponseType.OK)
        
        box = dialog.get_content_area()
        box.set_spacing(10)
        box.set_border_width(12)
        
        lbl = Gtk.Label(label=_("enter_new_name"))
        box.pack_start(lbl, False, False, 0)
        
        entry = Gtk.Entry()
        entry.set_text(name)
        entry.set_activates_default(True)
        box.pack_start(entry, False, False, 0)
        
        dialog.show_all()
        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            new_name = entry.get_text().strip()
            if new_name and new_name != name:
                favorites = self.load_favorites()
                for i, fav in enumerate(favorites):
                    if fav[1] == path:
                        favorites[i] = (new_name, path, fav[2], fav[3])
                        break
                self.save_favorites_to_disk(favorites)
                self.reload_sidebar()
        dialog.destroy()

    def on_remove_favorite(self, path):
        favorites = self.load_favorites()
        favorites = [f for f in favorites if f[1] != path]
        self.save_favorites_to_disk(favorites)
        self.reload_sidebar()

    def mount_and_navigate_volume(self, volume):
        dev_path = volume.get_identifier('unix-device')
        if not dev_path:
            return
            
        def do_mount():
            res = subprocess.run(["udisksctl", "mount", "-b", dev_path], capture_output=True, text=True)
            if res.returncode == 0:
                out = res.stdout.strip()
                if " at " in out:
                    mount_path = out.split(" at ")[-1].rstrip('.')
                    GLib.idle_add(self.load_directory, mount_path)
                    GLib.idle_add(self.on_mounts_changed, None, None)
            else:
                err_msg = res.stderr or "Could not mount volume"
                GLib.idle_add(self.show_error_dialog, _("mount_error"), err_msg)
                
        t = threading.Thread(target=do_mount)
        t.daemon = True
        t.start()

    def select_sidebar_path(self, path):
        norm = os.path.abspath(os.path.expanduser(path))
        for listbox in self.sidebar_lists:
            # Block activate signal while selecting programmatically
            listbox.handler_block_by_func(self.on_sidebar_row_activated)
            listbox.unselect_all()
            for row in listbox.get_children():
                if hasattr(row, 'path') and row.path and os.path.abspath(os.path.expanduser(row.path)) == norm:
                    listbox.select_row(row)
            listbox.handler_unblock_by_func(self.on_sidebar_row_activated)

    def on_mounts_changed(self, monitor, mount_or_volume):
        self.reload_sidebar()

    def on_eject_clicked(self, button, path):
        def do_unmount():
            try:
                subprocess.run(["gio", "mount", "-u", path], capture_output=True, text=True)
            except Exception as e:
                GLib.idle_add(self.show_error_dialog, _("eject_error"), str(e))
        t = threading.Thread(target=do_unmount)
        t.daemon = True
        t.start()

    def setup_icon_view(self):
        scroll = Gtk.ScrolledWindow()
        scroll.set_shadow_type(Gtk.ShadowType.NONE)
        scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        
        self.icon_view = Gtk.IconView.new_with_model(self.icon_filter)
        self.icon_view.get_style_context().add_class("file-view")
        self.icon_view.set_pixbuf_column(0)
        self.icon_view.set_text_column(1)
        self.icon_view.set_item_width(80)
        self.icon_view.set_column_spacing(10)
        self.icon_view.set_row_spacing(10)
        self.icon_view.set_selection_mode(Gtk.SelectionMode.MULTIPLE)
        
        # Double click activation
        self.icon_view.connect("item-activated", self.on_icon_view_item_activated)
        
        # Right click context menu
        self.icon_view.connect("button-press-event", self.on_icon_view_button_press)
        
        # drag and drop destination
        self.icon_view.drag_dest_set(Gtk.DestDefaults.ALL, [], Gdk.DragAction.COPY | Gdk.DragAction.MOVE)
        self.icon_view.drag_dest_add_uri_targets()
        self.icon_view.connect("drag-data-received", self.on_drag_data_received)
        
        scroll.add(self.icon_view)
        self.view_stack.add_named(scroll, "icon")

    def setup_list_view(self):
        scroll = Gtk.ScrolledWindow()
        scroll.set_shadow_type(Gtk.ShadowType.NONE)
        scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        
        self.tree_view = Gtk.TreeView.new_with_model(self.list_filter)
        self.tree_view.get_style_context().add_class("file-view")
        self.tree_view.get_selection().set_mode(Gtk.SelectionMode.MULTIPLE)
        
        # Create columns
        col_pix = Gtk.TreeViewColumn()
        cell_pix = Gtk.CellRendererPixbuf()
        col_pix.pack_start(cell_pix, False)
        col_pix.add_attribute(cell_pix, "pixbuf", 0)
        
        cell_txt = Gtk.CellRendererText()
        col_pix.pack_start(cell_txt, True)
        col_pix.add_attribute(cell_txt, "text", 1)
        col_pix.set_title(_("name"))
        col_pix.set_resizable(True)
        col_pix.set_expand(True)
        col_pix.set_sort_column_id(1)
        self.tree_view.append_column(col_pix)
        
        col_size = Gtk.TreeViewColumn(_("size"), Gtk.CellRendererText(), text=2)
        col_size.set_resizable(True)
        col_size.set_sort_column_id(2)
        col_size.set_fixed_width(100)
        self.tree_view.append_column(col_size)
        
        col_type = Gtk.TreeViewColumn(_("type"), Gtk.CellRendererText(), text=3)
        col_type.set_resizable(True)
        col_type.set_sort_column_id(3)
        col_type.set_fixed_width(120)
        self.tree_view.append_column(col_type)
        
        col_mod = Gtk.TreeViewColumn(_("modified"), Gtk.CellRendererText(), text=4)
        col_mod.set_resizable(True)
        col_mod.set_sort_column_id(4)
        col_mod.set_fixed_width(150)
        self.tree_view.append_column(col_mod)
        
        self.tree_view.connect("row-activated", self.on_tree_view_row_activated)
        self.tree_view.connect("button-press-event", self.on_tree_view_button_press)
        
        # drag and drop destination
        self.tree_view.drag_dest_set(Gtk.DestDefaults.ALL, [], Gdk.DragAction.COPY | Gdk.DragAction.MOVE)
        self.tree_view.drag_dest_add_uri_targets()
        self.tree_view.connect("drag-data-received", self.on_drag_data_received)
        
        scroll.add(self.tree_view)
        self.view_stack.add_named(scroll, "list")

    def on_view_changed(self, button, view_type):
        if button.get_active():
            self.view_stack.set_visible_child_name(view_type)

    def on_search_changed(self, entry):
        self.icon_filter.refilter()
        self.list_filter.refilter()

    def filter_visible_func(self, model, iter, data):
        # Query search text
        search_txt = self.search_entry.get_text().lower().strip()
        
        # Determine column indexes
        if model == self.icon_store:
            name = model.get_value(iter, 1)
        else:
            name = model.get_value(iter, 1)
            
        if not name:
            return False
            
        if search_txt and search_txt not in name.lower():
            return False
            
        return True

    def on_path_scroll_clicked(self, widget, event):
        self.show_path_entry()
        return True

    def show_path_entry(self):
        self.path_entry.set_text(self.current_dir)
        self.path_stack.set_visible_child_name("entry")
        self.path_entry.grab_focus()
        self.path_entry.select_region(0, -1)

    def show_path_breadcrumbs(self):
        self.path_stack.set_visible_child_name("buttons")

    def on_path_entry_activated(self, entry):
        text = entry.get_text().strip()
        if text:
            has_scheme = "://" in text and not text.startswith("file://")
            if has_scheme:
                self.mount_network_share(text)
            else:
                if text.startswith("file://"):
                    path = text[7:]
                else:
                    path = text
                self.load_directory(path)
        self.show_path_breadcrumbs()

    def on_path_entry_key_press(self, widget, event):
        keyname = Gdk.keyval_name(event.keyval)
        if keyname == "Escape":
            self.show_path_breadcrumbs()
            return True
        return False

    def update_path_recommendations(self):
        self.path_completion_model.clear()
        paths = [
            os.path.expanduser('~'),
            "/",
            GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_DESKTOP),
            GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_DOCUMENTS),
            GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_DOWNLOAD),
            GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_MUSIC),
            GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_PICTURES),
            GLib.get_user_special_dir(GLib.UserDirectory.DIRECTORY_VIDEOS),
            os.path.expanduser('~/.local/share/Trash/files')
        ]
        for p in paths:
            if p and os.path.exists(p):
                self.path_completion_model.append([p])

    def load_directory(self, path, addToHistory=True):
        path = os.path.abspath(os.path.expanduser(path))
        if not os.path.isdir(path):
            # Fallback to Home if path is deleted or invalid
            path = os.path.expanduser('~')
            
        if addToHistory:
            self.history = self.history[:self.history_index + 1]
            self.history.append(path)
            self.history_index = len(self.history) - 1
            
        self.current_dir = path
        
        # Update button states
        self.back_btn.set_sensitive(self.history_index > 0)
        self.forward_btn.set_sensitive(self.history_index < len(self.history) - 1)
        self.up_btn.set_sensitive(path != '/')
        
        try:
            entries = list(os.scandir(path))
        except Exception as e:
            self.show_error_dialog(_("cannot_read_dir"), str(e))
            return
            
        # Filter hidden files
        if not self.show_hidden:
            entries = [e for e in entries if not e.name.startswith('.')]
            
        # Sort directories first, then files alphabetically
        dir_list = []
        file_list = []
        for entry in entries:
            try:
                is_dir = entry.is_dir()
            except Exception:
                is_dir = False
            if is_dir:
                dir_list.append(entry)
            else:
                file_list.append(entry)
        dir_list.sort(key=lambda e: e.name.lower())
        file_list.sort(key=lambda e: e.name.lower())
        sorted_entries = dir_list + file_list
        
        # Clear stores
        self.icon_store.clear()
        self.list_store.clear()
        
        for entry in sorted_entries:
            full = entry.path
            is_dir = entry.is_dir()
            
            # Load metadata
            try:
                stat = entry.stat()
                size = stat.st_size
                mtime = datetime.datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M")
            except Exception:
                size = 0
                mtime = "N/A"
                
            size_str = format_bytes(size) if not is_dir else ""
            type_str = get_file_type_desc(full, is_dir=is_dir)
            
            # Load icons
            icon_pb = get_file_icon(full, size=48, is_dir=is_dir)
            list_icon_pb = get_file_icon(full, size=20, is_dir=is_dir)
            
            self.icon_store.append([icon_pb, entry.name, full, is_dir])
            self.list_store.append([list_icon_pb, entry.name, size_str, type_str, mtime, full, is_dir])
            
        self.update_breadcrumbs()
        self.select_sidebar_path(path)
        self.update_statusbar()
        self.start_thumbnail_loading(path)

    def start_thumbnail_loading(self, dir_path):
        self.current_thumbnail_load_id += 1
        load_id = self.current_thumbnail_load_id
        
        files_to_process = []
        for row in self.icon_store:
            if not row[3]: # not is_dir
                files_to_process.append(row[2])
                
        def worker():
            for file_path in files_to_process:
                if self.current_thumbnail_load_id != load_id:
                    return
                    
                time.sleep(0.01)
                
                ext = os.path.splitext(file_path)[1].lower()
                if ext not in ['.png', '.jpg', '.jpeg', '.gif', '.webp', '.bmp', '.svg']:
                    mime, _ = mimetypes.guess_type(file_path)
                    if not (mime and mime.startswith("image/")):
                        continue
                        
                if file_path in self.thumbnail_cache:
                    icon_pb, list_icon_pb = self.thumbnail_cache[file_path]
                else:
                    icon_pb = None
                    list_icon_pb = None
                    
                    # Try system thumbnail path
                    try:
                        gfile = Gio.File.new_for_path(file_path)
                        info = gfile.query_info("thumbnail::path", Gio.FileQueryInfoFlags.NONE, None)
                        thumb_path = info.get_attribute_as_string("thumbnail::path")
                        if thumb_path and os.path.exists(thumb_path):
                            icon_pb = GdkPixbuf.Pixbuf.new_from_file_at_scale(thumb_path, 48, 48, True)
                            list_icon_pb = GdkPixbuf.Pixbuf.new_from_file_at_scale(thumb_path, 20, 20, True)
                    except Exception:
                        pass
                        
                    # Fallback to local image scaling
                    if not icon_pb:
                        try:
                            icon_pb = GdkPixbuf.Pixbuf.new_from_file_at_scale(file_path, 48, 48, True)
                            list_icon_pb = GdkPixbuf.Pixbuf.new_from_file_at_scale(file_path, 20, 20, True)
                        except Exception:
                            pass
                            
                    if icon_pb and list_icon_pb:
                        if len(self.thumbnail_cache) >= 1000:
                            self.thumbnail_cache.clear()
                        self.thumbnail_cache[file_path] = (icon_pb, list_icon_pb)
                    else:
                        continue
                        
                if self.current_thumbnail_load_id == load_id:
                    GLib.idle_add(self.update_item_thumbnail, file_path, icon_pb, list_icon_pb, load_id)
                    
        t = threading.Thread(target=worker)
        t.daemon = True
        t.start()

    def update_item_thumbnail(self, file_path, icon_pb, list_icon_pb, load_id):
        if self.current_thumbnail_load_id != load_id:
            return False
            
        for row in self.icon_store:
            if row[2] == file_path:
                row[0] = icon_pb
                break
                
        for row in self.list_store:
            if row[5] == file_path:
                row[0] = list_icon_pb
                break
                
        return False

    def update_breadcrumbs(self):
        for child in self.path_box.get_children():
            self.path_box.remove(child)
            
        parts = []
        path = self.current_dir
        while True:
            parent, name = os.path.split(path)
            if name:
                parts.insert(0, (path, name))
            else:
                if parent:
                    parts.insert(0, (parent, parent))
                break
            path = parent
            
        for i, (full, name) in enumerate(parts):
            if i > 0:
                sep = Gtk.Label(label=">")
                sep.get_style_context().add_class("path-separator")
                self.path_box.pack_start(sep, False, False, 2)
                
            btn = Gtk.Button(label=name)
            btn.get_style_context().add_class("path-btn")
            btn.connect("clicked", lambda b, p=full: self.load_directory(p))
            self.path_box.pack_start(btn, False, False, 0)
            
        self.path_box.show_all()

    def update_statusbar(self):
        total_items = len(self.icon_store)
        
        # Disk free space
        try:
            total, used, free = shutil.disk_usage(self.current_dir)
            free_str = format_bytes(free)
            self.status_lbl.set_text(f"{total_items} {_('items')}  |  {free_str} {_('free')}")
        except Exception:
            self.status_lbl.set_text(f"{total_items} {_('items')}")

    def on_back_clicked(self, button):
        if self.history_index > 0:
            self.history_index -= 1
            self.load_directory(self.history[self.history_index], addToHistory=False)

    def on_forward_clicked(self, button):
        if self.history_index < len(self.history) - 1:
            self.history_index += 1
            self.load_directory(self.history[self.history_index], addToHistory=False)

    def on_up_clicked(self, button):
        parent = os.path.dirname(self.current_dir)
        if parent != self.current_dir:
            self.load_directory(parent)

    def on_icon_view_item_activated(self, view, path):
        model = view.get_model()
        child_iter = model.convert_path_to_child_path(path)
        iter = self.icon_store.get_iter(child_iter)
        full_path = self.icon_store.get_value(iter, 2)
        is_dir = self.icon_store.get_value(iter, 3)
        
        if is_dir:
            self.load_directory(full_path)
        else:
            self.open_file(full_path)

    def on_tree_view_row_activated(self, treeview, path, col):
        model = treeview.get_model()
        child_path = model.convert_path_to_child_path(path)
        iter = self.list_store.get_iter(child_path)
        full_path = self.list_store.get_value(iter, 5)
        is_dir = self.list_store.get_value(iter, 6)
        
        if is_dir:
            self.load_directory(full_path)
        else:
            self.open_file(full_path)

    def open_file(self, path):
        try:
            cmd = get_custom_default_command(path)
            if cmd:
                subprocess.Popen(cmd + [path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return
            gfile = Gio.File.new_for_path(path)
            info = gfile.query_default_handler(None)
            if info:
                info.launch([gfile], None)
            else:
                subprocess.Popen(["xdg-open", path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception:
            try:
                subprocess.Popen(["xdg-open", path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception as e:
                self.show_error_dialog(_("open_error"), str(e))

    def on_icon_view_button_press(self, view, event):
        if event.button == 3: # Right click
            pos = view.get_path_at_pos(event.x, event.y)
            if pos:
                if not view.path_is_selected(pos):
                    view.unselect_all()
                    view.select_path(pos)
                self.show_context_menu(event, is_item=True)
            else:
                view.unselect_all()
                self.show_context_menu(event, is_item=False)
            return True
        return False

    def on_tree_view_button_press(self, view, event):
        if event.button == 3: # Right click
            pos = view.get_path_at_pos(event.x, event.y)
            if pos:
                path, col, cx, cy = pos
                sel = view.get_selection()
                if not sel.path_is_selected(path):
                    sel.unselect_all()
                    sel.select_path(path)
                self.show_context_menu(event, is_item=True)
            else:
                view.get_selection().unselect_all()
                self.show_context_menu(event, is_item=False)
            return True
        return False

    def get_selected_paths(self):
        # Returns list of selected file paths
        paths = []
        is_icon = (self.view_stack.get_visible_child_name() == "icon")
        if is_icon:
            selected = self.icon_view.get_selected_items()
            for path in selected:
                child_path = self.icon_filter.convert_path_to_child_path(path)
                iter = self.icon_store.get_iter(child_path)
                paths.append(self.icon_store.get_value(iter, 2))
        else:
            model, selected = self.tree_view.get_selection().get_selected_rows()
            for path in selected:
                child_path = self.list_filter.convert_path_to_child_path(path)
                iter = self.list_store.get_iter(child_path)
                paths.append(self.list_store.get_value(iter, 5))
        return paths

    def show_context_menu(self, event, is_item):
        menu = Gtk.Menu()
        
        if is_item:
            item_open = Gtk.MenuItem(label=_("open"))
            item_open.connect("activate", self.on_menu_open)
            menu.append(item_open)
            
            selected_paths = self.get_selected_paths()
            if len(selected_paths) == 1:
                path = selected_paths[0]
                if not os.path.isdir(path):
                    item_open_with = Gtk.MenuItem(label=_("open_with"))
                    open_with_menu = Gtk.Menu()
                    item_open_with.set_submenu(open_with_menu)
                    
                    gfile = Gio.File.new_for_path(path)
                    try:
                        info = gfile.query_info("standard::content-type", Gio.FileQueryInfoFlags.NONE, None)
                        content_type = info.get_content_type()
                        if content_type:
                            apps = Gio.AppInfo.get_all_for_type(content_type)
                            seen_names = set()
                            for app in apps:
                                name = app.get_name()
                                if name in seen_names:
                                    continue
                                seen_names.add(name)
                                
                                app_item = Gtk.MenuItem(label=name)
                                app_item.connect("activate", lambda w, a=app, f=gfile: a.launch([f], None))
                                open_with_menu.append(app_item)
                    except Exception:
                        pass
                        
                    if open_with_menu.get_children():
                        open_with_menu.append(Gtk.SeparatorMenuItem())
                        
                    other_app_item = Gtk.MenuItem(label=_("other_application"))
                    other_app_item.connect("activate", self.on_other_application, path)
                    open_with_menu.append(other_app_item)
                    
                    menu.append(item_open_with)
            
            menu.append(Gtk.SeparatorMenuItem())
            
            item_cut = Gtk.MenuItem(label=_("cut"))
            item_cut.connect("activate", self.on_menu_cut)
            menu.append(item_cut)
            
            item_copy = Gtk.MenuItem(label=_("copy"))
            item_copy.connect("activate", self.on_menu_copy)
            menu.append(item_copy)
            
        item_paste = Gtk.MenuItem(label=_("paste"))
        item_paste.set_sensitive(len(self.clipboard_files) > 0)
        item_paste.connect("activate", self.on_menu_paste)
        menu.append(item_paste)
        
        if is_item:
            menu.append(Gtk.SeparatorMenuItem())
            
            item_rename = Gtk.MenuItem(label=_("rename"))
            item_rename.connect("activate", self.on_menu_rename)
            menu.append(item_rename)
            
            item_trash = Gtk.MenuItem(label=_("trash_action"))
            item_trash.connect("activate", self.on_menu_trash)
            menu.append(item_trash)
            
            item_delete = Gtk.MenuItem(label=_("delete"))
            item_delete.connect("activate", self.on_menu_delete)
            menu.append(item_delete)
            
            # Compress and Extract context actions
            menu.append(Gtk.SeparatorMenuItem())
            item_compress = Gtk.MenuItem(label=_("compress"))
            item_compress.connect("activate", self.on_menu_compress)
            menu.append(item_compress)
            
            selected_paths = self.get_selected_paths()
            if len(selected_paths) == 1 and selected_paths[0].lower().endswith(('.zip', '.tar', '.gz', '.tgz', '.bz2', '.tbz2', '.xz', '.txz', '.7z', '.rar')):
                item_extract = Gtk.MenuItem(label=_("extract_here"))
                item_extract.connect("activate", self.on_menu_extract)
                menu.append(item_extract)
                
            if len(selected_paths) == 1 and os.path.isdir(selected_paths[0]):
                item_fav = Gtk.MenuItem(label=_("add_to_favorites"))
                item_fav.connect("activate", self.on_menu_add_to_favorites, selected_paths[0])
                menu.append(item_fav)
            
            menu.append(Gtk.SeparatorMenuItem())
            
            item_props = Gtk.MenuItem(label=_("properties"))
            item_props.connect("activate", self.on_menu_properties)
            menu.append(item_props)
        else:
            menu.append(Gtk.SeparatorMenuItem())
            
            item_new_folder = Gtk.MenuItem(label=_("new_folder"))
            item_new_folder.connect("activate", self.on_menu_new_folder)
            menu.append(item_new_folder)
            
            item_new_file = Gtk.MenuItem(label=_("new_file"))
            item_new_file.connect("activate", self.on_menu_new_file)
            menu.append(item_new_file)
            
            menu.append(Gtk.SeparatorMenuItem())
            
            item_terminal = Gtk.MenuItem(label=_("open_terminal"))
            item_terminal.connect("activate", self.on_menu_open_terminal)
            menu.append(item_terminal)
            
        menu.show_all()
        menu.popup_at_pointer(event)

    def on_menu_open(self, widget):
        paths = self.get_selected_paths()
        for path in paths:
            self.open_file(path)

    def on_menu_open_terminal(self, widget):
        try:
            subprocess.Popen(["xfce4-terminal", f"--working-directory={self.current_dir}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception:
            try:
                subprocess.Popen(["x-terminal-emulator"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception as e:
                self.show_error_dialog(_("open_error"), str(e))

    def on_menu_cut(self, widget):
        self.clipboard_files = self.get_selected_paths()
        self.clipboard_action = 'cut'

    def on_menu_copy(self, widget):
        self.clipboard_files = self.get_selected_paths()
        self.clipboard_action = 'copy'

    def cancel_operation(self):
        self.cancelled = True
        if hasattr(self, "current_proc") and self.current_proc:
            try:
                self.current_proc.terminate()
            except Exception:
                pass

    def start_paste_operation(self, src_paths, dest_dir, action):
        if not src_paths:
            return
        self.cancelled = False
        self.progress_dialog = OperationProgressDialog(
            self,
            _("paste"),
            _("pasting"),
            cancel_callback=self.cancel_operation
        )
        t = threading.Thread(target=self.perform_paste, args=(src_paths, dest_dir, action))
        t.daemon = True
        t.start()

    def perform_paste(self, src_paths, dest_dir, action):
        try:
            for src in src_paths:
                if self.cancelled:
                    break
                if not os.path.exists(src):
                    continue
                name = os.path.basename(src)
                
                # Update progress message dynamically on UI thread
                GLib.idle_add(self.progress_dialog.update_message, f"{_('pasting')}\n{name}")
                
                if dest_dir.startswith(src):
                    continue
                if action == 'cut' and os.path.dirname(src) == dest_dir:
                    continue
                    
                dest = os.path.join(dest_dir, name)
                if os.path.exists(dest):
                    base, ext = os.path.splitext(name)
                    copy_name = f"{base} copy{ext}"
                    dest = os.path.join(dest_dir, copy_name)
                    counter = 2
                    while os.path.exists(dest):
                        copy_name = f"{base} copy {counter}{ext}"
                        dest = os.path.join(dest_dir, copy_name)
                        counter += 1
                        
                if action == 'copy':
                    if os.path.isdir(src):
                        shutil.copytree(src, dest)
                    else:
                        shutil.copy2(src, dest)
                elif action == 'cut':
                    shutil.move(src, dest)
                    
            GLib.idle_add(self.load_directory, self.current_dir)
            if action == 'cut' and not self.cancelled:
                self.clipboard_files = []
        except Exception as e:
            if not self.cancelled:
                GLib.idle_add(self.show_error_dialog, _("paste_error"), str(e))
        finally:
            GLib.idle_add(self.progress_dialog.close_dialog)

    def on_menu_paste(self, widget):
        self.start_paste_operation(self.clipboard_files, self.current_dir, self.clipboard_action)

    def on_other_application(self, widget, path):
        try:
            gfile = Gio.File.new_for_path(path)
            dialog = Gtk.AppChooserDialog.new(self, Gtk.DialogFlags.MODAL, gfile)
            response = dialog.run()
            if response == Gtk.ResponseType.OK:
                app_info = dialog.get_app_info()
                if app_info:
                    app_info.launch([gfile], None)
            dialog.destroy()
        except Exception as e:
            self.show_error_dialog(_("open_error"), str(e))

    def on_menu_connect_server(self, widget):
        dialog = Gtk.Dialog(title=_("connect_to_server"), transient_for=self, flags=0)
        dialog.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL, _("connect"), Gtk.ResponseType.OK)
        dialog.set_default_response(Gtk.ResponseType.OK)
        
        box = dialog.get_content_area()
        box.set_spacing(10)
        box.set_border_width(12)
        
        lbl = Gtk.Label(label=_("enter_server_address"))
        box.pack_start(lbl, False, False, 0)
        
        entry = Gtk.Entry()
        entry.set_placeholder_text(_("connect_placeholder"))
        entry.set_activates_default(True)
        box.pack_start(entry, False, False, 0)
        
        dialog.show_all()
        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            uri = entry.get_text().strip()
            if uri:
                self.mount_network_share(uri)
        dialog.destroy()

    def mount_network_share(self, uri):
        gfile = Gio.File.new_for_uri(uri)
        mount_op = Gtk.MountOperation.new(self)
        mount_op.set_anonymous(False)

        def on_mount_done(source, result):
            try:
                source.mount_enclosing_volume_finish(result)
            except GLib.Error as e:
                # G_IO_ERROR_ALREADY_MOUNTED is OK — share is already available
                if e.code != Gio.IOErrorEnum.ALREADY_MOUNTED:
                    self.show_error_dialog(_("connection_error"), e.message)
                    return

            # Refresh sidebar so the new mount appears in DEVICES
            self.on_mounts_changed(None, None)

            # Resolve the local GVfs path for the mounted share
            try:
                mount = gfile.find_enclosing_mount(None)
                if mount:
                    root = mount.get_root()
                    local_path = root.get_path()
                    if local_path and os.path.exists(local_path):
                        self.load_directory(local_path)
                        return
            except Exception:
                pass
            # Fallback: browse the GVfs mount point directly
            gvfs_base = os.path.join("/run/user", str(os.getuid()), "gvfs")
            if os.path.isdir(gvfs_base):
                for entry in sorted(os.listdir(gvfs_base), reverse=True):
                    candidate = os.path.join(gvfs_base, entry)
                    if os.path.isdir(candidate):
                        self.load_directory(candidate)
                        return

        gfile.mount_enclosing_volume(
            Gio.MountMountFlags.NONE,
            mount_op,
            None,
            on_mount_done
        )

    def on_menu_compress(self, widget):
        paths = self.get_selected_paths()
        if not paths:
            return
            
        dialog = Gtk.Dialog(title=_("compress"), transient_for=self, flags=0)
        dialog.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL, _("apply"), Gtk.ResponseType.OK)
        dialog.set_default_response(Gtk.ResponseType.OK)
        
        box = dialog.get_content_area()
        box.set_spacing(10)
        box.set_border_width(12)
        
        lbl_name = Gtk.Label(label=_("archive_name"))
        box.pack_start(lbl_name, False, False, 0)
        
        entry_name = Gtk.Entry()
        first_name = os.path.basename(paths[0])
        entry_name.set_text(first_name + ".zip")
        entry_name.set_activates_default(True)
        box.pack_start(entry_name, False, False, 0)
        
        lbl_fmt = Gtk.Label(label=_("format"))
        box.pack_start(lbl_fmt, False, False, 0)
        
        combo_fmt = Gtk.ComboBoxText()
        combo_fmt.append("zip", "ZIP (.zip)")
        combo_fmt.append("7z", "7-Zip (.7z)")
        combo_fmt.append("tar.gz", "Tarball GZIP (.tar.gz)")
        combo_fmt.append("tar.xz", "Tarball XZ (.tar.xz)")
        combo_fmt.append("tar.bz2", "Tarball BZIP2 (.tar.bz2)")
        combo_fmt.set_active(0)
        box.pack_start(combo_fmt, False, False, 0)
        
        def on_fmt_changed(combo):
            fmt_id = combo.get_active_id()
            current_text = entry_name.get_text().strip()
            if current_text:
                for ext in [".tar.gz", ".tar.xz", ".tar.bz2", ".zip", ".7z"]:
                    if current_text.lower().endswith(ext):
                        current_text = current_text[:-len(ext)]
                        break
                entry_name.set_text(current_text + f".{fmt_id}")

        combo_fmt.connect("changed", on_fmt_changed)
        
        dialog.show_all()
        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            archive_name = entry_name.get_text().strip()
            fmt = combo_fmt.get_active_id()
            if archive_name:
                self.cancelled = False
                self.current_proc = None
                self.progress_dialog = OperationProgressDialog(
                    self,
                    _("compress"),
                    _("compressing"),
                    cancel_callback=self.cancel_operation
                )
                t = threading.Thread(target=self.do_compress, args=(paths, archive_name, fmt))
                t.daemon = True
                t.start()
        dialog.destroy()

    def do_compress(self, src_paths, archive_name, format):
        dest_path = os.path.join(self.current_dir, archive_name)
        try:
            if format == "zip":
                cmd = ["7z", "a", "-tzip", dest_path] + src_paths
            elif format == "7z":
                cmd = ["7z", "a", "-t7z", dest_path] + src_paths
            elif format in ["tar.gz", "tar.xz", "tar.bz2"]:
                rel_paths = [os.path.relpath(p, self.current_dir) for p in src_paths]
                cmd = ["tar", "-caf", dest_path] + rel_paths
                
            proc = subprocess.Popen(cmd, cwd=self.current_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.current_proc = proc
            proc.wait()
            
            if proc.returncode != 0 and not self.cancelled:
                raise subprocess.CalledProcessError(proc.returncode, cmd)
                
            GLib.idle_add(self.load_directory, self.current_dir)
        except Exception as e:
            if not self.cancelled:
                GLib.idle_add(self.show_error_dialog, _("compress_error"), str(e))
        finally:
            self.current_proc = None
            GLib.idle_add(self.progress_dialog.close_dialog)

    def on_menu_extract(self, widget):
        paths = self.get_selected_paths()
        if len(paths) != 1:
            return
        archive_path = paths[0]
        
        self.cancelled = False
        self.current_proc = None
        self.progress_dialog = OperationProgressDialog(
            self,
            _("extract_here"),
            _("extracting"),
            cancel_callback=self.cancel_operation
        )
        t = threading.Thread(target=self.do_extract, args=(archive_path, self.current_dir))
        t.daemon = True
        t.start()

    def do_extract(self, archive_path, dest_dir):
        try:
            abs_dest_dir = os.path.abspath(dest_dir)
            archive_lower = archive_path.lower()
            if archive_lower.endswith(('.tar', '.tar.gz', '.tgz', '.tar.bz2', '.tbz2', '.tar.xz', '.txz')):
                cmd = ["tar", "-xf", archive_path, "-C", abs_dest_dir]
            else:
                cmd = ["7z", "x", "-y", archive_path, f"-o{abs_dest_dir}"]
                
            proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.current_proc = proc
            proc.wait()
            
            if proc.returncode != 0 and not self.cancelled:
                raise subprocess.CalledProcessError(proc.returncode, cmd)
                
            GLib.idle_add(self.load_directory, self.current_dir)
        except Exception as e:
            if not self.cancelled:
                GLib.idle_add(self.show_error_dialog, _("extract_error"), str(e))
        finally:
            self.current_proc = None
            GLib.idle_add(self.progress_dialog.close_dialog)

    def on_menu_rename(self, widget):
        paths = self.get_selected_paths()
        if not paths:
            return
        old_path = paths[0]
        old_name = os.path.basename(old_path)
        
        dialog = Gtk.Dialog(title=_("rename"), transient_for=self, flags=0)
        dialog.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL, Gtk.STOCK_OK, Gtk.ResponseType.OK)
        dialog.set_default_response(Gtk.ResponseType.OK)
        
        box = dialog.get_content_area()
        box.set_spacing(10)
        box.set_border_width(12)
        
        lbl = Gtk.Label(label=_("enter_new_name"))
        box.pack_start(lbl, False, False, 0)
        
        entry = Gtk.Entry()
        entry.set_text(old_name)
        entry.set_activates_default(True)
        box.pack_start(entry, False, False, 0)
        
        dialog.show_all()
        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            new_name = entry.get_text().strip()
            if new_name and new_name != old_name:
                new_path = os.path.join(os.path.dirname(old_path), new_name)
                try:
                    os.rename(old_path, new_path)
                    self.load_directory(self.current_dir)
                except Exception as e:
                    self.show_error_dialog(_("rename"), str(e))
        dialog.destroy()

    def on_menu_trash(self, widget):
        paths = self.get_selected_paths()
        for path in paths:
            try:
                gfile = Gio.File.new_for_path(path)
                gfile.trash(None)
            except Exception as e:
                self.show_error_dialog(_("trash"), str(e))
        self.load_directory(self.current_dir)

    def on_menu_delete(self, widget):
        paths = self.get_selected_paths()
        if not paths:
            return
            
        dialog = Gtk.MessageDialog(
            transient_for=self,
            flags=0,
            message_type=Gtk.MessageType.WARNING,
            buttons=Gtk.ButtonsType.YES_NO,
            text=_("delete_title")
        )
        dialog.format_secondary_text(_("confirm_delete"))
        response = dialog.run()
        if response == Gtk.ResponseType.YES:
            for path in paths:
                try:
                    if os.path.isdir(path):
                        shutil.rmtree(path)
                    else:
                        os.remove(path)
                except Exception as e:
                    self.show_error_dialog(_("delete_title"), str(e))
            self.load_directory(self.current_dir)
        dialog.destroy()

    def on_menu_new_folder(self, widget):
        base_name = _("new_folder_name")
        name = base_name
        counter = 1
        while os.path.exists(os.path.join(self.current_dir, name)):
            counter += 1
            name = f"{base_name} {counter}"
            
        full_path = os.path.join(self.current_dir, name)
        try:
            os.makedirs(full_path)
            self.load_directory(self.current_dir)
        except Exception as e:
            self.show_error_dialog(_("cannot_create_folder"), str(e))

    def on_menu_new_file(self, widget):
        base_name = _("new_file_name")
        name = base_name
        counter = 1
        while os.path.exists(os.path.join(self.current_dir, name)):
            counter += 1
            name = f"{base_name} {counter}"
            
        full_path = os.path.join(self.current_dir, name)
        try:
            with open(full_path, "w") as f:
                f.write("")
            self.load_directory(self.current_dir)
        except Exception as e:
            self.show_error_dialog(_("new_file"), str(e))

    def on_menu_add_to_favorites(self, widget, path):
        favorites = self.load_favorites()
        norm_path = os.path.normpath(os.path.abspath(path))
        if any(os.path.normpath(os.path.abspath(f[1])) == norm_path for f in favorites):
            return
            
        icon_name = "folder"
        basename = os.path.basename(path).lower()
        if basename == "desktop":
            icon_name = "user-desktop"
        elif basename == "documents":
            icon_name = "folder-documents"
        elif basename == "downloads":
            icon_name = "folder-download"
        elif basename == "music":
            icon_name = "folder-music"
        elif basename == "pictures":
            icon_name = "folder-pictures"
        elif basename == "videos":
            icon_name = "folder-videos"
            
        favorites.append((os.path.basename(path), path, icon_name, None))
        self.save_favorites_to_disk(favorites)
        self.reload_sidebar()

    def on_menu_properties(self, widget):
        paths = self.get_selected_paths()
        if not paths:
            return
        path = paths[0]
        
        # Details gathering
        name = os.path.basename(path)
        is_dir = os.path.isdir(path)
        
        if is_dir:
            size_str = _("calculating")
        else:
            try:
                size_bytes = os.path.getsize(path)
                size_str = f"{format_bytes(size_bytes)} ({size_bytes:,} bytes)"
            except:
                size_str = "N/A"
                
        type_str = get_file_type_desc(path)
        
        try:
            stat = os.stat(path)
            modified = datetime.datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
            owner = pwd.getpwuid(stat.st_uid).pw_name
            group = grp.getgrgid(stat.st_gid).gr_name
            permissions = oct(stat.st_mode & 0o777)
        except Exception:
            modified = "N/A"
            owner = "unknown"
            group = "unknown"
            permissions = ""
            
        # Dialog
        dialog = Gtk.Dialog(title=_("properties_title") + " - " + name, transient_for=self, flags=0)
        dialog.add_buttons(Gtk.STOCK_CLOSE, Gtk.ResponseType.CLOSE)
        dialog.set_default_size(360, 480)
        
        content = dialog.get_content_area()
        content.set_spacing(12)
        content.set_border_width(16)
        
        notebook = Gtk.Notebook()
        content.pack_start(notebook, True, True, 0)
        
        # General tab
        vbox_gen = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        vbox_gen.set_border_width(12)
        
        # Big Icon
        img = Gtk.Image.new_from_pixbuf(get_file_icon(path, size=64))
        img.set_alignment(0.5, 0.5)
        vbox_gen.pack_start(img, False, False, 10)
        
        grid = Gtk.Grid()
        grid.set_column_spacing(12)
        grid.set_row_spacing(10)
        vbox_gen.pack_start(grid, True, True, 0)
        
        items_list = [
            (_("name") + ":", name),
            (_("type") + ":", type_str),
            (_("size") + ":", size_str),
            (_("location") + ":", os.path.dirname(path)),
            (_("modified") + ":", modified),
        ]
        
        lbl_size_value = None
        for row_idx, (k, v) in enumerate(items_list):
            lbl_k = Gtk.Label(label=k)
            lbl_k.set_halign(Gtk.Align.END)
            lbl_k.get_style_context().add_class("dim-label")
            grid.attach(lbl_k, 0, row_idx, 1, 1)
            
            lbl_v = Gtk.Label(label=v)
            lbl_v.set_halign(Gtk.Align.START)
            lbl_v.set_line_wrap(True)
            lbl_v.set_max_width_chars(25)
            grid.attach(lbl_v, 1, row_idx, 1, 1)
            if k == _("size") + ":":
                lbl_size_value = lbl_v
            
        notebook.append_page(vbox_gen, Gtk.Label(label=_("general")))
        
        # Permissions tab
        vbox_perm = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        vbox_perm.set_border_width(12)
        
        grid_p = Gtk.Grid()
        grid_p.set_column_spacing(12)
        grid_p.set_row_spacing(10)
        vbox_perm.pack_start(grid_p, True, True, 0)
        
        p_list = [
            (_("owner") + ":", owner),
            (_("group") + ":", group),
            (_("octal") + ":", permissions),
        ]
        
        for row_idx, (k, v) in enumerate(p_list):
            lbl_k = Gtk.Label(label=k)
            lbl_k.set_halign(Gtk.Align.END)
            grid_p.attach(lbl_k, 0, row_idx, 1, 1)
            
            lbl_v = Gtk.Label(label=v)
            lbl_v.set_halign(Gtk.Align.START)
            grid_p.attach(lbl_v, 1, row_idx, 1, 1)
            
        notebook.append_page(vbox_perm, Gtk.Label(label=_("permissions")))
        
        # If it is a directory, start size calculation thread
        if is_dir and lbl_size_value:
            def calc_dir_size():
                size_bytes = 0
                file_count = 0
                for root, dirs, files in os.walk(path):
                    for f_name in files:
                        try:
                            size_bytes += os.path.getsize(os.path.join(root, f_name))
                            file_count += 1
                        except:
                            pass
                        if file_count % 1000 == 0:
                            current_size = size_bytes
                            current_count = file_count
                            GLib.idle_add(lbl_size_value.set_text, f"{format_bytes(current_size)} ({current_size:,} bytes) - {current_count:,} {_('items')}")
                final_size = size_bytes
                final_count = file_count
                GLib.idle_add(lbl_size_value.set_text, f"{format_bytes(final_size)} ({final_size:,} bytes) - {final_count:,} {_('items')}")
            
            t = threading.Thread(target=calc_dir_size)
            t.daemon = True
            t.start()
            
        dialog.show_all()
        dialog.run()
        dialog.destroy()

    def on_drag_data_received(self, widget, context, x, y, selection_data, info, time):
        uris = selection_data.get_uris()
        if uris:
            src_paths = []
            for uri in uris:
                # Convert URI file:// to local path
                gfile = Gio.File.new_for_uri(uri)
                path = gfile.get_path()
                if path:
                    src_paths.append(path)
                    
            if src_paths:
                # Default drop behavior is Copy
                self.start_paste_operation(src_paths, self.current_dir, 'copy')
            context.finish(True, False, time)

    def on_key_press(self, widget, event):
        keyname = Gdk.keyval_name(event.keyval)
        state = event.state
        
        focused = self.get_focus()
        if focused and isinstance(focused, Gtk.Entry):
            # If the entry has focus, let it handle standard editing keys
            if (state & Gdk.ModifierType.CONTROL_MASK) and keyname.lower() == "l":
                self.show_path_entry()
                return True
            if keyname in ["BackSpace", "Delete"] or (state & Gdk.ModifierType.CONTROL_MASK and keyname.lower() in ["c", "x", "v", "a", "z"]):
                return False
                
        # Backspace key (go up/back)
        if keyname == "BackSpace":
            self.on_up_clicked(None)
            return True
            
        # Alt+Left/Right navigation
        if (state & Gdk.ModifierType.MOD1_MASK):
            if keyname == "Left":
                self.on_back_clicked(None)
                return True
            elif keyname == "Right":
                self.on_forward_clicked(None)
                return True
                
        # Ctrl shortcuts
        if (state & Gdk.ModifierType.CONTROL_MASK):
            if keyname.lower() == "c":
                self.on_menu_copy(None)
                return True
            elif keyname.lower() == "x":
                self.on_menu_cut(None)
                return True
            elif keyname.lower() == "v":
                self.on_menu_paste(None)
                return True
            elif keyname.lower() == "h":
                # Toggle hidden files
                self.hidden_mitem.set_active(not self.hidden_mitem.get_active())
                return True
            elif keyname.lower() == "n":
                self.on_menu_new_folder(None)
                return True
            elif keyname.lower() == "l":
                self.show_path_entry()
                return True
                
        if keyname == "Delete":
            self.on_menu_trash(None)
            return True
            
        if keyname == "F2":
            self.on_menu_rename(None)
            return True
            
        return False

    def show_rename_dialog_for(self, filename):
        # Programmatic helper to rename right after folder creation
        path = os.path.join(self.current_dir, filename)
        if os.path.exists(path):
            self.on_menu_rename(None)

    def show_error_dialog(self, title, message):
        dialog = Gtk.MessageDialog(
            transient_for=self,
            flags=0,
            message_type=Gtk.MessageType.ERROR,
            buttons=Gtk.ButtonsType.OK,
            text=title
        )
        dialog.format_secondary_text(message)
        dialog.run()
        dialog.destroy()


def main():
    # Set default locale
    try:
        locale.setlocale(locale.LC_ALL, '')
    except:
        pass
        
    GLib.set_prgname("milofiles")
    GLib.set_application_name("miloFiles")
        
    initial_dir = None
    if len(sys.argv) > 1:
        # Check if the argument is a path
        arg_path = sys.argv[1]
        # Resolve file:// URIs if passed
        if arg_path.startswith("file://"):
            gfile = Gio.File.new_for_uri(arg_path)
            initial_dir = gfile.get_path()
        elif os.path.isdir(arg_path):
            initial_dir = arg_path
            
    win = miloFilesWindow(initial_dir)
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()
