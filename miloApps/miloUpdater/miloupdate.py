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
        'ready': 'Ready to check for updates',
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
        'ready': 'Listo para buscar actualizaciones',
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
        
        self.set_default_size(750, 540)
        self.set_border_width(24)
        self.set_position(Gtk.WindowPosition.CENTER)
        
        # Set window icon
        self.set_icon_name("miloupdate")
        
        # Apply CSS styling
        self.apply_css()
        
        # Main container
        main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
        self.add(main_box)
        
        # Title & Status Header
        header_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        header_box.set_halign(Gtk.Align.START)
        
        title_label = Gtk.Label()
        title_label.set_markup(f"<span font_desc='Inter, System-UI, sans-serif 18' weight='bold' foreground='#2c3e50'>{self.t['title']}</span>")
        title_label.set_halign(Gtk.Align.START)
        header_box.pack_start(title_label, False, False, 0)
        
        self.status_label = Gtk.Label()
        self.status_label.set_markup(f'<span font_desc="11" foreground="#7f8c8d">{self.t["ready"]}</span>')
        self.status_label.set_halign(Gtk.Align.START)
        header_box.pack_start(self.status_label, False, False, 0)
        
        main_box.pack_start(header_box, False, False, 0)
        
        # Terminal output
        scrolled = Gtk.ScrolledWindow()
        scrolled.set_vexpand(True)
        scrolled.set_hexpand(True)
        scrolled.get_style_context().add_class("terminal-card")
        
        self.terminal = Vte.Terminal()
        self.terminal.set_scroll_on_output(True)
        self.terminal.set_scrollback_lines(10000)
        
        # Apply dark mode theme to terminal
        bg_color = Gdk.RGBA()
        bg_color.parse("#1c1c1e")
        fg_color = Gdk.RGBA()
        fg_color.parse("#f4f4f6")
        self.terminal.set_colors(fg_color, bg_color, [])
        
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
        self.update_button.get_style_context().add_class("update-btn")
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
            background-color: #f1f2f6;
        }
        button {
            border-radius: 8px;
            padding: 6px 14px;
            font-size: 13px;
            font-weight: 500;
            color: #2c3e50;
            background-color: #ffffff;
            border: 1px solid #dcdde1;
            box-shadow: 0 1px 2px rgba(0, 0, 0, 0.04);
            transition: all 0.2s ease;
        }
        button:hover {
            background-color: #f8f9fa;
            border-color: #b1b2b9;
        }
        button:disabled {
            opacity: 0.5;
            background-color: #e3e4e9;
            color: #8e8e93;
            border-color: #dcdde1;
        }
        .update-btn {
            background-color: #007AFF;
            color: #ffffff;
            border: none;
            border-radius: 8px;
            font-size: 13px;
            font-weight: 600;
            transition: background-color 0.2s ease;
            box-shadow: 0 2px 6px rgba(0, 122, 255, 0.2);
        }
        .update-btn:hover {
            background-color: #0066d6;
        }
        .update-btn:active {
            background-color: #0051b5;
        }
        .update-btn:disabled {
            background-color: #e3e4e9;
            color: #8e8e93;
            border: none;
            box-shadow: none;
            opacity: 0.6;
        }
        .terminal-card {
            background-color: #ffffff;
            border: 1px solid #e3e4e9;
            border-radius: 10px;
            padding: 12px;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.02);
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
