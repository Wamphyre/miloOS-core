#!/usr/bin/env python3
"""
miloOS System Statistics Monitor
macOS Activity Monitor inspired interface for system monitoring
"""

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import Gtk, Gdk, GLib, GdkPixbuf
import subprocess
import os
import locale
import psutil
import time
import re
from collections import deque

# Translations
TRANSLATIONS = {
    'en': {
        'title': 'System Statistics',
        'overview': 'Overview',
        'cpu': 'CPU',
        'memory': 'Memory',
        'disk': 'Disk',
        'network': 'Network',
        'processes': 'Processes',
        'system_info': 'System Information',
        'milos_version': 'miloOS Version',
        'distributor': 'Distributor',
        'desktop_env': 'Desktop Environment',
        'xfce_version': 'XFCE Version',
        'gtk_version': 'GTK Version',
        'window_system': 'Window System',
        'gpu': 'GPU',
        'packages': 'Installed Packages',
        'kernel': 'Kernel',
        'uptime': 'Uptime',
        'processor': 'Processor',
        'cores': 'Cores',
        'threads': 'Threads',
        'frequency': 'Frequency',
        'current_freq': 'Current',
        'max_freq': 'Max',
        'usage': 'Usage',
        'memory_modules': 'Memory Modules',
        'total_memory': 'Total Memory',
        'type': 'Type',
        'speed': 'Speed',
        'model': 'Model',
        'capacity': 'Capacity',
        'read_speed': 'Read Speed',
        'write_speed': 'Write Speed',
        'interface': 'Interface',
        'download': 'Download',
        'upload': 'Upload',
        'process_name': 'Process Name',
        'cpu_percent': 'CPU %',
        'memory_percent': 'Memory %',
        'pid': 'PID',
        'user': 'User',
    },
    'es': {
        'title': 'Estadísticas del Sistema',
        'overview': 'Resumen',
        'cpu': 'CPU',
        'memory': 'Memoria',
        'disk': 'Disco',
        'network': 'Red',
        'processes': 'Procesos',
        'system_info': 'Información del Sistema',
        'milos_version': 'Versión de miloOS',
        'distributor': 'Distribuidor',
        'desktop_env': 'Entorno de Escritorio',
        'xfce_version': 'Versión de XFCE',
        'gtk_version': 'Versión de GTK',
        'window_system': 'Sistema de Ventanas',
        'gpu': 'GPU',
        'packages': 'Paquetes Instalados',
        'kernel': 'Kernel',
        'uptime': 'Tiempo Activo',
        'processor': 'Procesador',
        'cores': 'Núcleos',
        'threads': 'Hilos',
        'frequency': 'Frecuencia',
        'current_freq': 'Actual',
        'max_freq': 'Máxima',
        'usage': 'Uso',
        'memory_modules': 'Módulos de Memoria',
        'total_memory': 'Memoria Total',
        'type': 'Tipo',
        'speed': 'Velocidad',
        'model': 'Modelo',
        'capacity': 'Capacidad',
        'read_speed': 'Velocidad de Lectura',
        'write_speed': 'Velocidad de Escritura',
        'interface': 'Interfaz',
        'download': 'Descarga',
        'upload': 'Subida',
        'process_name': 'Nombre del Proceso',
        'cpu_percent': 'CPU %',
        'memory_percent': 'Memoria %',
        'pid': 'PID',
        'user': 'Usuario',
    }
}

def get_language():
    """Detect system language"""
    try:
        lang = locale.getlocale()[0]
        if lang and lang.startswith('es'):
            return 'es'
    except:
        pass
    return 'en'

def _(key):
    """Get translated string"""
    lang = get_language()
    return TRANSLATIONS.get(lang, TRANSLATIONS['en']).get(key, key)

def format_bytes(bytes_val):
    """Format bytes to human readable format"""
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if bytes_val < 1024.0:
            return f"{bytes_val:.1f} {unit}"
        bytes_val /= 1024.0
    return f"{bytes_val:.1f} PB"

class SysStatsWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title=_('title'))
        self.set_icon_name("sysstats")
        self.set_default_size(900, 550)
        self.set_position(Gtk.WindowPosition.CENTER)
        
        # Network history for graphs
        self.net_download_history = deque(maxlen=60)
        self.net_upload_history = deque(maxlen=60)
        self.last_net_io = psutil.net_io_counters()
        
        # Disk activity history
        self.disk_activity_history = deque(maxlen=60)
        self.last_disk_io = psutil.disk_io_counters()
        
        # Apply miloOS styling
        css_provider = Gtk.CssProvider()
        css_provider.load_from_data(b"""
            window {
                background-color: #f5f5f5;
            }
            .header-bar {
                background-color: #ffffff;
                border-bottom: 1px solid #d0d0d0;
                padding: 12px;
            }
            .tab-button {
                background-color: transparent;
                border: none;
                color: #666666;
                padding: 8px 20px;
                border-radius: 6px;
                font-size: 13px;
                margin: 0 4px;
            }
            .tab-button:checked {
                background-color: #007AFF;
                color: #ffffff;
            }
            .content-area {
                background-color: #ffffff;
                padding: 20px;
                margin: 20px;
                border-radius: 8px;
                border: 1px solid #e0e0e0;
            }
            .stat-label {
                color: #333333;
                font-size: 13px;
                font-weight: 600;
            }
            .stat-value {
                color: #666666;
                font-size: 12px;
            }
            .progress-bar {
                min-height: 8px;
                border-radius: 4px;
            }
            .progress-bar trough {
                background-color: #e0e0e0;
                border-radius: 4px;
            }
            .progress-bar progress {
                background-color: #007AFF;
                border-radius: 4px;
            }
            .process-list {
                background-color: #ffffff;
            }
            .process-header {
                background-color: #f5f5f5;
                color: #333333;
                font-weight: 600;
                font-size: 11px;
                padding: 8px;
            }
        """)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            css_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
        
        # Main container
        main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.add(main_box)
        
        # Header with tabs
        header = self.create_header()
        main_box.pack_start(header, False, False, 0)
        
        # Content stack
        self.content_stack = Gtk.Stack()
        self.content_stack.set_transition_type(Gtk.StackTransitionType.CROSSFADE)
        main_box.pack_start(self.content_stack, True, True, 0)
        
        # Create pages
        self.create_overview_page()
        self.create_cpu_page()
        self.create_memory_page()
        self.create_disk_page()
        self.create_network_page()
        self.create_processes_page()
        
        # Update timer - faster for network graphs
        GLib.timeout_add(1000, self.update_stats)
        
    def create_header(self):
        """Create header with tab buttons"""
        header_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        header_box.get_style_context().add_class("header-bar")
        header_box.set_halign(Gtk.Align.CENTER)
        
        # Tab buttons
        self.overview_btn = Gtk.RadioButton(label=_('overview'))
        self.overview_btn.get_style_context().add_class("tab-button")
        self.overview_btn.connect("toggled", self.on_tab_changed, "overview")
        header_box.pack_start(self.overview_btn, False, False, 0)
        
        self.cpu_btn = Gtk.RadioButton(label=_('cpu'))
        self.cpu_btn.join_group(self.overview_btn)
        self.cpu_btn.get_style_context().add_class("tab-button")
        self.cpu_btn.connect("toggled", self.on_tab_changed, "cpu")
        header_box.pack_start(self.cpu_btn, False, False, 0)
        
        self.memory_btn = Gtk.RadioButton(label=_('memory'))
        self.memory_btn.join_group(self.overview_btn)
        self.memory_btn.get_style_context().add_class("tab-button")
        self.memory_btn.connect("toggled", self.on_tab_changed, "memory")
        header_box.pack_start(self.memory_btn, False, False, 0)
        
        self.disk_btn = Gtk.RadioButton(label=_('disk'))
        self.disk_btn.join_group(self.overview_btn)
        self.disk_btn.get_style_context().add_class("tab-button")
        self.disk_btn.connect("toggled", self.on_tab_changed, "disk")
        header_box.pack_start(self.disk_btn, False, False, 0)
        
        self.network_btn = Gtk.RadioButton(label=_('network'))
        self.network_btn.join_group(self.overview_btn)
        self.network_btn.get_style_context().add_class("tab-button")
        self.network_btn.connect("toggled", self.on_tab_changed, "network")
        header_box.pack_start(self.network_btn, False, False, 0)
        
        self.processes_btn = Gtk.RadioButton(label=_('processes'))
        self.processes_btn.join_group(self.overview_btn)
        self.processes_btn.get_style_context().add_class("tab-button")
        self.processes_btn.connect("toggled", self.on_tab_changed, "processes")
        header_box.pack_start(self.processes_btn, False, False, 0)
        
        return header_box
    
    def on_tab_changed(self, button, page_name):
        """Handle tab change"""
        if button.get_active():
            self.content_stack.set_visible_child_name(page_name)
            self.update_stats()
    
    def get_system_info(self):
        """Get system information"""
        info = {}
        
        # miloOS version and distributor
        try:
            if os.path.exists('/etc/os-release'):
                with open('/etc/os-release') as f:
                    for line in f:
                        if line.startswith('PRETTY_NAME'):
                            info['os'] = line.split('=')[1].strip().strip('"')
                        elif line.startswith('ID='):
                            distributor = line.split('=')[1].strip().strip('"')
                            info['distributor'] = distributor.capitalize()
        except:
            info['os'] = 'miloOS'
            info['distributor'] = 'Unknown'
        
        if 'distributor' not in info:
            info['distributor'] = 'Debian'
        
        # Desktop environment
        info['desktop'] = os.environ.get('XDG_CURRENT_DESKTOP', 'XFCE')
        
        # XFCE version
        try:
            result = subprocess.run(['xfce4-about', '--version'], 
                                  capture_output=True, text=True, timeout=2)
            if result.returncode == 0:
                for line in result.stdout.split('\n'):
                    if 'xfce4-about' in line.lower():
                        parts = line.split()
                        if len(parts) >= 2:
                            version = parts[-1].strip()
                            # Remove any trailing parenthesis
                            version = version.rstrip(')')
                            info['xfce_version'] = version
                            break
        except:
            pass
        
        if 'xfce_version' not in info:
            info['xfce_version'] = 'N/A'
        
        # GTK version
        info['gtk_version'] = f"{Gtk.get_major_version()}.{Gtk.get_minor_version()}.{Gtk.get_micro_version()}"
        
        # Window system (X11 or Wayland)
        info['window_system'] = os.environ.get('XDG_SESSION_TYPE', 'Unknown').upper()
        if info['window_system'] == 'UNKNOWN':
            # Fallback detection
            if os.environ.get('WAYLAND_DISPLAY'):
                info['window_system'] = 'Wayland'
            elif os.environ.get('DISPLAY'):
                info['window_system'] = 'X11'
        
        # GPU
        try:
            result = subprocess.run(['lspci'], capture_output=True, text=True)
            if result.returncode == 0:
                for line in result.stdout.split('\n'):
                    if 'VGA compatible controller' in line or 'Display controller' in line or '3D controller' in line:
                        parts = line.split(':', 2)
                        if len(parts) >= 3:
                            gpu = parts[2].strip()
                            # Clean up the GPU name
                            if '(rev' in gpu:
                                gpu = gpu.split('(rev')[0].strip()
                            info['gpu'] = gpu
                            break
        except:
            pass
        
        if 'gpu' not in info:
            info['gpu'] = 'Unknown GPU'
        
        # CPU
        try:
            with open('/proc/cpuinfo') as f:
                for line in f:
                    if 'model name' in line:
                        info['cpu'] = line.split(':')[1].strip()
                        break
        except:
            pass
        
        if 'cpu' not in info:
            info['cpu'] = 'Unknown CPU'
        
        # RAM
        mem = psutil.virtual_memory()
        info['ram'] = format_bytes(mem.total)
        
        # Kernel
        info['kernel'] = os.uname().release
        
        # Uptime
        uptime_seconds = int(time.time() - psutil.boot_time())
        days = uptime_seconds // 86400
        hours = (uptime_seconds % 86400) // 3600
        minutes = (uptime_seconds % 3600) // 60
        info['uptime'] = f"{days}d {hours}h {minutes}m"
        
        # Packages
        try:
            result = subprocess.run(['dpkg', '-l'], capture_output=True, text=True)
            info['packages'] = str(len([l for l in result.stdout.split('\n') if l.startswith('ii')]))
        except:
            info['packages'] = 'N/A'
        
        return info
    
    def create_overview_page(self):
        """Create overview page with system summary"""
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        page.get_style_context().add_class("content-area")
        
        # Get system info
        sys_info = self.get_system_info()
        
        # Create grid for info
        grid = Gtk.Grid()
        grid.set_column_spacing(40)
        grid.set_row_spacing(12)
        grid.set_halign(Gtk.Align.CENTER)
        grid.set_valign(Gtk.Align.CENTER)
        
        row = 0
        
        # miloOS logo
        logo_path = '/usr/share/themes/miloOS/logo.png'
        if os.path.exists(logo_path):
            try:
                pixbuf = GdkPixbuf.Pixbuf.new_from_file_at_scale(logo_path, 80, 80, True)
                logo_image = Gtk.Image.new_from_pixbuf(pixbuf)
                logo_image.set_margin_bottom(20)
                grid.attach(logo_image, 0, row, 2, 1)
                row += 1
            except:
                pass
        
        # System info section
        title = Gtk.Label()
        title.set_markup(f"<span size='14000' weight='bold'>{_('system_info')}</span>")
        grid.attach(title, 0, row, 2, 1)
        row += 1
        
        info_items = [
            (_('milos_version'), sys_info['os']),
            (_('kernel'), sys_info['kernel']),
            (_('desktop_env'), sys_info['desktop']),
            (_('xfce_version'), sys_info.get('xfce_version', 'N/A')),
            (_('gtk_version'), sys_info.get('gtk_version', 'N/A')),
            (_('window_system'), sys_info.get('window_system', 'N/A')),
            (_('gpu'), sys_info.get('gpu', 'N/A')),
            (_('processor'), sys_info.get('cpu', 'N/A')),
            (_('total_memory'), sys_info.get('ram', 'N/A')),
            (_('packages'), sys_info['packages']),
            (_('uptime'), sys_info['uptime'])
        ]
        
        for label_text, value_text in info_items:
            label = Gtk.Label()
            label.set_markup(f"<b>{label_text}:</b>")
            label.set_halign(Gtk.Align.END)
            label.set_valign(Gtk.Align.START)
            grid.attach(label, 0, row, 1, 1)
            
            value = Gtk.Label(label=value_text)
            value.set_halign(Gtk.Align.START)
            value.set_line_wrap(True)
            value.set_max_width_chars(50)
            grid.attach(value, 1, row, 1, 1)
            row += 1
        
        page.pack_start(grid, True, True, 0)
        self.content_stack.add_named(page, "overview")
    
    def get_cpu_name(self):
        """Get CPU model name"""
        try:
            with open('/proc/cpuinfo') as f:
                for line in f:
                    if 'model name' in line:
                        return line.split(':')[1].strip()
        except:
            pass
        return 'Unknown CPU'
    
    def draw_cpu_core(self, widget, cr, core_index):
        """Draw CPU core usage square"""
        width = widget.get_allocated_width()
        height = widget.get_allocated_height()
        
        # Get usage for this core
        usage = self.cpu_core_widgets[core_index]['usage'] if core_index < len(self.cpu_core_widgets) else 0
        
        # Background
        cr.set_source_rgb(0.9, 0.9, 0.9)
        cr.rectangle(0, 0, width, height)
        cr.fill()
        
        # Usage fill (from bottom)
        fill_height = height * (usage / 100.0)
        
        # Color based on usage
        if usage < 50:
            cr.set_source_rgb(0.2, 0.78, 0.35)  # Green
        elif usage < 80:
            cr.set_source_rgb(1.0, 0.77, 0.25)  # Orange
        else:
            cr.set_source_rgb(0.78, 0.15, 0.18)  # Red
        
        cr.rectangle(0, height - fill_height, width, fill_height)
        cr.fill()
        
        # Border
        cr.set_source_rgb(0.7, 0.7, 0.7)
        cr.set_line_width(1)
        cr.rectangle(0, 0, width, height)
        cr.stroke()
        
        return False
    
    def create_cpu_page(self):
        """Create CPU monitoring page"""
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
        page.get_style_context().add_class("content-area")
        
        # CPU hardware info
        cpu_name = self.get_cpu_name()
        cpu_cores = psutil.cpu_count(logical=False)
        cpu_threads = psutil.cpu_count(logical=True)
        cpu_freq = psutil.cpu_freq()
        
        # Core usage visualization (squares)
        cores_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        cores_box.set_halign(Gtk.Align.CENTER)
        
        cores_label = Gtk.Label()
        cores_label.set_markup(f"<span size='11000' weight='bold'>{_('usage')}</span>")
        cores_box.pack_start(cores_label, False, False, 0)
        
        # Grid for core squares
        self.cpu_cores_grid = Gtk.Grid()
        self.cpu_cores_grid.set_column_spacing(8)
        self.cpu_cores_grid.set_row_spacing(8)
        self.cpu_cores_grid.set_halign(Gtk.Align.CENTER)
        
        # Create squares for each thread
        self.cpu_core_widgets = []
        cols = 8  # 8 cores per row
        for i in range(cpu_threads):
            core_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
            
            # Drawing area for the square
            drawing = Gtk.DrawingArea()
            drawing.set_size_request(60, 60)
            drawing.connect('draw', self.draw_cpu_core, i)
            core_box.pack_start(drawing, False, False, 0)
            
            # Label with core number
            label = Gtk.Label(label=f"Core {i}")
            label.get_style_context().add_class("stat-value")
            core_box.pack_start(label, False, False, 0)
            
            # Percentage label
            percent_label = Gtk.Label(label="0%")
            percent_label.get_style_context().add_class("stat-value")
            core_box.pack_start(percent_label, False, False, 0)
            
            self.cpu_core_widgets.append({
                'drawing': drawing,
                'label': percent_label,
                'usage': 0
            })
            
            row_pos = i // cols
            col_pos = i % cols
            self.cpu_cores_grid.attach(core_box, col_pos, row_pos, 1, 1)
        
        cores_box.pack_start(self.cpu_cores_grid, False, False, 0)
        page.pack_start(cores_box, False, False, 0)
        
        # Separator
        sep = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        sep.set_margin_top(10)
        sep.set_margin_bottom(10)
        page.pack_start(sep, False, False, 0)
        
        # Create grid for info
        grid = Gtk.Grid()
        grid.set_column_spacing(30)
        grid.set_row_spacing(12)
        grid.set_halign(Gtk.Align.CENTER)
        
        row = 0
        
        # CPU model
        label = Gtk.Label()
        label.set_markup(f"<b>{_('processor')}:</b>")
        label.set_halign(Gtk.Align.END)
        grid.attach(label, 0, row, 1, 1)
        
        value = Gtk.Label(label=cpu_name)
        value.set_halign(Gtk.Align.START)
        value.set_line_wrap(True)
        value.set_max_width_chars(50)
        grid.attach(value, 1, row, 1, 1)
        row += 1
        
        # Cores
        label = Gtk.Label()
        label.set_markup(f"<b>{_('cores')}:</b>")
        label.set_halign(Gtk.Align.END)
        grid.attach(label, 0, row, 1, 1)
        
        value = Gtk.Label(label=str(cpu_cores))
        value.set_halign(Gtk.Align.START)
        grid.attach(value, 1, row, 1, 1)
        row += 1
        
        # Threads
        label = Gtk.Label()
        label.set_markup(f"<b>{_('threads')}:</b>")
        label.set_halign(Gtk.Align.END)
        grid.attach(label, 0, row, 1, 1)
        
        value = Gtk.Label(label=str(cpu_threads))
        value.set_halign(Gtk.Align.START)
        grid.attach(value, 1, row, 1, 1)
        row += 1
        
        # Frequency
        if cpu_freq:
            label = Gtk.Label()
            label.set_markup(f"<b>{_('current_freq')}:</b>")
            label.set_halign(Gtk.Align.END)
            grid.attach(label, 0, row, 1, 1)
            
            self.cpu_freq_label = Gtk.Label(label=f"{cpu_freq.current:.0f} MHz")
            self.cpu_freq_label.set_halign(Gtk.Align.START)
            grid.attach(self.cpu_freq_label, 1, row, 1, 1)
            row += 1
            
            label = Gtk.Label()
            label.set_markup(f"<b>{_('max_freq')}:</b>")
            label.set_halign(Gtk.Align.END)
            grid.attach(label, 0, row, 1, 1)
            
            value = Gtk.Label(label=f"{cpu_freq.max:.0f} MHz")
            value.set_halign(Gtk.Align.START)
            grid.attach(value, 1, row, 1, 1)
            row += 1
        
        # Usage
        label = Gtk.Label()
        label.set_markup(f"<b>{_('usage')}:</b>")
        label.set_halign(Gtk.Align.END)
        grid.attach(label, 0, row, 1, 1)
        
        self.cpu_usage_label = Gtk.Label(label="0%")
        self.cpu_usage_label.set_halign(Gtk.Align.START)
        grid.attach(self.cpu_usage_label, 1, row, 1, 1)
        
        page.pack_start(grid, True, True, 0)
        self.content_stack.add_named(page, "cpu")
    
    def get_memory_info(self):
        """Get memory hardware info"""
        info = {'modules': [], 'total': format_bytes(psutil.virtual_memory().total)}
        
        try:
            # Try different methods to get dmidecode output
            result = None
            
            # Method 1: Try with pkexec using helper script (GUI password prompt)
            try:
                result = subprocess.run(['pkexec', '/usr/local/bin/sysstats-dmidecode-helper'], 
                                      capture_output=True, text=True, timeout=30)
                if result.returncode != 0:
                    result = None
            except Exception as e:
                print(f"pkexec failed: {e}")
                result = None
            
            # Method 2: Try with sudo (might be configured with NOPASSWD)
            if not result or result.returncode != 0:
                try:
                    result = subprocess.run(['sudo', '-n', 'dmidecode', '-t', 'memory'], 
                                          capture_output=True, text=True, timeout=5)
                    if result.returncode != 0:
                        result = None
                except:
                    result = None
            
            # Method 3: Try direct dmidecode (if user has permissions)
            if not result or result.returncode != 0:
                try:
                    result = subprocess.run(['dmidecode', '-t', 'memory'], 
                                          capture_output=True, text=True, timeout=5)
                except:
                    result = None
            
            if result and result.returncode == 0:
                current_module = {}
                in_memory_device = False
                
                for line in result.stdout.split('\n'):
                    line_stripped = line.strip()
                    
                    # Detect start of a memory device section
                    # "Memory Device" appears on its own line after "Handle"
                    if line_stripped == 'Memory Device':
                        # Save previous module if it exists and has size
                        if current_module.get('size'):
                            info['modules'].append(current_module.copy())
                        current_module = {}
                        in_memory_device = True
                        continue
                    
                    if not in_memory_device:
                        continue
                    
                    if line_stripped.startswith('Size:'):
                        size = line_stripped.split(':', 1)[1].strip()
                        # Skip empty modules but continue processing other modules
                        if 'No Module Installed' in size or size in ['No Module Installed', 'Not Installed']:
                            current_module = {}  # Reset to skip this module
                            # Don't set in_memory_device to False, just skip this one
                            continue
                        current_module['size'] = size
                    elif 'Type:' in line_stripped and current_module.get('size'):
                        mem_type = line_stripped.split(':', 1)[1].strip()
                        # Only save if it's a real type and not "Type Detail"
                        if mem_type not in ['Unknown', 'Other', '<OUT OF SPEC>'] and 'Detail' not in line_stripped:
                            current_module['type'] = mem_type
                    elif 'Speed:' in line_stripped and current_module.get('size'):
                        speed = line_stripped.split(':', 1)[1].strip()
                        # Avoid "Configured Memory Speed", only get "Speed"
                        if speed not in ['Unknown', 'Not Specified'] and 'Configured' not in line_stripped and 'Memory' not in line_stripped:
                            current_module['speed'] = speed
                    elif 'Manufacturer:' in line_stripped and current_module.get('size'):
                        manufacturer = line_stripped.split(':', 1)[1].strip()
                        if manufacturer not in ['Unknown', 'Not Specified', 'NO DIMM', '']:
                            current_module['manufacturer'] = manufacturer
                
                # Don't forget the last module
                if current_module.get('size'):
                    info['modules'].append(current_module.copy())
                    
        except Exception as e:
            print(f"Error getting memory info: {e}")
        
        # If no modules found, create generic entries based on total memory
        if not info['modules']:
            mem = psutil.virtual_memory()
            total_gb = mem.total / (1024**3)
            
            # Estimate number of modules (common configurations)
            if total_gb <= 8:
                # Single module
                info['modules'].append({
                    'size': format_bytes(mem.total),
                    'type': 'Unknown',
                    'speed': 'Unknown',
                    'manufacturer': 'Run with elevated privileges for details'
                })
            else:
                # Assume 2 modules for larger amounts
                module_size = mem.total // 2
                for i in range(2):
                    info['modules'].append({
                        'size': format_bytes(module_size),
                        'type': 'Unknown',
                        'speed': 'Unknown',
                        'manufacturer': 'Run with elevated privileges for details'
                    })
        
        return info
    
    def create_memory_page(self):
        """Create memory monitoring page"""
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
        page.get_style_context().add_class("content-area")
        
        mem_info = self.get_memory_info()
        
        # Memory modules visualization (squares)
        modules_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        modules_box.set_halign(Gtk.Align.CENTER)
        
        modules_label = Gtk.Label()
        modules_label.set_markup(f"<span size='11000' weight='bold'>{_('memory_modules')}</span>")
        modules_box.pack_start(modules_label, False, False, 0)
        
        # Grid for memory module squares
        self.memory_modules_grid = Gtk.Grid()
        self.memory_modules_grid.set_column_spacing(15)
        self.memory_modules_grid.set_row_spacing(8)
        self.memory_modules_grid.set_halign(Gtk.Align.CENTER)
        
        # Create squares for each module
        self.memory_module_widgets = []
        for i, module in enumerate(mem_info['modules']):
            module_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
            
            # Drawing area for the square
            drawing = Gtk.DrawingArea()
            drawing.set_size_request(80, 80)
            drawing.connect('draw', self.draw_memory_module, i)
            module_box.pack_start(drawing, False, False, 0)
            
            # Label with module info
            module_text = f"Módulo {i+1}" if get_language() == 'es' else f"Module {i+1}"
            label = Gtk.Label(label=module_text)
            label.get_style_context().add_class("stat-value")
            module_box.pack_start(label, False, False, 0)
            
            # Size label
            size_label = Gtk.Label(label=module.get('size', 'N/A'))
            size_label.get_style_context().add_class("stat-value")
            module_box.pack_start(size_label, False, False, 0)
            
            # Type and speed
            if module.get('type') and module.get('speed'):
                type_label = Gtk.Label(label=f"{module['type']} @ {module['speed']}")
                type_label.get_style_context().add_class("stat-value")
                module_box.pack_start(type_label, False, False, 0)
            
            self.memory_module_widgets.append({
                'drawing': drawing,
                'usage': 0
            })
            
            self.memory_modules_grid.attach(module_box, i, 0, 1, 1)
        
        modules_box.pack_start(self.memory_modules_grid, False, False, 0)
        page.pack_start(modules_box, False, False, 0)
        
        # Separator
        sep = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        sep.set_margin_top(10)
        sep.set_margin_bottom(10)
        page.pack_start(sep, False, False, 0)
        
        # Detailed memory info
        info_grid = Gtk.Grid()
        info_grid.set_column_spacing(30)
        info_grid.set_row_spacing(8)
        info_grid.set_halign(Gtk.Align.CENTER)
        
        row = 0
        
        # Total memory
        label = Gtk.Label()
        label.set_markup(f"<b>{_('total_memory')}:</b>")
        label.set_halign(Gtk.Align.END)
        info_grid.attach(label, 0, row, 1, 1)
        
        value = Gtk.Label(label=mem_info['total'])
        value.set_halign(Gtk.Align.START)
        info_grid.attach(value, 1, row, 1, 1)
        row += 1
        
        # Current usage
        label = Gtk.Label()
        label.set_markup(f"<b>{_('usage')}:</b>")
        label.set_halign(Gtk.Align.END)
        info_grid.attach(label, 0, row, 1, 1)
        
        self.memory_usage_label = Gtk.Label(label="0%")
        self.memory_usage_label.set_halign(Gtk.Align.START)
        info_grid.attach(self.memory_usage_label, 1, row, 1, 1)
        row += 1
        
        # Module details
        if mem_info['modules']:
            for i, module in enumerate(mem_info['modules'], 1):
                # Module header
                label = Gtk.Label()
                module_text = f"Módulo {i}" if get_language() == 'es' else f"Module {i}"
                label.set_markup(f"<b>{module_text}:</b>")
                label.set_halign(Gtk.Align.END)
                info_grid.attach(label, 0, row, 1, 1)
                
                # Module info
                module_info = []
                if module.get('size'):
                    module_info.append(module['size'])
                if module.get('type'):
                    module_info.append(module['type'])
                if module.get('speed'):
                    module_info.append(module['speed'])
                if module.get('manufacturer'):
                    module_info.append(module['manufacturer'])
                
                value = Gtk.Label(label=' - '.join(module_info))
                value.set_halign(Gtk.Align.START)
                value.set_line_wrap(True)
                value.set_max_width_chars(50)
                info_grid.attach(value, 1, row, 1, 1)
                row += 1
        
        page.pack_start(info_grid, False, False, 0)
        
        self.content_stack.add_named(page, "memory")
    
    def draw_memory_module(self, widget, cr, module_index):
        """Draw memory module usage square"""
        width = widget.get_allocated_width()
        height = widget.get_allocated_height()
        
        # Get overall memory usage (same for all modules since we can't get per-module usage)
        mem = psutil.virtual_memory()
        usage = mem.percent
        
        # Background
        cr.set_source_rgb(0.9, 0.9, 0.9)
        cr.rectangle(0, 0, width, height)
        cr.fill()
        
        # Usage fill (from bottom)
        fill_height = height * (usage / 100.0)
        
        # Color based on usage
        if usage < 50:
            cr.set_source_rgb(0.2, 0.78, 0.35)  # Green
        elif usage < 80:
            cr.set_source_rgb(1.0, 0.77, 0.25)  # Orange
        else:
            cr.set_source_rgb(0.78, 0.15, 0.18)  # Red
        
        cr.rectangle(0, height - fill_height, width, fill_height)
        cr.fill()
        
        # Border
        cr.set_source_rgb(0.7, 0.7, 0.7)
        cr.set_line_width(1)
        cr.rectangle(0, 0, width, height)
        cr.stroke()
        
        # Draw percentage text
        cr.set_source_rgb(0.2, 0.2, 0.2)
        cr.select_font_face("Sans", 0, 1)
        cr.set_font_size(14)
        text = f"{usage:.0f}%"
        extents = cr.text_extents(text)
        cr.move_to((width - extents.width) / 2, (height + extents.height) / 2)
        cr.show_text(text)
        
        return False
    
    def get_disk_info(self):
        """Get disk hardware info"""
        disks = []
        
        try:
            # Get disk info from lsblk with better parsing
            result = subprocess.run(['lsblk', '-d', '-n', '-o', 'NAME,MODEL,SIZE,ROTA,TYPE'], 
                                  capture_output=True, text=True)
            if result.returncode == 0:
                lines = result.stdout.strip().split('\n')
                for line in lines:
                    if not line.strip():
                        continue
                    
                    parts = line.split()
                    if len(parts) < 3:
                        continue
                    
                    name = parts[0]
                    
                    # Skip loop devices, ram, and other virtual devices
                    if name.startswith('loop') or name.startswith('ram') or name.startswith('sr'):
                        continue
                    
                    # Last part should be 'disk'
                    if parts[-1] != 'disk':
                        continue
                    
                    # ROTA is second to last
                    rota = parts[-2]
                    disk_type = 'HDD' if rota == '1' else 'SSD'
                    
                    # Size is third to last
                    size = parts[-3]
                    
                    # Model is everything between name and size
                    # parts[0] = name, parts[1:-3] = model, parts[-3] = size, parts[-2] = rota, parts[-1] = type
                    if len(parts) > 3:
                        model = ' '.join(parts[1:-3])
                    else:
                        model = 'Generic Disk'
                    
                    # Determine interface
                    interface = 'SATA'
                    if 'nvme' in name.lower():
                        interface = 'NVMe'
                        disk_type = 'NVMe SSD'
                    elif 'mmc' in name.lower():
                        interface = 'eMMC'
                        disk_type = 'eMMC'
                    
                    disks.append({
                        'name': name,
                        'model': model if model else f'{name.upper()} Drive',
                        'size': size,
                        'type': disk_type,
                        'interface': interface
                    })
        except Exception as e:
            print(f"Error getting disk info: {e}")
        
        # If no disks found, try to get at least the root partition info
        if not disks:
            try:
                partitions = psutil.disk_partitions()
                if partitions:
                    root_part = [p for p in partitions if p.mountpoint == '/'][0]
                    disks.append({
                        'name': root_part.device.split('/')[-1].rstrip('0123456789'),
                        'model': 'System Disk',
                        'size': format_bytes(psutil.disk_usage('/').total),
                        'type': 'Unknown',
                        'interface': 'Unknown'
                    })
            except:
                pass
        
        return disks
    
    def create_disk_page(self):
        """Create disk monitoring page"""
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
        page.get_style_context().add_class("content-area")
        
        disks = self.get_disk_info()
        
        if not disks:
            # Show message if no disks found
            label = Gtk.Label(label="No disk information available")
            label.set_halign(Gtk.Align.CENTER)
            label.set_valign(Gtk.Align.CENTER)
            page.pack_start(label, True, True, 0)
        else:
            # Disk visualization (squares)
            disks_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
            disks_box.set_halign(Gtk.Align.CENTER)
            
            disks_label = Gtk.Label()
            disks_label.set_markup(f"<span size='11000' weight='bold'>{_('disk')}</span>")
            disks_box.pack_start(disks_label, False, False, 0)
            
            # Grid for disk squares
            self.disks_grid = Gtk.Grid()
            self.disks_grid.set_column_spacing(15)
            self.disks_grid.set_row_spacing(8)
            self.disks_grid.set_halign(Gtk.Align.CENTER)
            
            # Create squares for each disk
            self.disk_widgets = []
            for i, disk in enumerate(disks):
                disk_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
                
                # Drawing area for the square
                drawing = Gtk.DrawingArea()
                drawing.set_size_request(100, 100)
                drawing.connect('draw', self.draw_disk, i)
                disk_box.pack_start(drawing, False, False, 0)
                
                # Label with disk name
                disk_text = f"Disco {i+1}" if get_language() == 'es' else f"Disk {i+1}"
                label = Gtk.Label(label=disk_text)
                label.get_style_context().add_class("stat-value")
                label.set_markup(f"<b>{disk_text}</b>")
                disk_box.pack_start(label, False, False, 0)
                
                # Model label
                model_label = Gtk.Label(label=disk['model'][:20])
                model_label.get_style_context().add_class("stat-value")
                model_label.set_line_wrap(True)
                model_label.set_max_width_chars(15)
                disk_box.pack_start(model_label, False, False, 0)
                
                # Type and size
                type_label = Gtk.Label(label=f"{disk['type']} - {disk['size']}")
                type_label.get_style_context().add_class("stat-value")
                disk_box.pack_start(type_label, False, False, 0)
                
                # Usage label
                usage_label = Gtk.Label(label="0%")
                usage_label.get_style_context().add_class("stat-value")
                disk_box.pack_start(usage_label, False, False, 0)
                
                self.disk_widgets.append({
                    'drawing': drawing,
                    'usage_label': usage_label,
                    'name': disk['name'],
                    'usage': 0
                })
                
                self.disks_grid.attach(disk_box, i, 0, 1, 1)
            
            disks_box.pack_start(self.disks_grid, False, False, 0)
            page.pack_start(disks_box, False, False, 0)
            
            # Separator
            sep = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
            sep.set_margin_top(15)
            sep.set_margin_bottom(15)
            page.pack_start(sep, False, False, 0)
            
            # Disk space summary
            summary_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
            summary_box.set_halign(Gtk.Align.CENTER)
            
            summary_label = Gtk.Label()
            summary_label.set_markup(f"<span size='11000' weight='bold'>Espacio en Disco</span>" if get_language() == 'es' else f"<span size='11000' weight='bold'>Disk Space</span>")
            summary_box.pack_start(summary_label, False, False, 0)
            
            self.disk_summary_label = Gtk.Label()
            self.disk_summary_label.set_halign(Gtk.Align.CENTER)
            summary_box.pack_start(self.disk_summary_label, False, False, 0)
            
            # Disk activity graph
            activity_label = Gtk.Label()
            activity_label.set_markup(f"<span size='10000' weight='bold'>Actividad de Disco</span>" if get_language() == 'es' else f"<span size='10000' weight='bold'>Disk Activity</span>")
            activity_label.set_margin_top(10)
            summary_box.pack_start(activity_label, False, False, 0)
            
            self.disk_activity_graph = Gtk.DrawingArea()
            self.disk_activity_graph.set_size_request(600, 100)
            self.disk_activity_graph.connect('draw', self.draw_disk_activity)
            summary_box.pack_start(self.disk_activity_graph, False, False, 0)
            
            self.disk_activity_label = Gtk.Label(label="0 KB/s")
            self.disk_activity_label.set_halign(Gtk.Align.START)
            summary_box.pack_start(self.disk_activity_label, False, False, 0)
            
            page.pack_start(summary_box, False, False, 0)
        
        self.content_stack.add_named(page, "disk")
    
    def draw_disk(self, widget, cr, disk_index):
        """Draw disk usage square"""
        width = widget.get_allocated_width()
        height = widget.get_allocated_height()
        
        # Get usage for this disk
        usage = self.disk_widgets[disk_index]['usage'] if disk_index < len(self.disk_widgets) else 0
        
        # Background
        cr.set_source_rgb(0.9, 0.9, 0.9)
        cr.rectangle(0, 0, width, height)
        cr.fill()
        
        # Usage fill (from bottom)
        fill_height = height * (usage / 100.0)
        
        # Color based on usage
        if usage < 50:
            cr.set_source_rgb(0.2, 0.78, 0.35)  # Green
        elif usage < 80:
            cr.set_source_rgb(1.0, 0.77, 0.25)  # Orange
        else:
            cr.set_source_rgb(0.78, 0.15, 0.18)  # Red
        
        cr.rectangle(0, height - fill_height, width, fill_height)
        cr.fill()
        
        # Border
        cr.set_source_rgb(0.7, 0.7, 0.7)
        cr.set_line_width(1)
        cr.rectangle(0, 0, width, height)
        cr.stroke()
        
        # Draw percentage text
        cr.set_source_rgb(0.2, 0.2, 0.2)
        cr.select_font_face("Sans", 0, 1)
        cr.set_font_size(16)
        text = f"{usage:.0f}%"
        extents = cr.text_extents(text)
        cr.move_to((width - extents.width) / 2, (height + extents.height) / 2)
        cr.show_text(text)
        
        return False
    
    def draw_disk_activity(self, widget, cr):
        """Draw disk activity graph"""
        width = widget.get_allocated_width()
        height = widget.get_allocated_height()
        
        # Background
        cr.set_source_rgb(0.96, 0.96, 0.96)
        cr.rectangle(0, 0, width, height)
        cr.fill()
        
        # Get data
        data = list(self.disk_activity_history)
        
        if not data or len(data) < 2:
            return False
        
        # Find max value for scaling
        max_val = max(data) if max(data) > 0 else 1
        
        # Draw grid lines
        cr.set_source_rgb(0.9, 0.9, 0.9)
        cr.set_line_width(1)
        for i in range(5):
            y = height * i / 4
            cr.move_to(0, y)
            cr.line_to(width, y)
            cr.stroke()
        
        # Draw graph line
        cr.set_source_rgb(0.6, 0.4, 0.8)  # Purple color
        cr.set_line_width(2)
        
        step = width / (len(data) - 1)
        for i, value in enumerate(data):
            x = i * step
            y = height - (value / max_val * height * 0.9)
            
            if i == 0:
                cr.move_to(x, y)
            else:
                cr.line_to(x, y)
        
        cr.stroke()
        
        # Fill area under curve
        cr.set_source_rgba(0.6, 0.4, 0.8, 0.2)
        step = width / (len(data) - 1)
        cr.move_to(0, height)
        for i, value in enumerate(data):
            x = i * step
            y = height - (value / max_val * height * 0.9)
            cr.line_to(x, y)
        cr.line_to(width, height)
        cr.close_path()
        cr.fill()
        
        return False
    
    def get_active_network_interface(self):
        """Get the active network interface connected to internet"""
        try:
            # Get default route
            result = subprocess.run(['ip', 'route', 'show', 'default'], 
                                  capture_output=True, text=True)
            if result.returncode == 0 and result.stdout:
                # Parse: default via 192.168.1.1 dev wlan0 ...
                parts = result.stdout.split()
                if 'dev' in parts:
                    dev_index = parts.index('dev')
                    if dev_index + 1 < len(parts):
                        return parts[dev_index + 1]
        except:
            pass
        return None
    
    def get_network_card_info(self):
        """Get network card model for active interface"""
        active_interface = self.get_active_network_interface()
        
        if not active_interface:
            return "No active network connection"
        
        try:
            # Get interface info - try with pkexec first
            result = None
            try:
                result = subprocess.run(['pkexec', 'ethtool', '-i', active_interface], 
                                      capture_output=True, text=True, timeout=5)
            except:
                result = subprocess.run(['ethtool', '-i', active_interface], 
                                      capture_output=True, text=True)
            
            if result and result.returncode == 0:
                driver = None
                for line in result.stdout.split('\n'):
                    if line.startswith('driver:'):
                        driver = line.split(':')[1].strip()
                        break
                
                # Try to get more detailed info from lspci
                if driver:
                    lspci_result = subprocess.run(['lspci', '-v'], 
                                                capture_output=True, text=True)
                    if lspci_result.returncode == 0:
                        in_network_section = False
                        for line in lspci_result.stdout.split('\n'):
                            if 'Network controller' in line or 'Ethernet controller' in line:
                                in_network_section = True
                                # Extract model name
                                parts = line.split(':', 2)
                                if len(parts) >= 3:
                                    model = parts[2].strip()
                            elif in_network_section and 'Kernel driver in use:' in line:
                                if driver in line:
                                    return f"{model} ({active_interface})"
                                in_network_section = False
                
                # Fallback: just show interface and driver
                return f"{active_interface} ({driver})"
        except:
            pass
        
        # Final fallback
        return f"{active_interface}"
    
    def create_network_page(self):
        """Create network monitoring page with real-time graphs"""
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
        page.get_style_context().add_class("content-area")
        
        # Download graph
        download_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        
        download_label = Gtk.Label()
        download_label.set_markup(f"<b>{_('download')}</b>")
        download_label.set_halign(Gtk.Align.START)
        download_box.pack_start(download_label, False, False, 0)
        
        self.download_graph = Gtk.DrawingArea()
        self.download_graph.set_size_request(-1, 150)
        self.download_graph.connect('draw', self.draw_network_graph, 'download')
        download_box.pack_start(self.download_graph, False, False, 0)
        
        self.download_speed_label = Gtk.Label(label="0 KB/s")
        self.download_speed_label.set_halign(Gtk.Align.START)
        download_box.pack_start(self.download_speed_label, False, False, 0)
        
        page.pack_start(download_box, False, False, 0)
        
        # Upload graph
        upload_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        
        upload_label = Gtk.Label()
        upload_label.set_markup(f"<b>{_('upload')}</b>")
        upload_label.set_halign(Gtk.Align.START)
        upload_box.pack_start(upload_label, False, False, 0)
        
        self.upload_graph = Gtk.DrawingArea()
        self.upload_graph.set_size_request(-1, 150)
        self.upload_graph.connect('draw', self.draw_network_graph, 'upload')
        upload_box.pack_start(self.upload_graph, False, False, 0)
        
        self.upload_speed_label = Gtk.Label(label="0 KB/s")
        self.upload_speed_label.set_halign(Gtk.Align.START)
        upload_box.pack_start(self.upload_speed_label, False, False, 0)
        
        page.pack_start(upload_box, False, False, 0)
        
        # Network card info
        network_card = self.get_network_card_info()
        network_card_label = Gtk.Label()
        network_card_label.set_markup(f"<span size='9000'><b>{_('model')}:</b> {network_card}</span>")
        network_card_label.set_halign(Gtk.Align.START)
        network_card_label.set_line_wrap(True)
        network_card_label.set_max_width_chars(60)
        page.pack_start(network_card_label, False, False, 0)
        
        self.content_stack.add_named(page, "network")
    
    def draw_network_graph(self, widget, cr, graph_type):
        """Draw network activity graph"""
        width = widget.get_allocated_width()
        height = widget.get_allocated_height()
        
        # Background
        cr.set_source_rgb(0.96, 0.96, 0.96)
        cr.rectangle(0, 0, width, height)
        cr.fill()
        
        # Get data
        if graph_type == 'download':
            data = list(self.net_download_history)
        else:
            data = list(self.net_upload_history)
        
        if not data or len(data) < 2:
            return False
        
        # Find max value for scaling
        max_val = max(data) if max(data) > 0 else 1
        
        # Draw grid lines
        cr.set_source_rgb(0.9, 0.9, 0.9)
        cr.set_line_width(1)
        for i in range(5):
            y = height * i / 4
            cr.move_to(0, y)
            cr.line_to(width, y)
            cr.stroke()
        
        # Draw graph line
        cr.set_source_rgb(0.0, 0.48, 1.0)  # Blue color
        cr.set_line_width(2)
        
        step = width / (len(data) - 1)
        for i, value in enumerate(data):
            x = i * step
            y = height - (value / max_val * height * 0.9)
            
            if i == 0:
                cr.move_to(x, y)
            else:
                cr.line_to(x, y)
        
        cr.stroke()
        
        # Fill area under curve
        cr.set_source_rgba(0.0, 0.48, 1.0, 0.2)
        step = width / (len(data) - 1)
        cr.move_to(0, height)
        for i, value in enumerate(data):
            x = i * step
            y = height - (value / max_val * height * 0.9)
            cr.line_to(x, y)
        cr.line_to(width, height)
        cr.close_path()
        cr.fill()
        
        return False
    
    def create_processes_page(self):
        """Create processes list page"""
        page = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        page.get_style_context().add_class("content-area")
        
        # Process list
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        
        self.process_store = Gtk.ListStore(int, str, str, float, float)
        self.process_tree = Gtk.TreeView(model=self.process_store)
        self.process_tree.get_style_context().add_class("process-list")
        
        # Columns
        columns = [
            (_('pid'), 0),
            (_('process_name'), 1),
            (_('user'), 2),
            (_('cpu_percent'), 3),
            (_('memory_percent'), 4)
        ]
        
        for title, col_id in columns:
            if col_id in [3, 4]:
                renderer = Gtk.CellRendererText()
                column = Gtk.TreeViewColumn(title, renderer)
                column.set_cell_data_func(renderer, self.format_percent, col_id)
            else:
                renderer = Gtk.CellRendererText()
                column = Gtk.TreeViewColumn(title, renderer, text=col_id)
            column.set_resizable(True)
            column.set_sort_column_id(col_id)
            self.process_tree.append_column(column)
        
        scrolled.add(self.process_tree)
        page.pack_start(scrolled, True, True, 0)
        
        self.content_stack.add_named(page, "processes")
    
    def format_percent(self, column, cell, model, iter, col_id):
        """Format percentage values"""
        value = model.get_value(iter, col_id)
        cell.set_property('text', f'{value:.1f}%')
    
    def update_stats(self):
        """Update all statistics"""
        visible_page = self.content_stack.get_visible_child_name()
        
        if visible_page == "cpu":
            self.update_cpu_stats()
        elif visible_page == "memory":
            self.update_memory_stats()
        elif visible_page == "disk":
            self.update_disk_stats()
        elif visible_page == "network":
            self.update_network_stats()
        elif visible_page == "processes":
            self.update_processes()
        
        return True
    
    def update_disk_stats(self):
        """Update disk statistics"""
        partitions = psutil.disk_partitions()
        
        # Calculate total disk space
        total_space = 0
        used_space = 0
        free_space = 0
        
        # Map partitions to physical disks
        for widget in self.disk_widgets:
            disk_name = widget['name']
            # Find main partition for this disk
            total_usage = 0
            partition_count = 0
            
            for partition in partitions:
                # Check if partition belongs to this disk
                if disk_name in partition.device:
                    try:
                        usage = psutil.disk_usage(partition.mountpoint)
                        total_usage += usage.percent
                        partition_count += 1
                        
                        # Add to totals
                        total_space += usage.total
                        used_space += usage.used
                        free_space += usage.free
                    except:
                        pass
            
            # Calculate average usage
            if partition_count > 0:
                avg_usage = total_usage / partition_count
                widget['usage'] = avg_usage
                widget['usage_label'].set_text(f"{avg_usage:.1f}%")
                widget['drawing'].queue_draw()
        
        # Update disk space summary
        if hasattr(self, 'disk_summary_label'):
            total_text = f"Total: {format_bytes(total_space)}" if get_language() == 'en' else f"Total: {format_bytes(total_space)}"
            used_text = f"Usado: {format_bytes(used_space)}" if get_language() == 'es' else f"Used: {format_bytes(used_space)}"
            free_text = f"Libre: {format_bytes(free_space)}" if get_language() == 'es' else f"Free: {format_bytes(free_space)}"
            
            summary = f"{total_text}  |  {used_text}  |  {free_text}"
            self.disk_summary_label.set_text(summary)
        
        # Update disk activity
        if hasattr(self, 'disk_activity_graph'):
            disk_io = psutil.disk_io_counters()
            if disk_io and self.last_disk_io:
                # Calculate bytes per second
                time_delta = 1.0
                read_speed = (disk_io.read_bytes - self.last_disk_io.read_bytes) / time_delta
                write_speed = (disk_io.write_bytes - self.last_disk_io.write_bytes) / time_delta
                total_speed = read_speed + write_speed
                
                # Update history
                self.disk_activity_history.append(total_speed)
                
                # Update label
                self.disk_activity_label.set_text(f"{format_bytes(total_speed)}/s")
                
                # Redraw graph
                self.disk_activity_graph.queue_draw()
                
                # Save current values
                self.last_disk_io = disk_io
    
    def update_cpu_stats(self):
        """Update CPU statistics"""
        cpu_percent = psutil.cpu_percent(interval=0.1)
        self.cpu_usage_label.set_text(f"{cpu_percent:.1f}%")
        
        # Update per-core usage
        per_cpu = psutil.cpu_percent(interval=0.1, percpu=True)
        for i, usage in enumerate(per_cpu):
            if i < len(self.cpu_core_widgets):
                self.cpu_core_widgets[i]['usage'] = usage
                self.cpu_core_widgets[i]['label'].set_text(f"{usage:.0f}%")
                self.cpu_core_widgets[i]['drawing'].queue_draw()
        
        # Update frequency if available
        if hasattr(self, 'cpu_freq_label'):
            cpu_freq = psutil.cpu_freq()
            if cpu_freq:
                self.cpu_freq_label.set_text(f"{cpu_freq.current:.0f} MHz")
    
    def update_memory_stats(self):
        """Update memory statistics"""
        mem = psutil.virtual_memory()
        self.memory_usage_label.set_markup(f"<b>{_('usage')}:</b> {mem.percent:.1f}% ({format_bytes(mem.used)} / {format_bytes(mem.total)})")
        
        # Update memory module squares
        for widget in self.memory_module_widgets:
            widget['drawing'].queue_draw()
    
    def update_network_stats(self):
        """Update network statistics with graphs"""
        net_io = psutil.net_io_counters()
        
        # Calculate speed (bytes per second)
        time_delta = 1.0  # 1 second update interval
        download_speed = (net_io.bytes_recv - self.last_net_io.bytes_recv) / time_delta
        upload_speed = (net_io.bytes_sent - self.last_net_io.bytes_sent) / time_delta
        
        # Update history
        self.net_download_history.append(download_speed)
        self.net_upload_history.append(upload_speed)
        
        # Update labels
        self.download_speed_label.set_text(f"{format_bytes(download_speed)}/s")
        self.upload_speed_label.set_text(f"{format_bytes(upload_speed)}/s")
        
        # Redraw graphs
        self.download_graph.queue_draw()
        self.upload_graph.queue_draw()
        
        # Save current values
        self.last_net_io = net_io
    
    def update_processes(self):
        """Update process list"""
        self.process_store.clear()
        
        for proc in psutil.process_iter(['pid', 'name', 'username', 'cpu_percent', 'memory_percent']):
            try:
                info = proc.info
                self.process_store.append([
                    info['pid'],
                    info['name'],
                    info['username'] or '',
                    info['cpu_percent'] or 0.0,
                    info['memory_percent'] or 0.0
                ])
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass

def main():
    win = SysStatsWindow()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()
