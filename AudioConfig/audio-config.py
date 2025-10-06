#!/usr/bin/env python3
"""
miloOS Audio Configuration Tool
Simple GUI for configuring PipeWire audio parameters
"""

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib
import subprocess
import json
import os
import locale

# Translations
TRANSLATIONS = {
    'en': {
        'title': 'miloOS Audio Configuration',
        'audio_config': 'Audio Configuration',
        'sample_rate': 'Sample Rate (Hz)',
        'buffer_size': 'Buffer Size (samples)',
        'output_device': 'Default Output Device',
        'input_device': 'Default Input Device',
        'apply': 'Apply',
        'close': 'Close',
        'config_applied': 'Configuration Applied',
        'config_applied_msg': 'Audio configuration has been saved and applied.\nPipeWire has been restarted.',
        'restarting': 'Restarting PipeWire...',
        'error': 'Error',
        'error_msg': 'Failed to apply configuration:\n{}',
        'error_loading': 'Error loading devices: {}',
        'error_config': 'Error loading config: {}'
    },
    'es': {
        'title': 'Configuración de Audio miloOS',
        'audio_config': 'Configuración de Audio',
        'sample_rate': 'Frecuencia de Muestreo (Hz)',
        'buffer_size': 'Tamaño de Buffer (samples)',
        'output_device': 'Dispositivo de Salida Predeterminado',
        'input_device': 'Dispositivo de Entrada Predeterminado',
        'apply': 'Aplicar',
        'close': 'Cerrar',
        'config_applied': 'Configuración Aplicada',
        'config_applied_msg': 'La configuración de audio se ha guardado y aplicado.\nPipeWire ha sido reiniciado.',
        'restarting': 'Reiniciando PipeWire...',
        'error': 'Error',
        'error_msg': 'Error al aplicar la configuración:\n{}',
        'error_loading': 'Error al cargar dispositivos: {}',
        'error_config': 'Error al cargar configuración: {}'
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
        
        # Set icon from theme
        try:
            self.set_icon_name("audio-config")
        except:
            # Fallback to loading icon from file
            try:
                icon_path = "/usr/share/icons/hicolor/scalable/apps/audio-config.svg"
                if os.path.exists(icon_path):
                    from gi.repository import GdkPixbuf
                    pixbuf = GdkPixbuf.Pixbuf.new_from_file_at_size(icon_path, 48, 48)
                    self.set_icon(pixbuf)
            except:
                pass
        
        self.set_border_width(20)
        self.set_default_size(500, 400)
        self.set_position(Gtk.WindowPosition.CENTER)
        
        # Main container
        vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=15)
        self.add(vbox)
        
        # Title
        title = Gtk.Label()
        title.set_markup(f"<span size='large' weight='bold'>{_('audio_config')}</span>")
        title.set_halign(Gtk.Align.START)
        vbox.pack_start(title, False, False, 0)
        
        # Sample Rate
        rate_box = self._create_section(_('sample_rate'))
        self.rate_combo = Gtk.ComboBoxText()
        rates = ["44100", "48000", "88200", "96000", "176400", "192000"]
        for rate in rates:
            self.rate_combo.append_text(rate)
        self.rate_combo.set_active(1)  # Default 48000
        rate_box.pack_start(self.rate_combo, True, True, 0)
        vbox.pack_start(rate_box, False, False, 0)
        
        # Buffer Size
        buffer_box = self._create_section(_('buffer_size'))
        self.buffer_combo = Gtk.ComboBoxText()
        buffers = ["32", "64", "128", "256", "512", "1024"]
        for buf in buffers:
            self.buffer_combo.append_text(buf)
        self.buffer_combo.set_active(3)  # Default 256
        buffer_box.pack_start(self.buffer_combo, True, True, 0)
        vbox.pack_start(buffer_box, False, False, 0)
        
        # Default Output Device
        output_box = self._create_section(_('output_device'))
        self.output_combo = Gtk.ComboBoxText()
        output_box.pack_start(self.output_combo, True, True, 0)
        vbox.pack_start(output_box, False, False, 0)
        
        # Default Input Device
        input_box = self._create_section(_('input_device'))
        self.input_combo = Gtk.ComboBoxText()
        input_box.pack_start(self.input_combo, True, True, 0)
        vbox.pack_start(input_box, False, False, 0)
        
        # Buttons
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        button_box.set_halign(Gtk.Align.END)
        
        apply_btn = Gtk.Button(label=_('apply'))
        apply_btn.connect("clicked", self.on_apply)
        apply_btn.get_style_context().add_class("suggested-action")
        button_box.pack_start(apply_btn, False, False, 0)
        
        close_btn = Gtk.Button(label=_('close'))
        close_btn.connect("clicked", lambda x: self.destroy())
        button_box.pack_start(close_btn, False, False, 0)
        
        vbox.pack_end(button_box, False, False, 0)
        
        # Load current configuration
        self.load_devices()
        self.load_config()
    
    def _create_section(self, title):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=5)
        label = Gtk.Label(label=title)
        label.set_halign(Gtk.Align.START)
        label.get_style_context().add_class("dim-label")
        box.pack_start(label, False, False, 0)
        return box
    
    def load_devices(self):
        """Load available audio devices"""
        try:
            # Get sinks (output devices)
            result = subprocess.run(['pactl', 'list', 'short', 'sinks'], 
                                  capture_output=True, text=True)
            for line in result.stdout.strip().split('\n'):
                if line:
                    parts = line.split('\t')
                    if len(parts) >= 2:
                        self.output_combo.append(parts[1], parts[1])
            
            # Get sources (input devices)
            result = subprocess.run(['pactl', 'list', 'short', 'sources'], 
                                  capture_output=True, text=True)
            for line in result.stdout.strip().split('\n'):
                if line and not '.monitor' in line:
                    parts = line.split('\t')
                    if len(parts) >= 2:
                        self.input_combo.append(parts[1], parts[1])
            
            self.output_combo.set_active(0)
            self.input_combo.set_active(0)
        except Exception as e:
            print(_('error_loading').format(e))
    
    def load_config(self):
        """Load current PipeWire configuration"""
        config_path = os.path.expanduser("~/.config/pipewire/pipewire.conf.d/99-custom.conf")
        if os.path.exists(config_path):
            try:
                with open(config_path, 'r') as f:
                    content = f.read()
                    
                    # Parse sample rate
                    for line in content.split('\n'):
                        if 'default.clock.rate' in line and '=' in line:
                            rate = line.split('=')[1].strip()
                            idx = self.rate_combo.get_model().get_iter_first()
                            i = 0
                            while idx:
                                if self.rate_combo.get_model().get_value(idx, 0) == rate:
                                    self.rate_combo.set_active(i)
                                    break
                                idx = self.rate_combo.get_model().iter_next(idx)
                                i += 1
                        
                        # Parse buffer size (quantum)
                        if 'default.clock.quantum' in line and '=' in line:
                            buffer = line.split('=')[1].strip()
                            idx = self.buffer_combo.get_model().get_iter_first()
                            i = 0
                            while idx:
                                if self.buffer_combo.get_model().get_value(idx, 0) == buffer:
                                    self.buffer_combo.set_active(i)
                                    break
                                idx = self.buffer_combo.get_model().iter_next(idx)
                                i += 1
            except Exception as e:
                print(_('error_config').format(e))
    
    def on_apply(self, button):
        """Apply configuration"""
        rate = self.rate_combo.get_active_text()
        buffer = self.buffer_combo.get_active_text()
        output = self.output_combo.get_active_id()
        input_dev = self.input_combo.get_active_id()
        
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
"""
        
        try:
            with open(config_path, 'w') as f:
                f.write(config_content)
            
            # Set default devices
            if output:
                subprocess.run(['pactl', 'set-default-sink', output])
            if input_dev:
                subprocess.run(['pactl', 'set-default-source', input_dev])
            
            # Restart PipeWire to apply changes
            try:
                subprocess.run(['systemctl', '--user', 'restart', 'pipewire'], 
                             check=True, capture_output=True)
                subprocess.run(['systemctl', '--user', 'restart', 'pipewire-pulse'], 
                             check=True, capture_output=True)
            except subprocess.CalledProcessError as e:
                print(f"Warning: Could not restart PipeWire: {e}")
            
            # Show success dialog
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
            # Show error dialog
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
