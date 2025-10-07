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
        'clock_source': 'Clock Source:',
        'source': 'Source:',
        'format': 'Format:',
        'by_default': 'By default',
        'internal_speakers': 'Internal speakers',
        'channel_volume': 'Channel Volume',
        'value': 'Value',
        'db': 'dB',
        'silence': 'Silence',
        'main_stream': 'Main stream',
        'main': 'Main',
        'configure_speakers': 'Configure speakers...',
        'channels': 'channels',
        'bits': 'bits',
        'integer': 'Integer',
        'entries': 'entries',
        'outputs': 'outputs'
    },
    'es': {
        'title': 'Dispositivos de audio',
        'input': 'Entrada',
        'output': 'Salida',
        'clock_source': 'Fuente de reloj:',
        'source': 'Fuente:',
        'format': 'Formato:',
        'by_default': 'Por omisión',
        'internal_speakers': 'Altavoces internos',
        'channel_volume': 'Volumen del canal',
        'value': 'Valor',
        'db': 'dB',
        'silence': 'Silenc...',
        'main_stream': 'Secuencia principal',
        'main': 'Principal',
        'configure_speakers': 'Configurar altavoces...',
        'channels': 'canales',
        'bits': 'bits',
        'integer': 'Entero',
        'entries': 'entradas',
        'outputs': 'salidas'
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
    def __init__(self, name, description, device_type, channels_in, channels_out, sample_rate, format_str):
        self.name = name
        self.description = description
        self.device_type = device_type  # 'sink' or 'source'
        self.channels_in = channels_in
        self.channels_out = channels_out
        self.sample_rate = sample_rate
        self.format_str = format_str
        self.is_default = False
        
    def get_display_name(self):
        """Get display name with channel info"""
        if self.device_type == 'sink':
            return f"{self.description}\n{self.channels_in} {_('entries')}/{self.channels_out} {_('outputs')}"
        else:
            return f"{self.description}\n{self.channels_in} {_('entries')}/{self.channels_out} {_('outputs')}"

class AudioConfigWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title=_('title'))
        self.set_icon_name("audio-config")
        self.set_default_size(1000, 600)
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
            .info-row {
                padding: 8px 0;
            }
            .info-label {
                color: #666666;
                font-size: 12px;
            }
            .info-value {
                color: #333333;
                font-size: 12px;
            }
            .format-combo {
                background-color: #ffffff;
                color: #333333;
                border: 1px solid #d0d0d0;
                border-radius: 6px;
                padding: 6px 12px;
            }
            .volume-section {
                background-color: #ffffff;
                border: 1px solid #d0d0d0;
                border-radius: 8px;
                padding: 16px;
                margin-top: 16px;
            }
            .volume-header {
                color: #666666;
                font-size: 11px;
                margin-bottom: 12px;
            }
            .channel-row {
                padding: 8px 0;
            }
            .channel-label {
                color: #333333;
                font-size: 12px;
                min-width: 80px;
            }
            .volume-scale {
                background-color: transparent;
            }
            .volume-scale trough {
                background-color: #e0e0e0;
                border-radius: 3px;
                min-height: 6px;
            }
            .volume-scale highlight {
                background-color: #007AFF;
                border-radius: 3px;
            }
            .volume-scale slider {
                background-color: #ffffff;
                border: 1px solid #d0d0d0;
                border-radius: 50%;
                min-width: 16px;
                min-height: 16px;
                margin: -5px;
                box-shadow: 0 1px 3px rgba(0, 0, 0, 0.2);
            }
            .volume-value {
                color: #666666;
                font-size: 11px;
                min-width: 60px;
            }
            .toolbar {
                background-color: #f5f5f5;
                border-top: 1px solid #d0d0d0;
                padding: 8px;
            }
            .toolbar-button {
                background-color: transparent;
                border: none;
                color: #666666;
                padding: 6px;
                border-radius: 4px;
                font-size: 16px;
            }
            .toolbar-button:hover {
                background-color: #e8e8e8;
            }
            .configure-button {
                background-color: #ffffff;
                color: #333333;
                border: 1px solid #d0d0d0;
                border-radius: 6px;
                padding: 8px 16px;
                font-size: 12px;
            }
            .configure-button:hover {
                background-color: #f0f0f0;
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
        
        # Content area (sidebar + detail)
        content_paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        main_box.pack_start(content_paned, True, True, 0)
        
        # Left sidebar
        sidebar = self.create_sidebar()
        content_paned.pack1(sidebar, False, False)
        content_paned.set_position(350)
        
        # Right detail panel
        self.detail_panel = self.create_detail_panel()
        content_paned.pack2(self.detail_panel, True, False)
        
        # Bottom toolbar
        toolbar = self.create_toolbar()
        main_box.pack_start(toolbar, False, False, 0)
        
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
    
    def create_toolbar(self):
        """Create bottom toolbar"""
        toolbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        toolbar.get_style_context().add_class("toolbar")
        toolbar.set_margin_start(8)
        toolbar.set_margin_end(8)
        
        # Add button
        add_btn = Gtk.Button(label="+")
        add_btn.get_style_context().add_class("toolbar-button")
        toolbar.pack_start(add_btn, False, False, 0)
        
        # Remove button
        remove_btn = Gtk.Button(label="−")
        remove_btn.get_style_context().add_class("toolbar-button")
        toolbar.pack_start(remove_btn, False, False, 0)
        
        # Settings button
        settings_btn = Gtk.Button(label="⚙")
        settings_btn.get_style_context().add_class("toolbar-button")
        toolbar.pack_start(settings_btn, False, False, 0)
        
        # More button
        more_btn = Gtk.Button(label="⋯")
        more_btn.get_style_context().add_class("toolbar-button")
        toolbar.pack_start(more_btn, False, False, 0)
        
        return toolbar
    
    def load_devices(self):
        """Load audio devices from PipeWire"""
        self.devices = []
        
        # Load sinks (output devices)
        try:
            result = subprocess.run(['pactl', 'list', 'sinks'], 
                                  capture_output=True, text=True)
            self.parse_devices(result.stdout, 'sink')
        except Exception as e:
            print(f"Error loading sinks: {e}")
        
        # Load sources (input devices)
        try:
            result = subprocess.run(['pactl', 'list', 'sources'], 
                                  capture_output=True, text=True)
            self.parse_devices(result.stdout, 'source')
        except Exception as e:
            print(f"Error loading sources: {e}")
        
        # Populate list
        self.populate_device_list()
    
    def parse_devices(self, output, device_type):
        """Parse pactl output to extract device information"""
        current_device = {}
        
        for line in output.split('\n'):
            line = line.strip()
            
            if (device_type == 'sink' and 'Sink #' in line) or \
               (device_type == 'source' and 'Source #' in line):
                if current_device.get('name'):
                    # Skip monitor sources
                    if device_type == 'source' and '.monitor' in current_device.get('name', ''):
                        current_device = {}
                        continue
                    self.create_device_from_dict(current_device, device_type)
                current_device = {}
                
            elif line.startswith('Name:'):
                current_device['name'] = line.split('Name:')[1].strip()
            elif line.startswith('Description:'):
                current_device['description'] = line.split('Description:')[1].strip()
            elif line.startswith('Sample Specification:'):
                # Parse format like "s16le 2ch 44100Hz"
                spec = line.split('Sample Specification:')[1].strip()
                parts = spec.split()
                if len(parts) >= 3:
                    current_device['format'] = parts[0]
                    current_device['channels'] = parts[1].replace('ch', '')
                    current_device['sample_rate'] = parts[2].replace('Hz', '')
            elif line.startswith('State:'):
                state = line.split('State:')[1].strip()
                current_device['is_default'] = (state == 'RUNNING')
        
        # Add last device
        if current_device.get('name'):
            if not (device_type == 'source' and '.monitor' in current_device.get('name', '')):
                self.create_device_from_dict(current_device, device_type)
    
    def create_device_from_dict(self, data, device_type):
        """Create AudioDevice from parsed data"""
        channels = int(data.get('channels', '2'))
        device = AudioDevice(
            name=data.get('name', 'unknown'),
            description=data.get('description', 'Unknown Device'),
            device_type=device_type,
            channels_in=channels if device_type == 'source' else 0,
            channels_out=channels if device_type == 'sink' else channels,
            sample_rate=data.get('sample_rate', '48000'),
            format_str=data.get('format', 's16le')
        )
        device.is_default = data.get('is_default', False)
        self.devices.append(device)
    
    def populate_device_list(self):
        """Populate device list in sidebar"""
        for child in self.device_listbox.get_children():
            self.device_listbox.remove(child)
        
        for device in self.devices:
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
        name_label.get_style_context().add_class("device-label")
        text_box.pack_start(name_label, False, False, 0)
        
        info_text = f"{device.channels_in} {_('entries')}/{device.channels_out} {_('outputs')}"
        info_label = Gtk.Label(label=info_text)
        info_label.set_halign(Gtk.Align.START)
        info_label.get_style_context().add_class("device-sublabel")
        text_box.pack_start(info_label, False, False, 0)
        
        hbox.pack_start(text_box, True, True, 0)
        
        # Default indicator
        if device.is_default:
            default_icon = Gtk.Label(label="●")
            default_icon.get_style_context().add_class("device-icon")
            hbox.pack_start(default_icon, False, False, 0)
        
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
        output_btn.set_active(True)
        tabs_box.pack_start(output_btn, False, False, 0)
        
        self.detail_content.pack_start(tabs_box, False, False, 0)
        
        # Device info
        info_grid = Gtk.Grid()
        info_grid.set_column_spacing(20)
        info_grid.set_row_spacing(12)
        
        # Clock source
        self.add_info_row(info_grid, 0, _('clock_source'), _('by_default'))
        
        # Source
        self.add_info_row(info_grid, 1, _('source'), _('internal_speakers'))
        
        # Format
        format_text = f"{device.channels_out} {_('channels')} de {self.get_bit_depth(device.format_str)} {_('bits')} {_('integer')} {device.sample_rate} kHz"
        format_combo = Gtk.ComboBoxText()
        format_combo.append_text(format_text)
        format_combo.set_active(0)
        format_combo.get_style_context().add_class("format-combo")
        
        format_label = Gtk.Label(label=_('format'))
        format_label.set_halign(Gtk.Align.END)
        format_label.get_style_context().add_class("info-label")
        
        info_grid.attach(format_label, 0, 2, 1, 1)
        info_grid.attach(format_combo, 1, 2, 1, 1)
        
        self.detail_content.pack_start(info_grid, False, False, 0)
        
        # Volume section
        volume_section = self.create_volume_section(device)
        self.detail_content.pack_start(volume_section, False, False, 16)
        
        # Configure button
        configure_btn = Gtk.Button(label=_('configure_speakers'))
        configure_btn.get_style_context().add_class("configure-button")
        configure_btn.set_halign(Gtk.Align.END)
        configure_btn.set_margin_top(20)
        self.detail_content.pack_start(configure_btn, False, False, 0)
        
        self.detail_content.show_all()
    
    def add_info_row(self, grid, row, label_text, value_text):
        """Add an info row to the grid"""
        label = Gtk.Label(label=label_text)
        label.set_halign(Gtk.Align.END)
        label.get_style_context().add_class("info-label")
        
        value = Gtk.Label(label=value_text)
        value.set_halign(Gtk.Align.START)
        value.get_style_context().add_class("info-value")
        
        grid.attach(label, 0, row, 1, 1)
        grid.attach(value, 1, row, 1, 1)
    
    def create_volume_section(self, device):
        """Create volume control section"""
        section = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        section.get_style_context().add_class("volume-section")
        
        # Header with tabs
        header_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=20)
        header_box.set_margin_bottom(12)
        
        header_label = Gtk.Label(label=_('channel_volume').upper())
        header_label.get_style_context().add_class("volume-header")
        header_box.pack_start(header_label, False, False, 0)
        
        # Tabs
        value_btn = Gtk.RadioButton(label=_('value'))
        value_btn.get_style_context().add_class("tab-button")
        header_box.pack_start(value_btn, False, False, 0)
        
        db_btn = Gtk.RadioButton(label=_('db'))
        db_btn.join_group(value_btn)
        db_btn.get_style_context().add_class("tab-button")
        header_box.pack_start(db_btn, False, False, 0)
        
        silence_btn = Gtk.RadioButton(label=_('silence'))
        silence_btn.join_group(value_btn)
        silence_btn.get_style_context().add_class("tab-button")
        header_box.pack_start(silence_btn, False, False, 0)
        
        section.pack_start(header_box, False, False, 0)
        
        # Expandable section
        expander = Gtk.Expander(label=_('main_stream'))
        expander.set_expanded(True)
        
        expander_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        expander_box.set_margin_top(12)
        
        # Main volume
        main_row = self.create_volume_row(_('main'), 0.254, -31.5, True)
        expander_box.pack_start(main_row, False, False, 0)
        
        # Channel volumes
        channels = device.channels_out if device.device_type == 'sink' else device.channels_in
        for i in range(channels):
            channel_row = self.create_volume_row(str(i + 1), 0.254, -31.5, False)
            expander_box.pack_start(channel_row, False, False, 0)
        
        expander.add(expander_box)
        section.pack_start(expander, False, False, 0)
        
        return section
    
    def create_volume_row(self, label_text, value, db_value, is_main):
        """Create a volume control row"""
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        row.get_style_context().add_class("channel-row")
        
        # Label
        label = Gtk.Label(label=label_text)
        label.set_halign(Gtk.Align.START)
        label.get_style_context().add_class("channel-label")
        if is_main:
            label.set_markup(f"<b>{label_text}</b>")
        row.pack_start(label, False, False, 0)
        
        # Volume slider
        scale = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 1, 0.01)
        scale.set_value(value)
        scale.set_draw_value(False)
        scale.get_style_context().add_class("volume-scale")
        scale.set_hexpand(True)
        row.pack_start(scale, True, True, 0)
        
        # Value label
        value_label = Gtk.Label(label=f"{value:.3f}")
        value_label.get_style_context().add_class("volume-value")
        value_label.set_width_chars(6)
        row.pack_start(value_label, False, False, 0)
        
        # dB label
        db_label = Gtk.Label(label=f"{db_value:.1f}")
        db_label.get_style_context().add_class("volume-value")
        db_label.set_width_chars(6)
        row.pack_start(db_label, False, False, 0)
        
        # Mute checkbox
        mute_check = Gtk.CheckButton()
        row.pack_start(mute_check, False, False, 0)
        
        # Connect scale to update labels
        scale.connect("value-changed", lambda s: value_label.set_text(f"{s.get_value():.3f}"))
        
        return row
    
    def get_bit_depth(self, format_str):
        """Extract bit depth from format string"""
        if 's16' in format_str.lower():
            return '16'
        elif 's24' in format_str.lower():
            return '24'
        elif 's32' in format_str.lower() or 'f32' in format_str.lower():
            return '32'
        return '16'

def main():
    win = AudioConfigWindow()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()
