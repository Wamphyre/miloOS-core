#!/usr/bin/env python3
"""
miloOS Audio Configuration Tool
macOS Audio MIDI Setup inspired interface for PipeWire configuration
"""

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk, GLib
import subprocess
import os
import locale
import re

# Translations
TRANSLATIONS = {
    'en': {
        'title': 'Audio Devices',
        'input': 'Input',
        'output': 'Output',
        'sample_rate': 'Sample Rate:',
        'buffer_size': 'Buffer Size:',
        'format': 'Format:',
        'speaker_config': 'Speaker Configuration:',
        'apply': 'Apply',
        'inputs': 'inputs',
        'outputs': 'outputs',
        'stereo': 'Stereo',
        'mono': 'Mono',
        'surround_51': '5.1 Surround',
        'surround_71': '7.1 Surround',
        'config_applied': 'Configuration Applied',
        'config_applied_msg': 'Audio configuration has been applied successfully.',
        'error': 'Error',
        'error_msg': 'Failed to apply configuration:\n{}'
    },
    'es': {
        'title': 'Dispositivos de audio',
        'input': 'Entrada',
        'output': 'Salida',
        'sample_rate': 'Frecuencia de muestreo:',
        'buffer_size': 'Tamaño de buffer:',
        'format': 'Formato:',
        'speaker_config': 'Configuración de altavoces:',
        'apply': 'Aplicar',
        'inputs': 'entradas',
        'outputs': 'salidas',
        'stereo': 'Estéreo',
        'mono': 'Mono',
        'surround_51': 'Envolvente 5.1',
        'surround_71': 'Envolvente 7.1',
        'config_applied': 'Configuración Aplicada',
        'config_applied_msg': 'La configuración de audio se ha aplicado correctamente.',
        'error': 'Error',
        'error_msg': 'Error al aplicar la configuración:\n{}'
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


class AudioDevice:
    """Represents an audio device"""
    def __init__(self, name, description, device_type, channels, sample_rates, formats):
        self.name = name
        self.description = description
        self.device_type = device_type  # 'sink' or 'source'
        self.channels = channels
        self.sample_rates = sample_rates  # List of available sample rates
        self.formats = formats  # List of available formats
        self.is_default = False
        self.current_rate = sample_rates[0] if sample_rates else '48000'
        self.current_format = formats[0] if formats else 's16le'

class AudioConfigWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title=_('title'))
        self.set_icon_name("audio-config")
        self.set_default_size(900, 550)
        self.set_position(Gtk.WindowPosition.CENTER)
        
        # Apply miloOS light theme styling
        css_provider = Gtk.CssProvider()
        css_provider.load_from_data(b"""
            window {
                background-color: #f5f5f5;
            }
            .device-sidebar {
                background-color: #ffffff;
                border-right: 1px solid #d0d0d0;
            }
            .device-list {
                background-color: #ffffff;
            }
            .device-row {
                padding: 12px;
                border-radius: 6px;
                color: #333333;
                min-height: 50px;
            }
            .device-row:hover {
                background-color: #f0f0f0;
            }
            .device-row:selected {
                background-color: #007AFF;
                color: #ffffff;
            }
            .device-row:selected .device-sublabel {
                color: rgba(255, 255, 255, 0.8);
            }
            .device-icon {
                font-size: 32px;
                margin-right: 12px;
            }
            .device-label {
                color: #333333;
                font-size: 13px;
            }
            .device-row:selected .device-label {
                color: #ffffff;
            }
            .device-sublabel {
                color: #666666;
                font-size: 11px;
            }
            .detail-panel {
                background-color: #f5f5f5;
                padding: 20px;
            }
            .section-title {
                color: #333333;
                font-size: 20px;
                font-weight: 600;
                margin-bottom: 20px;
            }
            .tabs {
                background-color: #e8e8e8;
                border-radius: 6px;
                padding: 4px;
            }
            .tab-button {
                background-color: transparent;
                border: none;
                color: #666666;
                padding: 6px 16px;
                border-radius: 4px;
                font-size: 12px;
            }
            .tab-button:checked {
                background-color: #ffffff;
                color: #333333;
                box-shadow: 0 1px 3px rgba(0, 0, 0, 0.1);
            }
            .info-label {
                color: #666666;
                font-size: 12px;
                min-width: 150px;
            }
            .info-combo {
                background-color: #ffffff;
                color: #333333;
                border: 1px solid #d0d0d0;
                border-radius: 6px;
                padding: 6px 12px;
                min-width: 300px;
            }
            .apply-button {
                background-color: #007AFF;
                color: #ffffff;
                border: none;
                border-radius: 6px;
                padding: 8px 24px;
                font-size: 13px;
                font-weight: 500;
            }
            .apply-button:hover {
                background-color: #0051D5;
            }
        """)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            css_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
        
        # Main container
        main_paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        self.add(main_paned)
        
        # Left sidebar
        sidebar = self.create_sidebar()
        main_paned.pack1(sidebar, False, False)
        main_paned.set_position(320)
        
        # Right detail panel
        self.detail_panel = self.create_detail_panel()
        main_paned.pack2(self.detail_panel, True, False)
        
        # Load devices
        self.devices = []
        self.selected_device = None
        self.load_devices()
        
    def create_sidebar(self):
        """Create left sidebar with device list"""
        sidebar_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        sidebar_box.get_style_context().add_class("device-sidebar")
        
        # Device list
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        
        self.device_listbox = Gtk.ListBox()
        self.device_listbox.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.device_listbox.get_style_context().add_class("device-list")
        self.device_listbox.connect("row-selected", self.on_device_selected)
        
        scrolled.add(self.device_listbox)
        sidebar_box.pack_start(scrolled, True, True, 0)
        
        return sidebar_box
    
    def create_detail_panel(self):
        """Create right detail panel"""
        detail_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        detail_box.get_style_context().add_class("detail-panel")
        
        # Scrolled window for content
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        
        self.detail_content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        scrolled.add(self.detail_content)
        
        detail_box.pack_start(scrolled, True, True, 0)
        
        return detail_box

    
    def load_devices(self):
        """Load audio devices from PipeWire"""
        self.devices = []
        
        # Get default sink and source
        default_sink = self.get_default_device('sink')
        default_source = self.get_default_device('source')
        
        # Load sinks (output devices)
        try:
            result = subprocess.run(['pactl', 'list', 'sinks'], 
                                  capture_output=True, text=True)
            self.parse_devices(result.stdout, 'sink', default_sink)
        except Exception as e:
            print(f"Error loading sinks: {e}")
        
        # Load sources (input devices) - skip monitors
        try:
            result = subprocess.run(['pactl', 'list', 'sources'], 
                                  capture_output=True, text=True)
            self.parse_devices(result.stdout, 'source', default_source)
        except Exception as e:
            print(f"Error loading sources: {e}")
        
        # Populate list
        self.populate_device_list()
    
    def get_default_device(self, device_type):
        """Get default sink or source name"""
        try:
            if device_type == 'sink':
                result = subprocess.run(['pactl', 'get-default-sink'], 
                                      capture_output=True, text=True)
            else:
                result = subprocess.run(['pactl', 'get-default-source'], 
                                      capture_output=True, text=True)
            return result.stdout.strip()
        except:
            return None
    
    def parse_devices(self, output, device_type, default_name):
        """Parse pactl output to extract device information"""
        current_device = {}
        sample_rates = []
        formats = []
        
        for line in output.split('\n'):
            line_stripped = line.strip()
            
            if (device_type == 'sink' and 'Sink #' in line) or \
               (device_type == 'source' and 'Source #' in line):
                if current_device.get('name'):
                    # Skip monitor sources
                    if device_type == 'source' and '.monitor' in current_device.get('name', ''):
                        current_device = {}
                        sample_rates = []
                        formats = []
                        continue
                    self.create_device_from_dict(current_device, device_type, sample_rates, formats, default_name)
                current_device = {}
                sample_rates = []
                formats = []
                
            elif line_stripped.startswith('Name:'):
                current_device['name'] = line_stripped.split('Name:')[1].strip()
            elif line_stripped.startswith('Description:'):
                current_device['description'] = line_stripped.split('Description:')[1].strip()
            elif line_stripped.startswith('Sample Specification:'):
                # Parse format like "s16le 2ch 44100Hz"
                spec = line_stripped.split('Sample Specification:')[1].strip()
                parts = spec.split()
                if len(parts) >= 3:
                    current_device['format'] = parts[0]
                    current_device['channels'] = parts[1].replace('ch', '')
                    current_device['sample_rate'] = parts[2].replace('Hz', '')
            elif 'Hz' in line_stripped and 'sample rates' not in line_stripped.lower():
                # Extract available sample rates
                rates = re.findall(r'(\d+)\s*Hz', line_stripped)
                sample_rates.extend(rates)
        
        # Add last device
        if current_device.get('name'):
            if not (device_type == 'source' and '.monitor' in current_device.get('name', '')):
                self.create_device_from_dict(current_device, device_type, sample_rates, formats, default_name)
    
    def create_device_from_dict(self, data, device_type, sample_rates, formats, default_name):
        """Create AudioDevice from parsed data"""
        channels = int(data.get('channels', '2'))
        
        # Default sample rates if none found
        if not sample_rates:
            sample_rates = ['44100', '48000', '88200', '96000', '192000']
        else:
            # Remove duplicates and sort
            sample_rates = sorted(list(set(sample_rates)), key=int)
        
        # Default formats
        if not formats:
            formats = ['s16le', 's24le', 's32le', 'f32le']
        
        device = AudioDevice(
            name=data.get('name', 'unknown'),
            description=data.get('description', 'Unknown Device'),
            device_type=device_type,
            channels=channels,
            sample_rates=sample_rates,
            formats=formats
        )
        device.is_default = (data.get('name') == default_name)
        device.current_rate = data.get('sample_rate', sample_rates[0])
        device.current_format = data.get('format', formats[0])
        
        self.devices.append(device)
    
    def populate_device_list(self):
        """Populate device list in sidebar"""
        for child in self.device_listbox.get_children():
            self.device_listbox.remove(child)
        
        # Separate output and input devices
        output_devices = [d for d in self.devices if d.device_type == 'sink']
        input_devices = [d for d in self.devices if d.device_type == 'source']
        
        # Sort devices: USB first, then others
        def is_usb_device(device):
            return 'usb' in device.name.lower() or 'usb' in device.description.lower()
        
        output_devices.sort(key=lambda d: (not is_usb_device(d), d.description))
        input_devices.sort(key=lambda d: (not is_usb_device(d), d.description))
        
        # Add output devices first
        for device in output_devices:
            row = self.create_device_row(device)
            self.device_listbox.add(row)
        
        # Add input devices
        for device in input_devices:
            row = self.create_device_row(device)
            self.device_listbox.add(row)
        
        self.device_listbox.show_all()
        
        # Select first device
        if self.devices:
            self.device_listbox.select_row(self.device_listbox.get_row_at_index(0))

    
    def create_device_row(self, device):
        """Create a device row for the sidebar"""
        row = Gtk.ListBoxRow()
        row.device = device
        row.get_style_context().add_class("device-row")
        
        hbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        hbox.set_margin_start(12)
        hbox.set_margin_end(12)
        hbox.set_margin_top(8)
        hbox.set_margin_bottom(8)
        
        # Icon
        icon_label = Gtk.Label()
        if device.device_type == 'sink':
            icon_label.set_markup('<span size="24000">🔊</span>')
        else:
            icon_label.set_markup('<span size="24000">🎤</span>')
        icon_label.get_style_context().add_class("device-icon")
        hbox.pack_start(icon_label, False, False, 0)
        
        # Text
        text_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        
        name_label = Gtk.Label(label=device.description)
        name_label.set_halign(Gtk.Align.START)
        name_label.set_line_wrap(True)
        name_label.set_max_width_chars(30)
        name_label.get_style_context().add_class("device-label")
        text_box.pack_start(name_label, False, False, 0)
        
        # Show correct channel info based on device type
        if device.device_type == 'sink':
            info_text = f"0 {_('inputs')}/{device.channels} {_('outputs')}"
        else:
            info_text = f"{device.channels} {_('inputs')}/0 {_('outputs')}"
        
        info_label = Gtk.Label(label=info_text)
        info_label.set_halign(Gtk.Align.START)
        info_label.get_style_context().add_class("device-sublabel")
        text_box.pack_start(info_label, False, False, 0)
        
        hbox.pack_start(text_box, True, True, 0)
        
        row.add(hbox)
        return row
    
    def on_device_selected(self, listbox, row):
        """Handle device selection"""
        if row:
            self.selected_device = row.device
            self.update_detail_panel()
    
    def update_detail_panel(self):
        """Update detail panel with selected device info"""
        # Clear existing content
        for child in self.detail_content.get_children():
            self.detail_content.remove(child)
        
        if not self.selected_device:
            return
        
        device = self.selected_device
        
        # Title
        title = Gtk.Label(label=device.description)
        title.set_halign(Gtk.Align.START)
        title.set_line_wrap(True)
        title.set_max_width_chars(50)
        title.get_style_context().add_class("section-title")
        self.detail_content.pack_start(title, False, False, 0)
        
        # Tabs (Input/Output)
        tabs_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=0)
        tabs_box.get_style_context().add_class("tabs")
        tabs_box.set_halign(Gtk.Align.START)
        tabs_box.set_margin_bottom(20)
        
        input_btn = Gtk.RadioButton(label=_('input'))
        input_btn.get_style_context().add_class("tab-button")
        tabs_box.pack_start(input_btn, False, False, 0)
        
        output_btn = Gtk.RadioButton(label=_('output'))
        output_btn.join_group(input_btn)
        output_btn.get_style_context().add_class("tab-button")
        
        # Set active tab based on device type
        if device.device_type == 'sink':
            output_btn.set_active(True)
        else:
            input_btn.set_active(True)
        
        tabs_box.pack_start(output_btn, False, False, 0)
        
        self.detail_content.pack_start(tabs_box, False, False, 0)
        
        # Configuration grid
        config_grid = Gtk.Grid()
        config_grid.set_column_spacing(20)
        config_grid.set_row_spacing(16)
        config_grid.set_margin_top(10)
        
        row_num = 0
        
        # Sample Rate
        rate_label = Gtk.Label(label=_('sample_rate'))
        rate_label.set_halign(Gtk.Align.END)
        rate_label.get_style_context().add_class("info-label")
        
        self.rate_combo = Gtk.ComboBoxText()
        self.rate_combo.get_style_context().add_class("info-combo")
        for rate in device.sample_rates:
            rate_hz = int(rate)
            if rate_hz >= 1000:
                display = f"{rate_hz / 1000:.1f} kHz"
            else:
                display = f"{rate_hz} Hz"
            self.rate_combo.append(rate, display)
        
        # Set current rate
        for i, rate in enumerate(device.sample_rates):
            if rate == device.current_rate:
                self.rate_combo.set_active(i)
                break
        if self.rate_combo.get_active() == -1:
            self.rate_combo.set_active(0)
        
        config_grid.attach(rate_label, 0, row_num, 1, 1)
        config_grid.attach(self.rate_combo, 1, row_num, 1, 1)
        row_num += 1
        
        # Buffer Size
        buffer_label = Gtk.Label(label=_('buffer_size'))
        buffer_label.set_halign(Gtk.Align.END)
        buffer_label.get_style_context().add_class("info-label")
        
        self.buffer_combo = Gtk.ComboBoxText()
        self.buffer_combo.get_style_context().add_class("info-combo")
        for size in ['32', '64', '128', '256', '512', '1024', '2048']:
            self.buffer_combo.append(size, f"{size} samples")
        self.buffer_combo.set_active(3)  # Default 256
        
        config_grid.attach(buffer_label, 0, row_num, 1, 1)
        config_grid.attach(self.buffer_combo, 1, row_num, 1, 1)
        row_num += 1
        
        # Format
        format_label = Gtk.Label(label=_('format'))
        format_label.set_halign(Gtk.Align.END)
        format_label.get_style_context().add_class("info-label")
        
        self.format_combo = Gtk.ComboBoxText()
        self.format_combo.get_style_context().add_class("info-combo")
        
        format_names = {
            's16le': '16-bit Integer',
            's24le': '24-bit Integer',
            's32le': '32-bit Integer',
            'f32le': '32-bit Float'
        }
        
        for fmt in device.formats:
            display_name = format_names.get(fmt, fmt)
            self.format_combo.append(fmt, f"{device.channels} {_('channels')} - {display_name}")
        
        # Set current format
        for i, fmt in enumerate(device.formats):
            if fmt == device.current_format:
                self.format_combo.set_active(i)
                break
        if self.format_combo.get_active() == -1:
            self.format_combo.set_active(0)
        
        config_grid.attach(format_label, 0, row_num, 1, 1)
        config_grid.attach(self.format_combo, 1, row_num, 1, 1)
        row_num += 1
        
        # Speaker Configuration (only for output devices)
        if device.device_type == 'sink':
            speaker_label = Gtk.Label(label=_('speaker_config'))
            speaker_label.set_halign(Gtk.Align.END)
            speaker_label.get_style_context().add_class("info-label")
            
            self.speaker_combo = Gtk.ComboBoxText()
            self.speaker_combo.get_style_context().add_class("info-combo")
            self.speaker_combo.append('mono', _('mono'))
            self.speaker_combo.append('stereo', _('stereo'))
            self.speaker_combo.append('surround-51', _('surround_51'))
            self.speaker_combo.append('surround-71', _('surround_71'))
            
            # Set based on channels
            if device.channels == 1:
                self.speaker_combo.set_active(0)
            elif device.channels == 2:
                self.speaker_combo.set_active(1)
            elif device.channels == 6:
                self.speaker_combo.set_active(2)
            elif device.channels == 8:
                self.speaker_combo.set_active(3)
            else:
                self.speaker_combo.set_active(1)
            
            config_grid.attach(speaker_label, 0, row_num, 1, 1)
            config_grid.attach(self.speaker_combo, 1, row_num, 1, 1)
            row_num += 1
        
        self.detail_content.pack_start(config_grid, False, False, 0)
        
        # Apply button
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        button_box.set_halign(Gtk.Align.END)
        button_box.set_margin_top(30)
        
        apply_btn = Gtk.Button(label=_('apply'))
        apply_btn.get_style_context().add_class("apply-button")
        apply_btn.connect("clicked", self.on_apply_clicked)
        button_box.pack_start(apply_btn, False, False, 0)
        
        self.detail_content.pack_start(button_box, False, False, 0)
        
        self.detail_content.show_all()
    
    def on_apply_clicked(self, button):
        """Apply configuration"""
        if not self.selected_device:
            return
        
        try:
            device = self.selected_device
            
            # Get selected values
            rate = self.rate_combo.get_active_id()
            buffer = self.buffer_combo.get_active_id()
            audio_format = self.format_combo.get_active_id()
            
            # Get speaker config if available
            speaker_config = None
            if device.device_type == 'sink' and hasattr(self, 'speaker_combo'):
                speaker_config = self.speaker_combo.get_active_id()
            
            # Create PipeWire configuration directory
            config_dir = os.path.expanduser("~/.config/pipewire/pipewire.conf.d")
            os.makedirs(config_dir, exist_ok=True)
            
            # Write global PipeWire configuration
            config_path = os.path.join(config_dir, "99-custom.conf")
            config_content = f"""# miloOS Audio Configuration
context.properties = {{
    default.clock.rate          = {rate}
    default.clock.quantum       = {buffer}
    default.clock.min-quantum   = {buffer}
    default.clock.max-quantum   = {buffer}
    default.format              = {audio_format}
}}

context.modules = [
    {{   name = libpipewire-module-rtkit
        args = {{
            nice.level   = -15
            rt.prio      = 88
            rt.time.soft = 200000
            rt.time.hard = 200000
        }}
        flags = [ ifexists nofail ]
    }}
]
"""
            
            with open(config_path, 'w') as f:
                f.write(config_content)
            
            # Create JACK configuration
            jack_config_dir = os.path.expanduser("~/.config/pipewire/jack.conf.d")
            os.makedirs(jack_config_dir, exist_ok=True)
            jack_config_path = os.path.join(jack_config_dir, "99-jack-custom.conf")
            jack_config_content = f"""# JACK configuration for miloOS
jack.properties = {{
    node.latency = {buffer}/{rate}
    jack.merge-monitor = true
    jack.short-name = true
}}
"""
            
            with open(jack_config_path, 'w') as f:
                f.write(jack_config_content)
            
            # Create WirePlumber configuration for device-specific settings
            wireplumber_dir = os.path.expanduser("~/.config/wireplumber/main.lua.d")
            os.makedirs(wireplumber_dir, exist_ok=True)
            wireplumber_path = os.path.join(wireplumber_dir, "99-device-config.lua")
            
            # Escape device name for Lua pattern matching
            device_name_escaped = device.name.replace('.', '%.')
            
            wireplumber_content = f"""-- Device-specific configuration for miloOS
alsa_monitor.rules = {{
  {{
    matches = {{
      {{
        {{ "node.name", "matches", "{device_name_escaped}" }},
      }},
    }},
    apply_properties = {{
      ["audio.format"] = "{audio_format}",
      ["audio.rate"] = {rate},
      ["api.alsa.period-size"] = {buffer},
      ["api.alsa.headroom"] = 1024,
    }},
  }},
}}
"""
            
            with open(wireplumber_path, 'w') as f:
                f.write(wireplumber_content)
            
            # Update device's current settings
            device.current_rate = rate
            device.current_format = audio_format
            
            # Restart PipeWire services
            subprocess.run(['systemctl', '--user', 'restart', 'pipewire'], 
                         capture_output=True)
            subprocess.run(['systemctl', '--user', 'restart', 'pipewire-pulse'], 
                         capture_output=True)
            subprocess.run(['systemctl', '--user', 'restart', 'wireplumber'], 
                         capture_output=True)
            
            # Wait a moment for services to restart
            GLib.timeout_add(1000, self.reload_after_apply)
            
            # Show success message
            dialog = Gtk.MessageDialog(
                transient_for=self,
                flags=0,
                message_type=Gtk.MessageType.INFO,
                buttons=Gtk.ButtonsType.OK,
                text=_('config_applied')
            )
            dialog.format_secondary_text(_('config_applied_msg'))
            dialog.run()
            dialog.destroy()
            
        except Exception as e:
            # Show error message
            dialog = Gtk.MessageDialog(
                transient_for=self,
                flags=0,
                message_type=Gtk.MessageType.ERROR,
                buttons=Gtk.ButtonsType.OK,
                text=_('error')
            )
            dialog.format_secondary_text(_('error_msg').format(str(e)))
            dialog.run()
            dialog.destroy()
    
    def reload_after_apply(self):
        """Reload devices after applying configuration"""
        self.load_devices()
        return False  # Don't repeat timeout

def main():
    win = AudioConfigWindow()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()
