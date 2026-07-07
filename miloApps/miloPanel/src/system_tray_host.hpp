#pragma once

#include <gtk/gtk.h>
#include <X11/Xlib.h>

#include <vector>

class SystemTrayHost {
public:
    SystemTrayHost() = default;
    ~SystemTrayHost();

    bool start(GtkWidget* panel_window, GtkWidget* tray_box, int icon_size);
    bool claimed() const { return claimed_; }
    void refresh_background();

private:
    struct TrayIcon {
        Window window = 0;
        GtkWidget* socket = nullptr;
    };

    struct DeferredRefresh {
        SystemTrayHost* host = nullptr;
        GtkWidget* socket = nullptr;
        Window window = 0;
    };

    GtkWidget* tray_box_ = nullptr;
    GdkWindow* filter_window_ = nullptr;
    Display* display_ = nullptr;
    Window owner_ = 0;
    Atom selection_atom_ = None;
    Atom opcode_atom_ = None;
    Atom manager_atom_ = None;
    int icon_size_ = 16;
    unsigned long bg_pixel_ = 0;
    bool claimed_ = false;
    std::vector<TrayIcon> icons_;

    void apply_background(GtkWidget* socket, Window icon_window);
    void schedule_background_refresh(GtkWidget* socket, Window icon_window, guint delay_ms);
    void dock_icon(Window icon_window);
    void forget_socket(GtkWidget* socket);
    static GdkFilterReturn event_filter(GdkXEvent* xevent, GdkEvent* event, gpointer data);
    static gboolean on_deferred_background_refresh(gpointer data);
};
