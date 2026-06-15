#include "dock_window.hpp"

#include <glib.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <cerrno>
#include <csignal>
#include <clocale>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

void normalize_process_cwd() {
    const char* home = g_get_home_dir();
    if (!home) {
        return;
    }
    if (g_chdir(home) == 0) {
        g_setenv("PWD", home, TRUE);
    }
}

DockWindow* dock_for_app(GtkApplication* app) {
    return static_cast<DockWindow*>(g_object_get_data(G_OBJECT(app), "dock-window"));
}

void set_dock_for_app(GtkApplication* app, DockWindow* dock) {
    g_object_set_data_full(G_OBJECT(app), "dock-window", dock, [](gpointer raw) {
        delete static_cast<DockWindow*>(raw);
    });
}

void add_action(GtkApplication* app, const char* name, GCallback callback) {
    GSimpleAction* action = g_simple_action_new(name, nullptr);
    g_signal_connect(action, "activate", callback, app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
    g_object_unref(action);
}

void setup_menubar(GtkApplication* app) {
    GMenu* menubar = g_menu_new();

    GMenu* dock_menu = g_menu_new();
    g_menu_append(dock_menu, "Preferencias...", "app.preferences");
    g_menu_append(dock_menu, "Recargar Dock", "app.reload");
    g_menu_append_submenu(menubar, "Dock", G_MENU_MODEL(dock_menu));

    GMenu* settings_menu = g_menu_new();
    GMenu* size_menu = g_menu_new();
    g_menu_append(size_menu, "Pequeno", "app.size-small");
    g_menu_append(size_menu, "Mediano", "app.size-medium");
    g_menu_append(size_menu, "Grande", "app.size-large");
    g_menu_append_submenu(settings_menu, "Tamano de iconos", G_MENU_MODEL(size_menu));

    GMenu* spacing_menu = g_menu_new();
    g_menu_append(spacing_menu, "Compacta", "app.spacing-compact");
    g_menu_append(spacing_menu, "Normal", "app.spacing-normal");
    g_menu_append(spacing_menu, "Amplia", "app.spacing-wide");
    g_menu_append_submenu(settings_menu, "Separacion entre lanzadores", G_MENU_MODEL(spacing_menu));

    g_menu_append(settings_menu, "Ocultar automaticamente", "app.autohide");

    GMenu* effect_menu = g_menu_new();
    g_menu_append(effect_menu, "Aumentar", "app.effect-magnify");
    g_menu_append(effect_menu, "Elevar", "app.effect-lift");
    g_menu_append(effect_menu, "Ninguno", "app.effect-none");
    g_menu_append_submenu(settings_menu, "Efecto", G_MENU_MODEL(effect_menu));

    GMenu* theme_menu = g_menu_new();
    g_menu_append(theme_menu, "Seguir sistema", "app.theme-auto");
    g_menu_append(theme_menu, "Claro", "app.theme-light");
    g_menu_append(theme_menu, "Oscuro", "app.theme-dark");
    g_menu_append_submenu(settings_menu, "Tema", G_MENU_MODEL(theme_menu));

    g_menu_append_submenu(menubar, "Configuracion", G_MENU_MODEL(settings_menu));
    gtk_application_set_menubar(app, G_MENU_MODEL(menubar));

    g_object_unref(theme_menu);
    g_object_unref(effect_menu);
    g_object_unref(spacing_menu);
    g_object_unref(size_menu);
    g_object_unref(settings_menu);
    g_object_unref(dock_menu);
    g_object_unref(menubar);
}

void setup_actions(GtkApplication* app) {
    add_action(app, "preferences", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->show_preferences_dialog();
        }
    }));
    add_action(app, "reload", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->reload_launchers();
        }
    }));

    add_action(app, "size-small", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_icon_size(34);
        }
    }));
    add_action(app, "size-medium", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_icon_size(38);
        }
    }));
    add_action(app, "size-large", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_icon_size(44);
        }
    }));

    add_action(app, "spacing-compact", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_launcher_spacing(1);
        }
    }));
    add_action(app, "spacing-normal", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_launcher_spacing(3);
        }
    }));
    add_action(app, "spacing-wide", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_launcher_spacing(7);
        }
    }));

    add_action(app, "autohide", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_auto_hide(!dock->settings().auto_hide);
        }
    }));

    add_action(app, "effect-magnify", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_effect("magnify");
        }
    }));
    add_action(app, "effect-lift", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_effect("lift");
        }
    }));
    add_action(app, "effect-none", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_effect("none");
        }
    }));

    add_action(app, "theme-auto", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_theme_mode("auto");
        }
    }));
    add_action(app, "theme-light", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_theme_mode("light");
        }
    }));
    add_action(app, "theme-dark", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer user_data) {
        if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
            dock->set_theme_mode("dark");
        }
    }));
}

void on_startup(GtkApplication* app, gpointer) {
    setup_actions(app);
    setup_menubar(app);
}

void on_activate(GtkApplication* app, gpointer) {
    DockWindow* dock = dock_for_app(app);
    if (!dock) {
        dock = new DockWindow(app);
        set_dock_for_app(app, dock);
        dock->show();
        return;
    }
    dock->present();
}

gboolean on_reload_signal(gpointer user_data) {
    if (DockWindow* dock = dock_for_app(GTK_APPLICATION(user_data))) {
        dock->reload_settings();
    }
    return G_SOURCE_CONTINUE;
}

gboolean on_quit_signal(gpointer user_data) {
    g_application_quit(G_APPLICATION(user_data));
    return G_SOURCE_REMOVE;
}

bool process_alive(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    if (kill(pid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

std::string state_dir() {
    const char* runtime_dir = g_getenv("XDG_RUNTIME_DIR");
    std::string base = runtime_dir && *runtime_dir ? runtime_dir : std::string(g_get_user_cache_dir());
    std::string dir = base + "/milodock";
    g_mkdir_with_parents(dir.c_str(), 0700);
    return dir;
}

pid_t read_pidfile(const std::string& path) {
    std::ifstream input(path);
    pid_t pid = 0;
    input >> pid;
    return pid;
}

void write_pidfile(const std::string& path) {
    std::ofstream output(path, std::ios::trunc);
    output << getpid() << "\n";
}

void cleanup_pidfile(const std::string& path) {
    if (read_pidfile(path) == getpid()) {
        g_unlink(path.c_str());
    }
}

bool claim_single_instance(bool replace, std::string* pidfile) {
    *pidfile = state_dir() + "/milodock.pid";
    pid_t old_pid = read_pidfile(*pidfile);
    if (old_pid > 0 && old_pid != getpid() && process_alive(old_pid)) {
        if (!replace) {
            g_printerr("miloDock ya se esta ejecutando\n");
            return false;
        }

        kill(old_pid, SIGTERM);
        for (int i = 0; i < 30 && process_alive(old_pid); ++i) {
            g_usleep(100000);
        }
        if (process_alive(old_pid)) {
            kill(old_pid, SIGKILL);
        }
    }

    write_pidfile(*pidfile);
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "");
    g_set_prgname("milodock");
    g_set_application_name("miloDock");
    normalize_process_cwd();

    bool replace = false;
    std::vector<char*> filtered_args;
    filtered_args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        if (g_strcmp0(argv[i], "--replace") == 0) {
            replace = true;
            continue;
        }
        filtered_args.push_back(argv[i]);
    }
    int filtered_argc = static_cast<int>(filtered_args.size());

    std::string pidfile;
    if (!claim_single_instance(replace, &pidfile)) {
        return 0;
    }

    GtkApplication* app = gtk_application_new("org.miloOS.miloDock", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), nullptr);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    g_unix_signal_add(SIGUSR1, on_reload_signal, app);
    g_unix_signal_add(SIGTERM, on_quit_signal, app);
    g_unix_signal_add(SIGINT, on_quit_signal, app);
    int status = g_application_run(G_APPLICATION(app), filtered_argc, filtered_args.data());
    g_object_unref(app);
    cleanup_pidfile(pidfile);
    return status;
}
