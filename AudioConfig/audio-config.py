#!/usr/bin/env python3
"""
miloOS Audio Configuration Tool
macOS-inspired GUI for configuring PipeWire audio parameters
"""

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk
import subprocess
import os
import locale

# Translations
TRANSLATIONS = {
    'en': {
        'title': 'Sound',
        'subtitle': 'Audio Server Configuration',
        'sample_rate': 'Sample Rate:',
        'buffer_size': 'Buffer Size:',
        'format': 'Format:',
        'config_applied': 'Configuration Applied',
        'config_applied_msg': 'Audio settings have been applied successfully.\nPipeWire has been restarted.',
        'error': 'Error',
        'error_msg': 'Failed to apply configuration:\n{}',
        'apply': 'Apply',
        'cancel': 'Cancel',
        'note': 'Note: Use the XFCE audio plugin to select input/output devices.'
    },
    'es': {
        'title': 'Sonido',
        'subtitle': 'Configuración de servidor de audio',
        'sample_rate': 'Frecuencia de muestreo:',
        'buffer_size': 'Tamaño de buffer:',
        'format': 'Formato:',
        'config_applied': 'Configuración Aplicada',
        'config_applied_msg': 'La configuración de audio se ha aplicado correctamente.\nPipeWire se ha reiniciado.',
        'error': 'Error',
        'error_msg': 'Error al aplicar la configuración:\n{}',
        'apply': 'Aplicar',
        'cancel': 'Cancelar',
        'note': 'Nota: Usa el plugin de audio de XFCE para seleccionar dispositivos de entrada/salida.'
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
        self.set_default_size(500, 350)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_resizable(False)
        
        # Apply macOS-like styling
        css_provider = Gtk.CssProvider()
        css_provider.load_from_data(b"""
            window {
                background-color: #f5f5f5;
            }
            .section-label {
                font-weight: 600;
                font-size: 13px;
                color: #333;
                padding: 8px 0;
            }
            .note-label {
                font-size: 11px;
                color: #666;
                font-style: italic;
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
        title_label.set_margin_bottom(20)
        main_box.pack_start(title_label, False, False, 0)
        
        # Settings Grid
        settings_grid = Gtk.Grid()
        settings_grid.set_column_spacing(15)
        settings_grid.set_row_spacing(15)
        settings_grid.set_halign(Gtk.Align.CENTER)
        
        # Sample Rate
        rate_label = Gtk.Label(label=_('sample_rate'))
        rate_label.set_halign(Gtk.Align.END)
        self.rate_combo = Gtk.ComboBoxText()
        for rate in ["44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz", "192000 Hz"]:
            self.rate_combo.append_text(rate)
        self.rate_combo.set_active(1)  # Default: 48000 Hz
        settings_grid.attach(rate_label, 0, 0, 1, 1)
        settings_grid.attach(self.rate_combo, 1, 0, 1, 1)
        
        # Buffer Size
        buffer_label = Gtk.Label(label=_('buffer_size'))
        buffer_label.set_halign(Gtk.Align.END)
        self.buffer_combo = Gtk.ComboBoxText()
        for buf in ["32 samples", "64 samples", "128 samples", "256 samples", "512 samples", "1024 samples"]:
            self.buffer_combo.append_text(buf)
        self.buffer_combo.set_active(3)  # Default: 256 samples
        settings_grid.attach(buffer_label, 0, 1, 1, 1)
        settings_grid.attach(self.buffer_combo, 1, 1, 1, 1)
        
        # Format
        format_label = Gtk.Label(label=_('format'))
        format_label.set_halign(Gtk.Align.END)
        self.format_combo = Gtk.ComboBoxText()
        for fmt in ["S16LE (16-bit)", "S24LE (24-bit)", "S32LE (32-bit)", "F32LE (32-bit float)"]:
            self.format_combo.append_text(fmt)
        self.format_combo.set_active(2)  # Default: S32LE
        settings_grid.attach(format_label, 0, 2, 1, 1)
        settings_grid.attach(self.format_combo, 1, 2, 1, 1)
        
        main_box.pack_start(settings_grid, True, True, 0)
        
        # Note label
        note_label = Gtk.Label(label=_('note'))
        note_label.get_style_context().add_class("note-label")
        note_label.set_line_wrap(True)
        note_label.set_max_width_chars(50)
        note_label.set_margin_top(20)
        note_label.set_margin_bottom(10)
        main_box.pack_start(note_label, False, False, 0)
        
        # Buttons at bottom
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        button_box.set_margin_top(10)
        button_box.set_halign(Gtk.Align.END)
        
        cancel_btn = Gtk.Button(label=_('cancel'))
        cancel_btn.connect("clicked", lambda x: self.destroy())
        button_box.pack_start(cancel_btn, False, False, 0)
        
        apply_btn = Gtk.Button(label=_('apply'))
        apply_btn.get_style_context().add_class("suggested-action")
        apply_btn.connect("clicked", lambda x: self.apply_config())
        button_box.pack_start(apply_btn, False, False, 0)
        
        main_box.pack_start(button_box, False, False, 0)
        
        # Load current config
        self.load_config()
    
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
                            model = self.rate_combo.get_model()
                            for i in range(len(model)):
                                if rate in model[i][0]:
                                    self.rate_combo.set_active(i)
                                    break
                        
                        if 'default.clock.quantum' in line and '=' in line:
                            buffer = line.split('=')[1].strip()
                            buffer_text = f"{buffer} samples"
                            model = self.buffer_combo.get_model()
                            for i in range(len(model)):
                                if buffer in model[i][0]:
                                    self.buffer_combo.set_active(i)
                                    break
            except Exception as e:
                print(f"Error loading config: {e}")
    
    def apply_config(self):
        """Apply configuration when Apply button is clicked"""
        try:
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
            
            # Write PipeWire configuration
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
