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
        'output': 'Output',
        'input': 'Input',
        'output_volume': 'Output volume:',
        'input_level': 'Input level:',
        'sample_rate': 'Sample Rate:',
        'buffer_size': 'Buffer Size:',
        'format': 'Format:',
        'config_applied': 'Configuration Applied',
        'config_applied_msg': 'Audio settings have been applied successfully.',
        'error': 'Error',
        'error_msg': 'Failed to apply configuration:\n{}',
        'no_devices': 'No devices found'
    },
    'es': {
        'title': 'Sonido',
        'output': 'Salida',
        'input': 'Entrada',
        'output_volume': 'Volumen de salida:',
        'input_level': 'Nivel de entrada:',
        'sample_rate': 'Frecuencia de muestreo:',
        'buffer_size': 'Tamaño de buffer:',
        'format': 'Formato:',
        'config_applied': 'Configuración Aplicada',
        'config_applied_msg': 'La configuración de audio se ha aplicado correctamente.',
        'error': 'Error',
        'error_msg': 'Error al aplicar la configuración:\n{}',
        'no_devices': 'No se encontraron dispositivos'
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
        self.set_default_size(600, 550)
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
        
        self.current_output = None
        self.current_input = None
        
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
        
        # Output volume
        volume_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        volume_box.set_margin_top(8)
        volume_label = Gtk.Label(label=_('output_volume'))
        volume_label.set_size_request(120, -1)
        volume_label.set_halign(Gtk.Align.START)
        self.output_volume = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 1)
        self.output_volume.set_value(75)
        self.output_volume.set_draw_value(False)
        self.output_volume.connect("value-changed", self.on_output_volume_changed)
        volume_box.pack_start(volume_label, False, False, 0)
        volume_box.pack_start(self.output_volume, True, True, 0)
        main_box.pack_start(volume_box, False, False, 0)
        
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
        
        # Input level
        input_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        input_box.set_margin_top(8)
        input_label_vol = Gtk.Label(label=_('input_level'))
        input_label_vol.set_size_request(120, -1)
        input_label_vol.set_halign(Gtk.Align.START)
        self.input_volume = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 100, 1)
        self.input_volume.set_value(75)
        self.input_volume.set_draw_value(False)
        self.input_volume.connect("value-changed", self.on_input_volume_changed)
        input_box.pack_start(input_label_vol, False, False, 0)
        input_box.pack_start(self.input_volume, True, True, 0)
        main_box.pack_start(input_box, False, False, 0)
        
        # Separator
        sep2 = Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL)
        sep2.get_style_context().add_class("separator")
        sep2.set_margin_top(20)
        sep2.set_margin_bottom(20)
        main_box.pack_start(sep2, False, False, 0)
        
        # SETTINGS SECTION
        settings_grid = Gtk.Grid()
        settings_grid.set_column_spacing(15)
        settings_grid.set_row_spacing(12)
        
        # Sample Rate
        rate_label = Gtk.Label(label=_('sample_rate'))
        rate_label.set_halign(Gtk.Align.START)
        self.rate_combo = Gtk.ComboBoxText()
        for rate in ["44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz", "192000 Hz"]:
            self.rate_combo.append_text(rate)
        self.rate_combo.set_active(1)
        settings_grid.attach(rate_label, 0, 0, 1, 1)
        settings_grid.attach(self.rate_combo, 1, 0, 1, 1)
        
        # Buffer Size
        buffer_label = Gtk.Label(label=_('buffer_size'))
        buffer_label.set_halign(Gtk.Align.START)
        self.buffer_combo = Gtk.ComboBoxText()
        for buf in ["32 samples", "64 samples", "128 samples", "256 samples", "512 samples", "1024 samples"]:
            self.buffer_combo.append_text(buf)
        self.buffer_combo.set_active(3)
        settings_grid.attach(buffer_label, 0, 1, 1, 1)
        settings_grid.attach(self.buffer_combo, 1, 1, 1, 1)
        
        # Format
        format_label = Gtk.Label(label=_('format'))
        format_label.set_halign(Gtk.Align.START)
        self.format_combo = Gtk.ComboBoxText()
        for fmt in ["S16LE (16-bit)", "S24LE (24-bit)", "S32LE (32-bit)", "F32LE (32-bit float)"]:
            self.format_combo.append_text(fmt)
        self.format_combo.set_active(2)
        settings_grid.attach(format_label, 0, 2, 1, 1)
        settings_grid.attach(self.format_combo, 1, 2, 1, 1)
        
        main_box.pack_start(settings_grid, False, False, 0)
        
        # Buttons at bottom
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        button_box.set_margin_top(20)
        button_box.set_halign(Gtk.Align.END)
        
        cancel_btn = Gtk.Button(label="Cancel" if get_language() == 'en' else "Cancelar")
        cancel_btn.connect("clicked", lambda x: self.destroy())
        button_box.pack_start(cancel_btn, False, False, 0)
        
        apply_btn = Gtk.Button(label="Apply" if get_language() == 'en' else "Aplicar")
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
        """Handle output device selection"""
        if row:
            self.current_output = row.device_name
            # Update all rows to show correct selection
            for r in listbox.get_children():
                hbox = r.get_child()
                indicator = hbox.get_children()[0]
                indicator.set_text("●" if r == row else "○")
    
    def on_input_selected(self, listbox, row):
        """Handle input device selection"""
        if row:
            self.current_input = row.device_name
            # Update all rows to show correct selection
            for r in listbox.get_children():
                hbox = r.get_child()
                indicator = hbox.get_children()[0]
                indicator.set_text("●" if r == row else "○")
    
    def on_output_volume_changed(self, scale):
        """Handle output volume change"""
        if self.current_output:
            volume = int(scale.get_value())
            subprocess.run(['pactl', 'set-sink-volume', self.current_output, f'{volume}%'],
                         capture_output=True)
    
    def on_input_volume_changed(self, scale):
        """Handle input volume change"""
        if self.current_input:
            volume = int(scale.get_value())
            subprocess.run(['pactl', 'set-source-volume', self.current_input, f'{volume}%'],
                         capture_output=True)
    
    def apply_config(self):
        """Apply configuration when Apply button is clicked"""
        try:
            # Get selected devices
            output_device = self.current_output
            input_device = self.current_input
            
            # Get settings
            rate_text = self.rate_combo.get_active_text()
            buffer_text = self.buffer_combo.get_active_text()
            format_text = self.format_combo.get_active_text()
            
            # Extract numeric values
            rate = rate_text.split()[0] if rate_text else "48000"
            buffer = buffer_text.split()[0] if buffer_text else "256"
            
            # Format mapping
            format_map = {
                "S16LE (16-bit)": "S16LE",
                "S24LE (24-bit)": "S24LE",
                "S32LE (32-bit)": "S32LE",
                "F32LE (32-bit float)": "F32LE"
            }
            audio_format = format_map.get(format_text, "S32LE")
            
            # Create config directory
            config_dir = os.path.expanduser("~/.config/pipewire/pipewire.conf.d")
            os.makedirs(config_dir, exist_ok=True)
            
            # Write configuration
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
