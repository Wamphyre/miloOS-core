#!/usr/bin/env python3
"""
miloOS Audio Configuration Tool
macOS-inspired GUI for configuring PipeWire audio parameters
"""

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk, GLib
import subprocess
import os
import locale

# Translations
TRANSLATIONS = {
    'en': {
        'title': 'Sound',
        'subtitle': 'AudioConfig - Audio Server Configuration',
        'output': 'Output',
        'input': 'Input',
        'output_settings': 'Output Device Settings',
        'input_settings': 'Input Device Settings',
        'sample_rate': 'Sample Rate:',
        'buffer_size': 'Buffer Size:',
        'format': 'Format:',
        'config_applied': 'Configuration Applied',
        'config_applied_msg': 'Audio settings have been applied successfully.',
        'error': 'Error',
        'error_msg': 'Failed to apply configuration:\n{}',
        'no_devices': 'No devices found',
        'apply': 'Apply',
        'cancel': 'Cancel'
    },
    'es': {
        'title': 'Sonido',
        'subtitle': 'Configuración de servidor de audio',
        'output': 'Salida',
        'input': 'Entrada',
        'output_settings': 'Configuración del Dispositivo de Salida',
        'input_settings': 'Configuración del Dispositivo de Entrada',
        'sample_rate': 'Frecuencia de muestreo:',
        'buffer_size': 'Tamaño de buffer:',
        'format': 'Formato:',
        'config_applied': 'Configuración Aplicada',
        'config_applied_msg': 'La configuración de audio se ha aplicado correctamente.',
        'error': 'Error',
        'error_msg': 'Error al aplicar la configuración:\n{}',
        'no_devices': 'No se encontraron dispositivos',
        'apply': 'Aplicar',
        'cancel': 'Cancelar'
    }
}

def get_language():
    """Detect system language"""
    try:
        lang = locale.getdefaultlocale()[0]
        if lang and lang.startswith('es'):
            return 'es'
    except:
        pass
    return 'en'

def _(key):
    """Get translated string"""
    lang = get_language()
    return TRANSLATIONS.get(lang, TRANSLATIONS['en']).get(key, key)

class AudioConfigWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title=_('title'))
        self.set_wmclass("Audio-config", "Audio-config")
        self.set_icon_name("audio-config")
        self.set_default_size(600, 750)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_resizable(False)
        
        # Apply macOS-like styling
        css_provider = Gtk.CssProvider()
        css_provider.load_from_data(b"""
            window {
                background-color: #f5f5f5;
            }
            .device-list {
                background-color: white;
                border: 1px solid #d0d0d0;
                border-radius: 6px;
            }
            .device-row {
                padding: 8px 12px;
                border-radius: 4px;
            }
            .device-row:hover {
                background-color: #f0f0f0;
            }
            .device-row:selected {
                background-color: #007AFF;
                color: white;
            }
            .section-label {
                font-weight: 600;
                font-size: 13px;
                color: #333;
                padding: 8px 0;
            }
            .separator {
                background-color: #d0d0d0;
                min-height: 1px;
            }
        """)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            css_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
        
        # Main container with padding
        main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        main_box.set_margin_top(15)
        main_box.set_margin_bottom(15)
        main_box.set_margin_start(20)
        main_box.set_margin_end(20)
        self.add(main_box)
        
        # Title/Subtitle
        title_label = Gtk.Label()
        title_label.set_markup(f"<span size='11000' weight='600'>{_('subtitle')}</span>")
        title_label.set_halign(Gtk.Align.START)
        title_label.set_margin_bottom(15)
        main_box.pack_start(title_label, False, False, 0)
        
        self.current_output = None
        self.current_input = None
        self.output_configs = {}  # Store config per device
        self.input_configs = {}   # Store config per device
        
        # OUTPUT SECTION
        output_label = Gtk.Label(label=_('output'))
        output_label.get_style_context().add_class("section-label")
        output_label.set_halign(Gtk.Align.START)
        main_box.pack_start(output_label, False, False, 0)
        
        # Output devices list
        self.output_listbox = Gtk.ListBox()
        self.output_listbox.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.output_listbox.get_style_context().add_class("device-list")
        self.output_listbox.set_size_request(-1, 100)
        main_box.pack_start(self.output_listbox, False, False, 5)
        
        # Separator
        sep1 = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        sep1.get_style_context().add_class("separator")
        sep1.set_margin_top(20)
        sep1.set_margin_bottom(20)
        main_box.pack_start(sep1, False, False, 0)
        
        # INPUT SECTION
        input_label = Gtk.Label(label=_('input'))
        input_label.get_style_context().add_class("section-label")
        input_label.set_halign(Gtk.Align.START)
        main_box.pack_start(input_label, False, False, 0)
        
        # Input devices list
        self.input_listbox = Gtk.ListBox()
        self.input_listbox.set_selection_mode(Gtk.SelectionMode.SINGLE)
        self.input_listbox.get_style_context().add_class("device-list")
        self.input_listbox.set_size_request(-1, 100)
        main_box.pack_start(self.input_listbox, False, False, 5)
        
        # Separator
        sep2 = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        sep2.get_style_context().add_class("separator")
        sep2.set_margin_top(20)
        sep2.set_margin_bottom(20)
        main_box.pack_start(sep2, False, False, 0)
        
        # OUTPUT DEVICE SETTINGS
        output_settings_label = Gtk.Label(label=_('output_settings'))
        output_settings_label.get_style_context().add_class("section-label")
        output_settings_label.set_halign(Gtk.Align.START)
        main_box.pack_start(output_settings_label, False, False, 0)
        
        output_grid = Gtk.Grid()
        output_grid.set_column_spacing(15)
        output_grid.set_row_spacing(12)
        output_grid.set_margin_top(8)
        
        # Output Sample Rate
        out_rate_label = Gtk.Label(label=_('sample_rate'))
        out_rate_label.set_halign(Gtk.Align.START)
        self.output_rate_combo = Gtk.ComboBoxText()
        for rate in ["44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz", "192000 Hz"]:
            self.output_rate_combo.append_text(rate)
        self.output_rate_combo.set_active(1)
        output_grid.attach(out_rate_label, 0, 0, 1, 1)
        output_grid.attach(self.output_rate_combo, 1, 0, 1, 1)
        
        # Output Buffer Size
        out_buffer_label = Gtk.Label(label=_('buffer_size'))
        out_buffer_label.set_halign(Gtk.Align.START)
        self.output_buffer_combo = Gtk.ComboBoxText()
        for buf in ["32 samples", "64 samples", "128 samples", "256 samples", "512 samples", "1024 samples"]:
            self.output_buffer_combo.append_text(buf)
        self.output_buffer_combo.set_active(3)
        output_grid.attach(out_buffer_label, 0, 1, 1, 1)
        output_grid.attach(self.output_buffer_combo, 1, 1, 1, 1)
        
        # Output Format
        out_format_label = Gtk.Label(label=_('format'))
        out_format_label.set_halign(Gtk.Align.START)
        self.output_format_combo = Gtk.ComboBoxText()
        for fmt in ["S16LE (16-bit)", "S24LE (24-bit)", "S32LE (32-bit)", "F32LE (32-bit float)"]:
            self.output_format_combo.append_text(fmt)
        self.output_format_combo.set_active(2)
        output_grid.attach(out_format_label, 0, 2, 1, 1)
        output_grid.attach(self.output_format_combo, 1, 2, 1, 1)
        
        main_box.pack_start(output_grid, False, False, 0)
        
        # Separator
        sep3 = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        sep3.get_style_context().add_class("separator")
        sep3.set_margin_top(20)
        sep3.set_margin_bottom(20)
        main_box.pack_start(sep3, False, False, 0)
        
        # INPUT DEVICE SETTINGS
        input_settings_label = Gtk.Label(label=_('input_settings'))
        input_settings_label.get_style_context().add_class("section-label")
        input_settings_label.set_halign(Gtk.Align.START)
        main_box.pack_start(input_settings_label, False, False, 0)
        
        input_grid = Gtk.Grid()
        input_grid.set_column_spacing(15)
        input_grid.set_row_spacing(12)
        input_grid.set_margin_top(8)
        
        # Input Sample Rate
        in_rate_label = Gtk.Label(label=_('sample_rate'))
        in_rate_label.set_halign(Gtk.Align.START)
        self.input_rate_combo = Gtk.ComboBoxText()
        for rate in ["44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz", "192000 Hz"]:
            self.input_rate_combo.append_text(rate)
        self.input_rate_combo.set_active(1)
        input_grid.attach(in_rate_label, 0, 0, 1, 1)
        input_grid.attach(self.input_rate_combo, 1, 0, 1, 1)
        
        # Input Buffer Size
        in_buffer_label = Gtk.Label(label=_('buffer_size'))
        in_buffer_label.set_halign(Gtk.Align.START)
        self.input_buffer_combo = Gtk.ComboBoxText()
        for buf in ["32 samples", "64 samples", "128 samples", "256 samples", "512 samples", "1024 samples"]:
            self.input_buffer_combo.append_text(buf)
        self.input_buffer_combo.set_active(3)
        input_grid.attach(in_buffer_label, 0, 1, 1, 1)
        input_grid.attach(self.input_buffer_combo, 1, 1, 1, 1)
        
        # Input Format
        in_format_label = Gtk.Label(label=_('format'))
        in_format_label.set_halign(Gtk.Align.START)
        self.input_format_combo = Gtk.ComboBoxText()
        for fmt in ["S16LE (16-bit)", "S24LE (24-bit)", "S32LE (32-bit)", "F32LE (32-bit float)"]:
            self.input_format_combo.append_text(fmt)
        self.input_format_combo.set_active(2)
        input_grid.attach(in_format_label, 0, 2, 1, 1)
        input_grid.attach(self.input_format_combo, 1, 2, 1, 1)
        
        main_box.pack_start(input_grid, False, False, 0)
        
        # Buttons at bottom
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        button_box.set_margin_top(20)
        button_box.set_halign(Gtk.Align.END)
        
        cancel_btn = Gtk.Button(label=_('cancel'))
        cancel_btn.connect("clicked", lambda x: self.destroy())
        button_box.pack_start(cancel_btn, False, False, 0)
        
        apply_btn = Gtk.Button(label=_('apply'))
        apply_btn.get_style_context().add_class("suggested-action")
        apply_btn.connect("clicked", lambda x: self.apply_config())
        button_box.pack_start(apply_btn, False, False, 0)
        
        main_box.pack_start(button_box, False, False, 0)
        
        # Load devices and config
        self.load_devices()
        self.load_config()
        
        # Connect selection change
        self.output_listbox.connect("row-selected", self.on_output_selected)
        self.input_listbox.connect("row-selected", self.on_input_selected)
    
    def load_devices(self):
        """Load available audio devices in macOS style"""
        try:
            # Get sinks (output devices)
            result = subprocess.run(['pactl', 'list', 'sinks'], 
                                  capture_output=True, text=True)
            
            current_sink = None
            sink_name = None
            sink_desc = None
            
            for line in result.stdout.split('\n'):
                if 'Sink #' in line:
                    if sink_name and sink_desc:
                        self._add_device_row(self.output_listbox, sink_desc, sink_name, current_sink)
                    current_sink = None
                    sink_name = None
                    sink_desc = None
                elif 'Name:' in line:
                    sink_name = line.split('Name:')[1].strip()
                elif 'Description:' in line:
                    sink_desc = line.split('Description:')[1].strip()
                elif 'State: RUNNING' in line:
                    current_sink = sink_name
            
            if sink_name and sink_desc:
                self._add_device_row(self.output_listbox, sink_desc, sink_name, current_sink)
            
            # Get sources (input devices)
            result = subprocess.run(['pactl', 'list', 'sources'], 
                                  capture_output=True, text=True)
            
            current_source = None
            source_name = None
            source_desc = None
            
            for line in result.stdout.split('\n'):
                if 'Source #' in line:
                    if source_name and source_desc and '.monitor' not in source_name:
                        self._add_device_row(self.input_listbox, source_desc, source_name, current_source)
                    current_source = None
                    source_name = None
                    source_desc = None
                elif 'Name:' in line:
                    source_name = line.split('Name:')[1].strip()
                elif 'Description:' in line:
                    source_desc = line.split('Description:')[1].strip()
                elif 'State: RUNNING' in line:
                    current_source = source_name
            
            if source_name and source_desc and '.monitor' not in source_name:
                self._add_device_row(self.input_listbox, source_desc, source_name, current_source)
                
        except Exception as e:
            print(f"Error loading devices: {e}")
    
    def _add_device_row(self, listbox, description, name, is_current):
        """Add a device row to listbox"""
        row = Gtk.ListBoxRow()
        row.device_name = name
        row.get_style_context().add_class("device-row")
        
        hbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        hbox.set_margin_top(4)
        hbox.set_margin_bottom(4)
        hbox.set_margin_start(8)
        hbox.set_margin_end(8)
        
        # Radio button indicator (visual only)
        if is_current == name:
            indicator = Gtk.Label(label="●")
            listbox.select_row(row)
        else:
            indicator = Gtk.Label(label="○")
        indicator.set_size_request(20, -1)
        hbox.pack_start(indicator, False, False, 0)
        
        # Device name
        label = Gtk.Label(label=description)
        label.set_halign(Gtk.Align.START)
        hbox.pack_start(label, True, True, 0)
        
        row.add(hbox)
        listbox.add(row)
        row.show_all()
    
    def load_config(self):
        """Load current PipeWire configuration"""
        config_path = os.path.expanduser("~/.config/pipewire/pipewire.conf.d/99-custom.conf")
        if os.path.exists(config_path):
            try:
                with open(config_path, 'r') as f:
                    content = f.read()
                    
                    for line in content.split('\n'):
                        if 'default.clock.rate' in line and '=' in line:
                            rate = line.split('=')[1].strip()
                            rate_text = f"{rate} Hz"
                            for i, text in enumerate([self.rate_combo.get_model()[j][0] for j in range(len(self.rate_combo.get_model()))]):
                                if rate in text:
                                    self.rate_combo.set_active(i)
                                    break
                        
                        if 'default.clock.quantum' in line and '=' in line:
                            buffer = line.split('=')[1].strip()
                            buffer_text = f"{buffer} samples"
                            for i, text in enumerate([self.buffer_combo.get_model()[j][0] for j in range(len(self.buffer_combo.get_model()))]):
                                if buffer in text:
                                    self.buffer_combo.set_active(i)
                                    break
            except Exception as e:
                print(f"Error loading config: {e}")
    
    def on_output_selected(self, listbox, row):
        """Handle output device selection and load its configuration"""
        if row:
            # Save current output config before switching
            if self.current_output:
                self.save_device_config(self.current_output, 'output')
            
            self.current_output = row.device_name
            
            # Update visual selection
            for r in listbox.get_children():
                hbox = r.get_child()
                indicator = hbox.get_children()[0]
                indicator.set_text("●" if r == row else "○")
            
            # Load config for this device
            self.load_device_config(row.device_name, 'output')
    
    def on_input_selected(self, listbox, row):
        """Handle input device selection and load its configuration"""
        if row:
            # Save current input config before switching
            if self.current_input:
                self.save_device_config(self.current_input, 'input')
            
            self.current_input = row.device_name
            
            # Update visual selection
            for r in listbox.get_children():
                hbox = r.get_child()
                indicator = hbox.get_children()[0]
                indicator.set_text("●" if r == row else "○")
            
            # Load config for this device
            self.load_device_config(row.device_name, 'input')
    
    def save_device_config(self, device_name, device_type):
        """Save current UI settings for a device"""
        if device_type == 'output':
            self.output_configs[device_name] = {
                'rate': self.output_rate_combo.get_active(),
                'buffer': self.output_buffer_combo.get_active(),
                'format': self.output_format_combo.get_active()
            }
        else:
            self.input_configs[device_name] = {
                'rate': self.input_rate_combo.get_active(),
                'buffer': self.input_buffer_combo.get_active(),
                'format': self.input_format_combo.get_active()
            }
    
    def load_device_config(self, device_name, device_type):
        """Load saved settings for a device or detect defaults"""
        if device_type == 'output':
            if device_name in self.output_configs:
                config = self.output_configs[device_name]
                self.output_rate_combo.set_active(config['rate'])
                self.output_buffer_combo.set_active(config['buffer'])
                self.output_format_combo.set_active(config['format'])
            else:
                # Detect device capabilities
                self.detect_device_capabilities(device_name, 'sink')
        else:
            if device_name in self.input_configs:
                config = self.input_configs[device_name]
                self.input_rate_combo.set_active(config['rate'])
                self.input_buffer_combo.set_active(config['buffer'])
                self.input_format_combo.set_active(config['format'])
            else:
                # Detect device capabilities
                self.detect_device_capabilities(device_name, 'source')
    
    def detect_device_capabilities(self, device_name, device_type):
        """Detect and set optimal settings for device"""
        try:
            cmd = ['pactl', 'list', 'sinks' if device_type == 'sink' else 'sources']
            result = subprocess.run(cmd, capture_output=True, text=True)
            
            in_device = False
            for line in result.stdout.split('\n'):
                if f'Name: {device_name}' in line:
                    in_device = True
                elif in_device and 'Sample Specification:' in line:
                    # Parse current sample rate
                    if '48000Hz' in line or '48kHz' in line:
                        if device_type == 'sink':
                            self.output_rate_combo.set_active(1)  # 48000 Hz
                        else:
                            self.input_rate_combo.set_active(1)
                    elif '44100Hz' in line or '44.1kHz' in line:
                        if device_type == 'sink':
                            self.output_rate_combo.set_active(0)  # 44100 Hz
                        else:
                            self.input_rate_combo.set_active(0)
                    break
        except Exception as e:
            print(f"Error detecting capabilities: {e}")
    

    
    def apply_config(self):
        """Apply configuration when Apply button is clicked"""
        try:
            # Get selected devices
            output_device = self.current_output
            input_device = self.current_input
            
            # Save current device configs
            if self.current_output:
                self.save_device_config(self.current_output, 'output')
            if self.current_input:
                self.save_device_config(self.current_input, 'input')
            
            # Get output settings
            out_rate_text = self.output_rate_combo.get_active_text()
            out_buffer_text = self.output_buffer_combo.get_active_text()
            out_format_text = self.output_format_combo.get_active_text()
            
            # Get input settings
            in_rate_text = self.input_rate_combo.get_active_text()
            in_buffer_text = self.input_buffer_combo.get_active_text()
            
            # Extract numeric values (use output as default for global config)
            rate = out_rate_text.split()[0] if out_rate_text else "48000"
            buffer = out_buffer_text.split()[0] if out_buffer_text else "256"
            
            # Format mapping
            format_map = {
                "S16LE (16-bit)": "S16LE",
                "S24LE (24-bit)": "S24LE",
                "S32LE (32-bit)": "S32LE",
                "F32LE (32-bit float)": "F32LE"
            }
            audio_format = format_map.get(out_format_text, "S32LE")
            
            # Create config directory
            config_dir = os.path.expanduser("~/.config/pipewire/pipewire.conf.d")
            os.makedirs(config_dir, exist_ok=True)
            
            # Write PipeWire configuration
            config_path = os.path.join(config_dir, "99-custom.conf")
            config_content = f"""# miloOS Audio Configuration
context.properties = {{
    default.clock.rate          = {rate}
    default.clock.quantum       = {buffer}
    default.clock.min-quantum   = {buffer}
    default.clock.max-quantum   = {buffer}
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
            
            # Write JACK configuration
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
            
            # Set default devices
            if output_device:
                subprocess.run(['pactl', 'set-default-sink', output_device], 
                             capture_output=True)
            if input_device:
                subprocess.run(['pactl', 'set-default-source', input_device], 
                             capture_output=True)
            
            # Restart PipeWire
            subprocess.run(['systemctl', '--user', 'restart', 'pipewire'], 
                         capture_output=True)
            subprocess.run(['systemctl', '--user', 'restart', 'pipewire-pulse'], 
                         capture_output=True)
            
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

def main():
    win = AudioConfigWindow()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()
