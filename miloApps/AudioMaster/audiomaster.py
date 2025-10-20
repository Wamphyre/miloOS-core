#!/usr/bin/env python3
"""
miloOS Audio Master
Professional audio mastering tool using Matchering
"""

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk, GLib
import subprocess
import os
import locale
import threading

# Translations
TRANSLATIONS = {
    'en': {
        'title': 'Audio Master',
        'reference_track': 'Reference Track',
        'target_track': 'Target Track',
        'output_file': 'Output File',
        'select_reference': 'Select Reference',
        'select_target': 'Select Target',
        'select_output': 'Select Output Location',
        'start_mastering': 'Start Mastering',
        'processing': 'Processing...',
        'success': 'Mastering Complete',
        'success_msg': 'Your audio has been mastered successfully!',
        'error': 'Error',
        'error_msg': 'Failed to master audio:\n{}',
        'select_file': 'Select Audio File',
        'save_file': 'Save Mastered Audio',
        'audio_files': 'Audio Files',
        'all_files': 'All Files',
        'no_reference': 'Please select a reference track',
        'no_target': 'Please select a target track',
        'no_output': 'Please select an output location',
        'install_matchering': 'Install Matchering',
        'matchering_not_found': 'Matchering Not Installed',
        'matchering_install_msg': 'Matchering is required for audio mastering.\nWould you like to install it now?',
        'installing': 'Installing Matchering...',
        'install_success': 'Installation Complete',
        'install_success_msg': 'Matchering has been installed successfully!',
        'install_failed': 'Installation Failed',
        'install_failed_msg': 'Failed to install Matchering:\n{}',
        'close': 'Close',
        'cancel': 'Cancel',
        'progress': 'Progress',
        'options': 'Mastering Options',
        'quality': 'Quality:',
        'quality_low': 'Fast (Lower Quality)',
        'quality_medium': 'Balanced',
        'quality_high': 'Best (Slower)',
        'normalize': 'Normalize Output',
        'limiter': 'Apply Limiter',
    },
    'es': {
        'title': 'Audio Master',
        'reference_track': 'Pista de Referencia',
        'target_track': 'Pista a Masterizar',
        'output_file': 'Archivo de Salida',
        'select_reference': 'Seleccionar Referencia',
        'select_target': 'Seleccionar Pista',
        'select_output': 'Seleccionar Ubicación',
        'start_mastering': 'Iniciar Masterización',
        'processing': 'Procesando...',
        'success': 'Masterización Completa',
        'success_msg': '¡Tu audio ha sido masterizado exitosamente!',
        'error': 'Error',
        'error_msg': 'Error al masterizar el audio:\n{}',
        'select_file': 'Seleccionar Archivo de Audio',
        'save_file': 'Guardar Audio Masterizado',
        'audio_files': 'Archivos de Audio',
        'all_files': 'Todos los Archivos',
        'no_reference': 'Por favor selecciona una pista de referencia',
        'no_target': 'Por favor selecciona una pista a masterizar',
        'no_output': 'Por favor selecciona una ubicación de salida',
        'install_matchering': 'Instalar Matchering',
        'matchering_not_found': 'Matchering No Instalado',
        'matchering_install_msg': 'Matchering es necesario para la masterización de audio.\n¿Deseas instalarlo ahora?',
        'installing': 'Instalando Matchering...',
        'install_success': 'Instalación Completa',
        'install_success_msg': '¡Matchering se ha instalado correctamente!',
        'install_failed': 'Instalación Fallida',
        'install_failed_msg': 'Error al instalar Matchering:\n{}',
        'close': 'Cerrar',
        'cancel': 'Cancelar',
        'progress': 'Progreso',
        'options': 'Opciones de Masterización',
        'quality': 'Calidad:',
        'quality_low': 'Rápido (Menor Calidad)',
        'quality_medium': 'Balanceado',
        'quality_high': 'Mejor (Más Lento)',
        'normalize': 'Normalizar Salida',
        'limiter': 'Aplicar Limitador',
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

class AudioMasterWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title=_('title'))
        self.set_icon_name("audiomaster")
        self.set_default_size(700, 500)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_border_width(20)
        
        # Set WM_CLASS for proper dock integration
        self.set_wmclass("audiomaster", "audiomaster")
        
        # File paths
        self.reference_path = None
        self.target_path = None
        self.output_path = None
        
        # Mastering options
        self.quality_level = 1  # 0=low, 1=medium, 2=high
        self.normalize = True
        self.use_limiter = True
        
        # Apply miloOS styling
        self.apply_css()
        
        # Check if matchering is installed
        if not self.check_matchering():
            self.show_install_dialog()
            return
        
        # Main container
        main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
        self.add(main_box)
        
        # Content area (no header)
        
        # Content area
        content_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=15)
        content_box.set_halign(Gtk.Align.CENTER)
        content_box.set_valign(Gtk.Align.CENTER)
        
        # Reference track
        self.reference_box = self.create_file_selector(
            _('reference_track'),
            _('select_reference'),
            self.on_select_reference
        )
        content_box.pack_start(self.reference_box, False, False, 0)
        
        # Target track
        self.target_box = self.create_file_selector(
            _('target_track'),
            _('select_target'),
            self.on_select_target
        )
        content_box.pack_start(self.target_box, False, False, 0)
        
        # Output file
        self.output_box = self.create_file_selector(
            _('output_file'),
            _('select_output'),
            self.on_select_output
        )
        content_box.pack_start(self.output_box, False, False, 0)
        
        # Options section
        options_box = self.create_options_section()
        content_box.pack_start(options_box, False, False, 0)
        
        main_box.pack_start(content_box, True, True, 0)
        
        # Progress bar
        self.progress_bar = Gtk.ProgressBar()
        self.progress_bar.set_show_text(True)
        self.progress_bar.set_text("")
        self.progress_bar.set_no_show_all(True)
        main_box.pack_start(self.progress_bar, False, False, 0)
        
        # Button box
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        button_box.set_halign(Gtk.Align.CENTER)
        
        self.master_button = Gtk.Button(label=_('start_mastering'))
        self.master_button.set_size_request(200, 40)
        self.master_button.connect('clicked', self.on_start_mastering)
        button_box.pack_start(self.master_button, False, False, 0)
        
        self.close_button = Gtk.Button(label=_('close'))
        self.close_button.set_size_request(100, 40)
        self.close_button.connect('clicked', lambda w: self.destroy())
        button_box.pack_start(self.close_button, False, False, 0)
        
        main_box.pack_start(button_box, False, False, 0)
    
    def apply_css(self):
        """Apply custom CSS styling"""
        css_provider = Gtk.CssProvider()
        css = b"""
        window {
            background-color: #f5f5f7;
        }
        button {
            border-radius: 8px;
            padding: 8px 16px;
            font-weight: 500;
            background-image: linear-gradient(to bottom, #ffffff, #f0f0f0);
            border: 1px solid #d0d0d0;
        }
        button:hover {
            background-image: linear-gradient(to bottom, #ffffff, #e8e8e8);
        }
        button:disabled {
            opacity: 0.5;
        }
        .file-box {
            background-color: #ffffff;
            border: 1px solid #d0d0d0;
            border-radius: 8px;
            padding: 15px;
            min-width: 500px;
        }
        .file-label {
            color: #666666;
            font-size: 12px;
            font-weight: 600;
        }
        .file-path {
            color: #333333;
            font-size: 13px;
        }
        progressbar {
            min-height: 8px;
        }
        progressbar trough {
            background-color: #e0e0e0;
            border-radius: 4px;
        }
        progressbar progress {
            background-color: #007AFF;
            border-radius: 4px;
        }
        """
        css_provider.load_from_data(css)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            css_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
    
    def check_matchering(self):
        """Check if matchering is installed"""
        try:
            result = subprocess.run(['python3', '-c', 'import matchering'],
                                  capture_output=True, timeout=5)
            return result.returncode == 0
        except:
            return False
    
    def show_install_dialog(self):
        """Show dialog to install matchering"""
        dialog = Gtk.MessageDialog(
            transient_for=self,
            flags=0,
            message_type=Gtk.MessageType.QUESTION,
            buttons=Gtk.ButtonsType.YES_NO,
            text=_('matchering_not_found')
        )
        dialog.format_secondary_text(_('matchering_install_msg'))
        response = dialog.run()
        dialog.destroy()
        
        if response == Gtk.ResponseType.YES:
            self.install_matchering()
        else:
            self.destroy()
    
    def install_matchering(self):
        """Install matchering using pip"""
        # Show progress dialog
        progress_dialog = Gtk.MessageDialog(
            transient_for=self,
            flags=0,
            message_type=Gtk.MessageType.INFO,
            buttons=Gtk.ButtonsType.NONE,
            text=_('installing')
        )
        progress_dialog.show()
        
        def install_thread():
            try:
                # Install matchering and dependencies
                result = subprocess.run(
                    ['pip3', 'install', '--user', 'matchering'],
                    capture_output=True,
                    text=True
                )
                
                GLib.idle_add(self.on_install_complete, progress_dialog, result.returncode == 0, result.stderr)
            except Exception as e:
                GLib.idle_add(self.on_install_complete, progress_dialog, False, str(e))
        
        thread = threading.Thread(target=install_thread)
        thread.daemon = True
        thread.start()
    
    def on_install_complete(self, progress_dialog, success, error_msg):
        """Handle installation completion"""
        progress_dialog.destroy()
        
        if success:
            dialog = Gtk.MessageDialog(
                transient_for=self,
                flags=0,
                message_type=Gtk.MessageType.INFO,
                buttons=Gtk.ButtonsType.OK,
                text=_('install_success')
            )
            dialog.format_secondary_text(_('install_success_msg'))
            dialog.run()
            dialog.destroy()
            
            # Rebuild the window
            for child in self.get_children():
                self.remove(child)
            self.__init__()
            self.show_all()
        else:
            dialog = Gtk.MessageDialog(
                transient_for=self,
                flags=0,
                message_type=Gtk.MessageType.ERROR,
                buttons=Gtk.ButtonsType.OK,
                text=_('install_failed')
            )
            dialog.format_secondary_text(_('install_failed_msg').format(error_msg))
            dialog.run()
            dialog.destroy()
            self.destroy()
    
    def create_options_section(self):
        """Create options section"""
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        box.get_style_context().add_class("file-box")
        
        # Label
        label = Gtk.Label(label=_('options'))
        label.set_halign(Gtk.Align.START)
        label.get_style_context().add_class("file-label")
        box.pack_start(label, False, False, 0)
        
        # Quality selector
        quality_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        quality_label = Gtk.Label(label=_('quality'))
        quality_box.pack_start(quality_label, False, False, 0)
        
        self.quality_combo = Gtk.ComboBoxText()
        self.quality_combo.append_text(_('quality_low'))
        self.quality_combo.append_text(_('quality_medium'))
        self.quality_combo.append_text(_('quality_high'))
        self.quality_combo.set_active(1)  # Default to medium
        self.quality_combo.connect('changed', self.on_quality_changed)
        quality_box.pack_start(self.quality_combo, True, True, 0)
        
        box.pack_start(quality_box, False, False, 0)
        
        # Normalize checkbox
        self.normalize_check = Gtk.CheckButton(label=_('normalize'))
        self.normalize_check.set_active(True)
        self.normalize_check.connect('toggled', self.on_normalize_toggled)
        box.pack_start(self.normalize_check, False, False, 0)
        
        # Limiter checkbox
        self.limiter_check = Gtk.CheckButton(label=_('limiter'))
        self.limiter_check.set_active(True)
        self.limiter_check.connect('toggled', self.on_limiter_toggled)
        box.pack_start(self.limiter_check, False, False, 0)
        
        return box
    
    def on_quality_changed(self, combo):
        """Handle quality selection change"""
        self.quality_level = combo.get_active()
    
    def on_normalize_toggled(self, button):
        """Handle normalize toggle"""
        self.normalize = button.get_active()
    
    def on_limiter_toggled(self, button):
        """Handle limiter toggle"""
        self.use_limiter = button.get_active()
    
    def create_file_selector(self, label_text, button_text, callback):
        """Create a file selector widget"""
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        box.get_style_context().add_class("file-box")
        
        # Label
        label = Gtk.Label(label=label_text)
        label.set_halign(Gtk.Align.START)
        label.get_style_context().add_class("file-label")
        box.pack_start(label, False, False, 0)
        
        # File path and button
        hbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        
        path_label = Gtk.Label(label="")
        path_label.set_halign(Gtk.Align.START)
        path_label.set_ellipsize(3)  # ELLIPSIZE_END
        path_label.get_style_context().add_class("file-path")
        hbox.pack_start(path_label, True, True, 0)
        
        button = Gtk.Button(label=button_text)
        button.connect('clicked', callback)
        hbox.pack_start(button, False, False, 0)
        
        box.pack_start(hbox, False, False, 0)
        
        # Store reference to path label
        box.path_label = path_label
        
        return box
    
    def on_select_reference(self, button):
        """Select reference track"""
        dialog = Gtk.FileChooserDialog(
            title=_('select_file'),
            parent=self,
            action=Gtk.FileChooserAction.OPEN
        )
        dialog.add_buttons(
            _('cancel'), Gtk.ResponseType.CANCEL,
            _('select_reference'), Gtk.ResponseType.OK
        )
        
        # Add audio file filters
        filter_audio = Gtk.FileFilter()
        filter_audio.set_name(_('audio_files'))
        filter_audio.add_mime_type("audio/*")
        filter_audio.add_pattern("*.mp3")
        filter_audio.add_pattern("*.wav")
        filter_audio.add_pattern("*.flac")
        filter_audio.add_pattern("*.ogg")
        filter_audio.add_pattern("*.m4a")
        dialog.add_filter(filter_audio)
        
        filter_all = Gtk.FileFilter()
        filter_all.set_name(_('all_files'))
        filter_all.add_pattern("*")
        dialog.add_filter(filter_all)
        
        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            self.reference_path = dialog.get_filename()
            self.reference_box.path_label.set_text(os.path.basename(self.reference_path))
        
        dialog.destroy()
    
    def on_select_target(self, button):
        """Select target track"""
        dialog = Gtk.FileChooserDialog(
            title=_('select_file'),
            parent=self,
            action=Gtk.FileChooserAction.OPEN
        )
        dialog.add_buttons(
            _('cancel'), Gtk.ResponseType.CANCEL,
            _('select_target'), Gtk.ResponseType.OK
        )
        
        # Add audio file filters
        filter_audio = Gtk.FileFilter()
        filter_audio.set_name(_('audio_files'))
        filter_audio.add_mime_type("audio/*")
        filter_audio.add_pattern("*.mp3")
        filter_audio.add_pattern("*.wav")
        filter_audio.add_pattern("*.flac")
        filter_audio.add_pattern("*.ogg")
        filter_audio.add_pattern("*.m4a")
        dialog.add_filter(filter_audio)
        
        filter_all = Gtk.FileFilter()
        filter_all.set_name(_('all_files'))
        filter_all.add_pattern("*")
        dialog.add_filter(filter_all)
        
        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            self.target_path = dialog.get_filename()
            self.target_box.path_label.set_text(os.path.basename(self.target_path))
        
        dialog.destroy()
    
    def on_select_output(self, button):
        """Select output location"""
        dialog = Gtk.FileChooserDialog(
            title=_('save_file'),
            parent=self,
            action=Gtk.FileChooserAction.SAVE
        )
        dialog.add_buttons(
            _('cancel'), Gtk.ResponseType.CANCEL,
            _('select_output'), Gtk.ResponseType.OK
        )
        dialog.set_do_overwrite_confirmation(True)
        
        # Set default filename
        if self.target_path:
            base = os.path.splitext(os.path.basename(self.target_path))[0]
            dialog.set_current_name(f"{base}_mastered.wav")
        else:
            dialog.set_current_name("mastered.wav")
        
        # Add audio file filters
        filter_audio = Gtk.FileFilter()
        filter_audio.set_name(_('audio_files'))
        filter_audio.add_pattern("*.wav")
        filter_audio.add_pattern("*.flac")
        dialog.add_filter(filter_audio)
        
        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            self.output_path = dialog.get_filename()
            self.output_box.path_label.set_text(os.path.basename(self.output_path))
        
        dialog.destroy()
    
    def on_start_mastering(self, button):
        """Start the mastering process"""
        # Validate inputs
        if not self.reference_path:
            self.show_error(_('no_reference'))
            return
        
        if not self.target_path:
            self.show_error(_('no_target'))
            return
        
        if not self.output_path:
            self.show_error(_('no_output'))
            return
        
        # Disable button and show progress
        self.master_button.set_sensitive(False)
        self.progress_bar.show()
        self.progress_bar.set_fraction(0.0)
        self.progress_bar.set_text(_('processing'))
        
        # Start mastering in a thread
        def master_thread():
            try:
                # Import and run matchering
                import matchering as mg
                
                # Configure quality settings
                config = mg.Config()
                if self.quality_level == 0:  # Low quality (fast)
                    config.internal_sample_rate = 44100
                elif self.quality_level == 1:  # Medium quality
                    config.internal_sample_rate = 48000
                else:  # High quality (slow)
                    config.internal_sample_rate = 96000
                
                # Apply normalize and limiter settings
                if not self.normalize:
                    config.normalize_loudness = False
                if not self.use_limiter:
                    config.use_limiter = False
                
                # Process with config
                mg.process(
                    target=self.target_path,
                    reference=self.reference_path,
                    results=[mg.pcm16(self.output_path)],
                    config=config
                )
                
                GLib.idle_add(self.on_mastering_complete, True, "")
            except Exception as e:
                GLib.idle_add(self.on_mastering_complete, False, str(e))
        
        thread = threading.Thread(target=master_thread)
        thread.daemon = True
        thread.start()
        
        # Pulse progress bar
        GLib.timeout_add(100, self.pulse_progress)
    
    def pulse_progress(self):
        """Pulse the progress bar"""
        if self.progress_bar.get_visible() and self.progress_bar.get_fraction() == 0.0:
            self.progress_bar.pulse()
            return True
        return False
    
    def on_mastering_complete(self, success, error_msg):
        """Handle mastering completion"""
        self.progress_bar.hide()
        self.master_button.set_sensitive(True)
        
        if success:
            dialog = Gtk.MessageDialog(
                transient_for=self,
                flags=0,
                message_type=Gtk.MessageType.INFO,
                buttons=Gtk.ButtonsType.OK,
                text=_('success')
            )
            dialog.format_secondary_text(_('success_msg'))
            dialog.run()
            dialog.destroy()
        else:
            self.show_error(_('error_msg').format(error_msg))
    
    def show_error(self, message):
        """Show error dialog"""
        dialog = Gtk.MessageDialog(
            transient_for=self,
            flags=0,
            message_type=Gtk.MessageType.ERROR,
            buttons=Gtk.ButtonsType.OK,
            text=_('error')
        )
        dialog.format_secondary_text(message)
        dialog.run()
        dialog.destroy()

def main():
    win = AudioMasterWindow()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()
