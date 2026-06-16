#include "panel_window.hpp"

#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <cerrno>
#include <clocale>
#include <csignal>
#include <fstream>
#include <sstream>
#include <string>
#include <set>
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

std::string normalized_gtk_modules(const char* modules, bool ensure_appmenu) {
    std::vector<std::string> ordered_modules;
    std::set<std::string> seen;

    auto add_module = [&ordered_modules, &seen](const std::string& module) {
        if (module.empty() || seen.find(module) != seen.end()) {
            return;
        }
        seen.insert(module);
        ordered_modules.push_back(module);
    };

    if (ensure_appmenu) {
        add_module("appmenu-gtk-module");
    }

    std::stringstream stream(modules ? modules : "");
    std::string module;
    while (std::getline(stream, module, ':')) {
        add_module(module);
    }

    std::string result;
    for (const std::string& value : ordered_modules) {
        if (!result.empty()) {
            result += ":";
        }
        result += value;
    }
    return result;
}

void disable_appmenu_export_for_panel() {
    const char* modules = g_getenv("GTK_MODULES");
    const std::string child_modules = normalized_gtk_modules(modules, true);
    if (!child_modules.empty()) {
        g_setenv("MILO_PANEL_CHILD_GTK_MODULES", child_modules.c_str(), TRUE);
    } else {
        g_unsetenv("MILO_PANEL_CHILD_GTK_MODULES");
    }
    g_setenv("UBUNTU_MENUPROXY", "1", FALSE);
    g_setenv("GTK_MODULES", "", TRUE);
}

PanelWindow* panel_for_app(GtkApplication* app) {
    return static_cast<PanelWindow*>(g_object_get_data(G_OBJECT(app), "panel-window"));
}

void set_panel_for_app(GtkApplication* app, PanelWindow* panel) {
    g_object_set_data_full(G_OBJECT(app), "panel-window", panel, [](gpointer raw) {
        delete static_cast<PanelWindow*>(raw);
    });
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
    std::string dir = base + "/milopanel";
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
    *pidfile = state_dir() + "/milopanel.pid";
    pid_t old_pid = read_pidfile(*pidfile);
    if (old_pid > 0 && old_pid != getpid() && process_alive(old_pid)) {
        if (!replace) {
            g_printerr("miloPanel ya se esta ejecutando\n");
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

gboolean on_reload_signal(gpointer user_data) {
    if (PanelWindow* panel = panel_for_app(GTK_APPLICATION(user_data))) {
        panel->reload_settings();
    }
    return G_SOURCE_CONTINUE;
}

gboolean on_quit_signal(gpointer user_data) {
    g_application_quit(G_APPLICATION(user_data));
    return G_SOURCE_REMOVE;
}

void on_activate(GtkApplication* app, gpointer user_data) {
    bool reserve_space = GPOINTER_TO_INT(user_data);
    PanelWindow* panel = panel_for_app(app);
    if (!panel) {
        panel = new PanelWindow(app, reserve_space);
        set_panel_for_app(app, panel);
    }
    panel->show();
}

} // namespace

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "");
    g_set_prgname("milopanel");
    g_set_application_name("miloPanel");
    normalize_process_cwd();
    disable_appmenu_export_for_panel();

    bool replace = false;
    bool reserve_space = true;
    std::vector<char*> filtered_args;
    filtered_args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        if (g_strcmp0(argv[i], "--replace") == 0) {
            replace = true;
            continue;
        }
        if (g_strcmp0(argv[i], "--no-struts") == 0) {
            reserve_space = false;
            continue;
        }
        filtered_args.push_back(argv[i]);
    }

    std::string pidfile;
    if (!claim_single_instance(replace, &pidfile)) {
        return 0;
    }

    GtkApplication* app = gtk_application_new("org.miloOS.miloPanel", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), GINT_TO_POINTER(reserve_space));
    g_unix_signal_add(SIGUSR1, on_reload_signal, app);
    g_unix_signal_add(SIGTERM, on_quit_signal, app);
    g_unix_signal_add(SIGINT, on_quit_signal, app);

    int filtered_argc = static_cast<int>(filtered_args.size());
    int status = g_application_run(G_APPLICATION(app), filtered_argc, filtered_args.data());
    g_object_unref(app);
    cleanup_pidfile(pidfile);
    return status;
}
