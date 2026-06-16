#include "system_tray_host.hpp"

#include <gdk/gdkx.h>
#include <gtk/gtkx.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>

namespace {

constexpr long SYSTEM_TRAY_REQUEST_DOCK = 0;

Atom atom(Display* display, const char* name) {
    return XInternAtom(display, name, False);
}

bool tray_debug_enabled() {
    const char* value = g_getenv("MILO_PANEL_DEBUG_TRAY");
    return value && *value;
}

void with_x11_error_trap(const std::function<void()>& action) {
    GdkDisplay* gdk_display = gdk_display_get_default();
    if (gdk_display && GDK_IS_X11_DISPLAY(gdk_display)) {
        gdk_x11_display_error_trap_push(gdk_display);
        action();
        gdk_x11_display_error_trap_pop_ignored(gdk_display);
        return;
    }
    action();
}

void clear_xembed_window_background(Display* display, Window window) {
    if (!display || window == 0) {
        return;
    }

    with_x11_error_trap([&]() {
        XSetWindowBorderWidth(display, window, 0);
        XSetWindowBorder(display, window, 0);
        XSetWindowBackgroundPixmap(display, window, ParentRelative);
        XClearWindow(display, window);
        XFlush(display);
    });
}

std::string window_class(Display* display, Window window) {
    XClassHint hint;
    hint.res_name = nullptr;
    hint.res_class = nullptr;
    if (!XGetClassHint(display, window, &hint)) {
        return "unknown";
    }
    std::string value;
    if (hint.res_class) {
        value = hint.res_class;
    } else if (hint.res_name) {
        value = hint.res_name;
    } else {
        value = "unknown";
    }
    if (hint.res_name) {
        XFree(hint.res_name);
    }
    if (hint.res_class) {
        XFree(hint.res_class);
    }
    return value;
}

} // namespace

SystemTrayHost::~SystemTrayHost() {
    if (claimed_) {
        gdk_window_remove_filter(filter_window_, event_filter, this);
    }
    if (filter_window_) {
        g_object_unref(filter_window_);
    }
}

bool SystemTrayHost::start(GtkWidget* panel_window, GtkWidget* tray_box, int icon_size) {
    GdkDisplay* gdk_display = gdk_display_get_default();
    if (!gdk_display || !GDK_IS_X11_DISPLAY(gdk_display) || !gtk_widget_get_window(panel_window)) {
        return false;
    }

    display_ = GDK_DISPLAY_XDISPLAY(gdk_display);
    filter_window_ = gtk_widget_get_window(panel_window);
    if (!filter_window_) {
        return false;
    }
    g_object_ref(filter_window_);
    owner_ = GDK_WINDOW_XID(filter_window_);
    tray_box_ = tray_box;
    icon_size_ = icon_size;
    selection_atom_ = atom(display_, "_NET_SYSTEM_TRAY_S0");
    opcode_atom_ = atom(display_, "_NET_SYSTEM_TRAY_OPCODE");
    manager_atom_ = atom(display_, "MANAGER");

    if (XGetSelectionOwner(display_, selection_atom_) != None) {
        return false;
    }

    XSetSelectionOwner(display_, selection_atom_, owner_, CurrentTime);
    if (XGetSelectionOwner(display_, selection_atom_) != owner_) {
        return false;
    }

    Atom orientation_atom = atom(display_, "_NET_SYSTEM_TRAY_ORIENTATION");
    unsigned long horizontal = 0;
    XChangeProperty(display_, owner_, orientation_atom, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&horizontal), 1);
    Atom icon_size_atom = atom(display_, "_NET_SYSTEM_TRAY_ICON_SIZE");
    unsigned long icon_size_value = static_cast<unsigned long>(std::max(1, icon_size_));
    XChangeProperty(display_, owner_, icon_size_atom, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&icon_size_value), 1);
    Atom padding_atom = atom(display_, "_NET_SYSTEM_TRAY_PADDING");
    unsigned long padding_value = 0;
    XChangeProperty(display_, owner_, padding_atom, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&padding_value), 1);

    GdkScreen* screen = gtk_widget_get_screen(panel_window);
    GdkVisual* gdk_visual = screen ? gdk_screen_get_rgba_visual(screen) : nullptr;
    if (!gdk_visual && screen) {
        gdk_visual = gdk_screen_get_system_visual(screen);
    }
    if (gdk_visual) {
        Visual* xvisual = GDK_VISUAL_XVISUAL(gdk_visual);
        if (xvisual) {
            Atom visual_atom = atom(display_, "_NET_SYSTEM_TRAY_VISUAL");
            Atom visualid_atom = atom(display_, "VISUALID");
            unsigned long visual_id = xvisual->visualid;
            XChangeProperty(display_, owner_, visual_atom, visualid_atom, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&visual_id), 1);
        }
    }

    XEvent event;
    std::memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.window = DefaultRootWindow(display_);
    event.xclient.message_type = manager_atom_;
    event.xclient.format = 32;
    event.xclient.data.l[0] = CurrentTime;
    event.xclient.data.l[1] = selection_atom_;
    event.xclient.data.l[2] = owner_;
    XSendEvent(display_, DefaultRootWindow(display_), False, StructureNotifyMask, &event);
    XFlush(display_);

    gdk_window_add_filter(filter_window_, event_filter, this);
    claimed_ = true;
    return true;
}

void SystemTrayHost::dock_icon(Window icon_window) {
    if (!tray_box_ || icon_window == 0) {
        return;
    }
    const auto existing = std::find_if(icons_.begin(), icons_.end(), [icon_window](const TrayIcon& icon) {
        return icon.window == icon_window;
    });
    if (existing != icons_.end()) {
        return;
    }

    if (tray_debug_enabled()) {
        XWindowAttributes attributes;
        const bool has_attributes = XGetWindowAttributes(display_, icon_window, &attributes) != 0;
        g_printerr(
            "miloPanel tray dock request window=0x%lx class=%s map_state=%d size=%dx%d\n",
            icon_window,
            window_class(display_, icon_window).c_str(),
            has_attributes ? attributes.map_state : -1,
            has_attributes ? attributes.width : -1,
            has_attributes ? attributes.height : -1);
    }

    GtkWidget* socket = gtk_socket_new();
    gtk_widget_set_can_focus(socket, FALSE);
    gtk_widget_set_app_paintable(socket, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(socket), "milopanel-tray-socket");
    gtk_widget_set_size_request(socket, icon_size_, icon_size_);
    GdkScreen* screen = gtk_widget_get_screen(tray_box_);
    GdkVisual* visual = screen ? gdk_screen_get_rgba_visual(screen) : nullptr;
    if (visual) {
        gtk_widget_set_visual(socket, visual);
    }
    gtk_box_pack_start(GTK_BOX(tray_box_), socket, FALSE, FALSE, 0);
    g_signal_connect(socket, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer) -> gboolean {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
        cairo_paint(cr);
        return FALSE;
    }), nullptr);
    gtk_widget_realize(socket);
    if (GdkWindow* gdk_window = gtk_widget_get_window(socket)) {
        GdkRGBA transparent{0.0, 0.0, 0.0, 0.0};
        gdk_window_set_background_rgba(gdk_window, &transparent);
        clear_xembed_window_background(display_, GDK_WINDOW_XID(gdk_window));
    }
    clear_xembed_window_background(display_, icon_window);
    gtk_socket_add_id(GTK_SOCKET(socket), icon_window);
    gtk_widget_show(socket);
    icons_.push_back({icon_window, socket});
    g_signal_connect(socket, "plug-added", G_CALLBACK(+[](GtkSocket* socket, gpointer data) {
        auto* host = static_cast<SystemTrayHost*>(data);
        Window plug_window = gtk_socket_get_plug_window(socket)
            ? GDK_WINDOW_XID(gtk_socket_get_plug_window(socket))
            : 0;
        if (plug_window != 0) {
            clear_xembed_window_background(host->display_, plug_window);
        }
        if (tray_debug_enabled()) {
            g_printerr(
                "miloPanel tray plug-added socket=0x%lx plug=0x%lx\n",
                gtk_socket_get_id(socket),
                plug_window);
        }
    }), this);
    g_signal_connect(socket, "destroy", G_CALLBACK(+[](GtkWidget* widget, gpointer data) {
        static_cast<SystemTrayHost*>(data)->forget_socket(widget);
    }), this);
    g_signal_connect(socket, "plug-removed", G_CALLBACK(+[](GtkSocket* socket, gpointer) -> gboolean {
        if (tray_debug_enabled()) {
            g_printerr("miloPanel tray plug-removed socket=0x%lx\n", gtk_socket_get_id(socket));
        }
        gtk_widget_hide(GTK_WIDGET(socket));
        g_object_ref(socket);
        g_idle_add(+[](gpointer data) -> gboolean {
            GtkWidget* widget = GTK_WIDGET(data);
            gtk_widget_destroy(widget);
            g_object_unref(widget);
            return G_SOURCE_REMOVE;
        }, socket);
        return TRUE;
    }), nullptr);
}

void SystemTrayHost::forget_socket(GtkWidget* socket) {
    icons_.erase(
        std::remove_if(icons_.begin(), icons_.end(), [socket](const TrayIcon& icon) {
            return icon.socket == socket;
        }),
        icons_.end());
}

GdkFilterReturn SystemTrayHost::event_filter(GdkXEvent* xevent, GdkEvent*, gpointer data) {
    auto* host = static_cast<SystemTrayHost*>(data);
    auto* event = static_cast<XEvent*>(xevent);
    if (!host || event->type != ClientMessage) {
        return GDK_FILTER_CONTINUE;
    }

    XClientMessageEvent* message = &event->xclient;
    if (message->message_type == host->opcode_atom_ && message->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
        host->dock_icon(static_cast<Window>(message->data.l[2]));
        return GDK_FILTER_REMOVE;
    }
    return GDK_FILTER_CONTINUE;
}
