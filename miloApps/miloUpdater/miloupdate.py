#!/usr/bin/env python3
"""
miloOS System Updater
Simple and elegant system update interface
"""

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('Vte', '2.91')
from gi.repository import Gtk, Gdk, GLib, Vte
import subprocess
import os
import locale
import threading

# Translations
TRANSLATIONS = {
    'en': {
        'title': 'System Updater',
        'check_updates': 'Check for Updates',
        'install_updates': 'Install Updates',
        'checking': 'Checking for updates...',
        'updating': 'Installing updates...',
        'up_to_date': 'System is up to date',
        'updates_available': 'updates available',
        'error': 'Error',
        'success': 'Updates installed successfully',
        'close': 'Close',
        'output': 'Output',
    },
    'es': {
        'title': 'Actualizador del Sistema',
        'check_updates': 'Buscar Actualizaciones',
        'install_updates': 'Instalar Actualizaciones',
        'checking': 'Buscando actualizaciones...',
        'updating': 'Instalando actualizaciones...',
        'up_to_date': 'El sistema está actualizado',
        'updates_available': 'actualizaciones disponibles',
        'error': 'Error',
        'success': 'Actualizaciones instaladas correctamente',
        'close': 'Cerrar',
        'output': 'Salida',
    }
}

def get_system_language():
    """Detect system language"""
    try:
        lang = locale.getdefaultlocale()[0]
        if lang and lang.startswith('es'):
            return 'es'
    except:
        pass
    return 'en'

class UpdaterWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title="miloOS Updater")
        self.lang = get_system_language()
        self.t = TRANSLATIONS[self.lang]
        
        self.set_default_size(700, 500)
        self.set_border_width(20)
        self.set_position(Gtk.WindowPosition.CENTER)
        
        # Set window icon
        self.set_icon_name("miloupdate")
        
        # Set WM_CLASS for proper dock integration
        self.set_wmclass("miloupdate", "miloupdate")
        
        # Apply CSS styling
        self.apply_css()
        
        # Main container
        main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
        self.add(main_box)
        
        # Status label
        self.status_label = Gtk.Label()
        self.status_label.set_markup('<span size="large">Ready</span>')
        self.status_label.set_halign(Gtk.Align.CENTER)
        main_box.pack_start(self.status_label, False, False, 0)
        
        # Terminal output
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_vexpand(True)
        scrolled.set_hexpand(True)
        
        self.terminal = Vte.Terminal()
        self.terminal.set_scroll_on_output(True)
        self.terminal.set_scrollback_lines(10000)
        scrolled.add(self.terminal)
        
        main_box.pack_start(scrolled, True, True, 0)
        
        # Button box
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        button_box.set_halign(Gtk.Align.CENTER)
        
        self.check_button = Gtk.Button(label=self.t['check_updates'])
        self.check_button.set_size_request(180, 40)
        self.check_button.connect('clicked', self.on_check_updates)
        button_box.pack_start(self.check_button, False, False, 0)
        
        self.update_button = Gtk.Button(label=self.t['install_updates'])
        self.update_button.set_size_request(180, 40)
        self.update_button.set_sensitive(False)
        self.update_button.connect('clicked', self.on_install_updates)
        button_box.pack_start(self.update_button, False, False, 0)
        
        self.close_button = Gtk.Button(label=self.t['close'])
        self.close_button.set_size_request(100, 40)
        self.close_button.connect('clicked', lambda w: self.destroy())
        button_box.pack_start(self.close_button, False, False, 0)
        
        main_box.pack_start(button_box, False, False, 0)
        
        self.updates_count = 0
        
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
        """
        css_provider.load_from_data(css)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            css_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
    
    def run_command_in_terminal(self, command, callback=None):
        """Run command in VTE terminal"""
        def on_child_exited(terminal, status):
            if callback:
                GLib.idle_add(callback, status)
        
        self.terminal.connect('child-exited', on_child_exited)
        self.terminal.spawn_sync(
            Vte.PtyFlags.DEFAULT,
            os.environ['HOME'],
            ['/bin/bash', '-c', command],
            [],
            GLib.SpawnFlags.DO_NOT_REAP_CHILD,
            None,
            None,
        )
    
    def on_check_updates(self, button):
        """Check for available updates"""
        self.check_button.set_sensitive(False)
        self.update_button.set_sensitive(False)
        self.status_label.set_markup(f'<span size="large">{self.t["checking"]}</span>')
        
        def check_finished(status):
            # Count upgradable packages
            try:
                result = subprocess.run(
                    ['apt', 'list', '--upgradable'],
                    capture_output=True,
                    text=True
                )
                lines = result.stdout.strip().split('\n')
                # Subtract 1 for the header line
                self.updates_count = max(0, len([l for l in lines if l.strip()]) - 1)
                
                if self.updates_count > 0:
                    self.status_label.set_markup(
                        f'<span size="large" weight="bold">{self.updates_count} {self.t["updates_available"]}</span>'
                    )
                    self.update_button.set_sensitive(True)
                else:
                    self.status_label.set_markup(
                        f'<span size="large" foreground="#28a745">{self.t["up_to_date"]}</span>'
                    )
            except Exception as e:
                self.status_label.set_markup(
                    f'<span size="large" foreground="#dc3545">{self.t["error"]}: {str(e)}</span>'
                )
            
            self.check_button.set_sensitive(True)
        
        # Run apt update with pkexec
        command = 'pkexec apt update'
        self.run_command_in_terminal(command, check_finished)
    
    def on_install_updates(self, button):
        """Install available updates"""
        self.check_button.set_sensitive(False)
        self.update_button.set_sensitive(False)
        self.status_label.set_markup(f'<span size="large">{self.t["updating"]}</span>')
        
        def update_finished(status):
            if status == 0:
                self.status_label.set_markup(
                    f'<span size="large" foreground="#28a745">{self.t["success"]}</span>'
                )
            else:
                self.status_label.set_markup(
                    f'<span size="large" foreground="#dc3545">{self.t["error"]}</span>'
                )
            
            self.check_button.set_sensitive(True)
            self.updates_count = 0
        
        # Run apt upgrade with pkexec
        command = 'pkexec apt upgrade -y'
        self.run_command_in_terminal(command, update_finished)

def main():
    window = UpdaterWindow()
    window.connect('destroy', Gtk.main_quit)
    window.show_all()
    Gtk.main()

if __name__ == '__main__':
    main()
