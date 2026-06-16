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

private:
    struct TrayIcon {
        Window window = 0;
        GtkWidget* socket = nullptr;
    };

    GtkWidget* tray_box_ = nullptr;
    GdkWindow* filter_window_ = nullptr;
    Display* display_ = nullptr;
    Window owner_ = 0;
    Atom selection_atom_ = None;
    Atom opcode_atom_ = None;
    Atom manager_atom_ = None;
    int icon_size_ = 16;
    bool claimed_ = false;
    std::vector<TrayIcon> icons_;

    void dock_icon(Window icon_window);
    void forget_socket(GtkWidget* socket);
    static GdkFilterReturn event_filter(GdkXEvent* xevent, GdkEvent* event, gpointer data);
};
