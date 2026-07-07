#include "system_tray_host.hpp"

#include <gdk/gdkx.h>
#include <gtk/gtkx.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr long SYSTEM_TRAY_REQUEST_DOCK = 0;

struct WindowIdentity {
    std::string res_name;
    std::string res_class;
    std::string title;
};

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

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string window_text_property(Display* display, Window window, const char* property_name) {
    if (!display || window == 0) {
        return {};
    }

    Atom property = atom(display, property_name);
    Atom utf8 = atom(display, "UTF8_STRING");
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    std::string value;

    with_x11_error_trap([&]() {
        if (XGetWindowProperty(display, window, property, 0, 512, False, utf8,
                               &actual_type, &actual_format, &nitems, &bytes_after,
                               &data) == Success && data && actual_format == 8) {
            value.assign(reinterpret_cast<char*>(data), nitems);
        }
        if (data) {
            XFree(data);
            data = nullptr;
        }
    });
    return value;
}

unsigned short xcolor_channel(double value) {
    value = std::max(0.0, std::min(1.0, value));
    return static_cast<unsigned short>(value * 65535.0 + 0.5);
}

unsigned long fallback_pixel(Display* display, const GdkRGBA& color) {
    const int screen = DefaultScreen(display);
    const double luminance = (color.red * 0.2126) + (color.green * 0.7152) + (color.blue * 0.0722);
    return luminance > 0.5 ? WhitePixel(display, screen) : BlackPixel(display, screen);
}

unsigned long x11_pixel_for_color(Display* display, const GdkRGBA& color) {
    if (!display) {
        return 0;
    }

    XColor xcolor;
    xcolor.red = xcolor_channel(color.red);
    xcolor.green = xcolor_channel(color.green);
    xcolor.blue = xcolor_channel(color.blue);
    xcolor.flags = DoRed | DoGreen | DoBlue;

    Colormap cmap = DefaultColormap(display, DefaultScreen(display));
    if (XAllocColor(display, cmap, &xcolor)) {
        return xcolor.pixel;
    }
    return fallback_pixel(display, color);
}

double color_delta(const GdkRGBA& first, const GdkRGBA& second) {
    return std::max({
        std::abs(first.red - second.red),
        std::abs(first.green - second.green),
        std::abs(first.blue - second.blue)
    });
}

double clamp_channel(double value) {
    return std::max(0.0, std::min(1.0, value));
}

GdkRGBA opaque_color(GdkRGBA color) {
    color.red = clamp_channel(color.red);
    color.green = clamp_channel(color.green);
    color.blue = clamp_channel(color.blue);
    color.alpha = 1.0;
    return color;
}

GdkRGBA composite_over(const GdkRGBA& foreground, const GdkRGBA& background) {
    const double alpha = clamp_channel(foreground.alpha);
    GdkRGBA color;
    color.red = clamp_channel((foreground.red * alpha) + (background.red * (1.0 - alpha)));
    color.green = clamp_channel((foreground.green * alpha) + (background.green * (1.0 - alpha)));
    color.blue = clamp_channel((foreground.blue * alpha) + (background.blue * (1.0 - alpha)));
    color.alpha = 1.0;
    return color;
}

GdkRGBA widget_background_color(GtkWidget* widget, GtkWidget* fallback) {
    GdkRGBA color{0.0, 0.0, 0.0, 1.0};
    GtkWidget* candidates[] = {widget, fallback};

    for (GtkWidget* candidate : candidates) {
        if (!candidate) {
            continue;
        }

        GtkStyleContext* context = gtk_widget_get_style_context(candidate);
        if (!context) {
            continue;
        }

        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        gtk_style_context_get_background_color(context, GTK_STATE_FLAG_NORMAL, &color);
        #pragma GCC diagnostic pop

        if (color.alpha > 0.0) {
            return color;
        }
    }

    return GdkRGBA{0.0, 0.0, 0.0, 1.0};
}

bool widget_screen_rect(GtkWidget* widget, int& x, int& y, int& width, int& height) {
    if (!widget) {
        return false;
    }

    GdkWindow* window = gtk_widget_get_window(widget);
    if (!window) {
        return false;
    }

    GtkAllocation allocation = {};
    gtk_widget_get_allocation(widget, &allocation);
    gdk_window_get_origin(window, &x, &y);
    if (!gtk_widget_get_has_window(widget)) {
        x += allocation.x;
        y += allocation.y;
    }
    width = std::max(1, allocation.width);
    height = std::max(1, allocation.height);
    return true;
}

bool window_pixel_color(GtkWidget* widget, GdkWindow* source_window, int x, int y, GdkRGBA& color) {
    GdkWindow* source = source_window;
    if (!source) {
        GdkScreen* screen = widget ? gtk_widget_get_screen(widget) : nullptr;
        source = screen ? gdk_screen_get_root_window(screen) : nullptr;
    }
    if (!source) {
        return false;
    }

    if (source_window) {
        int origin_x = 0;
        int origin_y = 0;
        gdk_window_get_origin(source, &origin_x, &origin_y);
        x -= origin_x;
        y -= origin_y;
    }

    const int source_width = std::max(1, gdk_window_get_width(source));
    const int source_height = std::max(1, gdk_window_get_height(source));
    x = std::max(0, std::min(source_width - 1, x));
    y = std::max(0, std::min(source_height - 1, y));

    GdkPixbuf* pixbuf = gdk_pixbuf_get_from_window(source, x, y, 1, 1);
    if (!pixbuf) {
        return false;
    }

    const int channels = gdk_pixbuf_get_n_channels(pixbuf);
    const guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
    const bool valid = channels >= 3 && pixels != nullptr;
    if (valid) {
        color.red = pixels[0] / 255.0;
        color.green = pixels[1] / 255.0;
        color.blue = pixels[2] / 255.0;
        color.alpha = 1.0;
    }
    g_object_unref(pixbuf);
    return valid;
}

bool adjacent_screen_color(GtkWidget* widget, GdkWindow* source_window, GdkRGBA& color) {
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    if (!widget_screen_rect(widget, x, y, width, height)) {
        return false;
    }

    GdkScreen* screen = gtk_widget_get_screen(widget);
    GdkWindow* root = screen ? gdk_screen_get_root_window(screen) : nullptr;
    const int root_width = root ? gdk_window_get_width(root) : 0;
    if (x > 0 && window_pixel_color(widget, source_window, x - 1, y + (height / 2), color)) {
        return true;
    }
    if (root_width <= 0 || x + width >= root_width) {
        return false;
    }
    return window_pixel_color(widget, source_window, x + width, y + (height / 2), color);
}

bool widget_center_screen_color(GtkWidget* widget, GdkWindow* source_window, GdkRGBA& color) {
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    if (!widget_screen_rect(widget, x, y, width, height)) {
        return false;
    }
    return window_pixel_color(widget, source_window, x + (width / 2), y + (height / 2), color);
}

GdkRGBA xembed_background_color(GtkWidget* socket, GtkWidget* sample_widget, GtkWidget* fallback, GdkWindow* panel_window) {
    constexpr double visible_edge_delta = 0.04;
    constexpr double panel_color_delta = 0.18;

    const GdkRGBA panel_color = widget_background_color(socket, fallback);
    const GdkRGBA solid_panel_color = opaque_color(panel_color);

    GdkRGBA adjacent_color;
    GdkRGBA center_color;
    const bool have_panel_adjacent = adjacent_screen_color(sample_widget ? sample_widget : socket, panel_window, adjacent_color);
    if (have_panel_adjacent) {
        return adjacent_color;
    }

    const bool have_adjacent = adjacent_screen_color(sample_widget ? sample_widget : socket, nullptr, adjacent_color);
    const bool have_center = widget_center_screen_color(socket, nullptr, center_color);

    if (have_adjacent && have_center) {
        if (color_delta(adjacent_color, center_color) > visible_edge_delta) {
            return adjacent_color;
        }
        if (color_delta(adjacent_color, solid_panel_color) < panel_color_delta) {
            return adjacent_color;
        }
        if (panel_color.alpha < 0.999) {
            return composite_over(panel_color, center_color);
        }
        return adjacent_color;
    }

    if (have_adjacent) {
        if (panel_color.alpha >= 0.999 || color_delta(adjacent_color, solid_panel_color) < panel_color_delta) {
            return adjacent_color;
        }
        return composite_over(panel_color, adjacent_color);
    }

    if (have_center && panel_color.alpha < 0.999) {
        return composite_over(panel_color, center_color);
    }

    return solid_panel_color;
}

void set_xembed_window_background(Display* display, Window window, unsigned long bg_pixel) {
    if (!display || window == 0) {
        return;
    }

    with_x11_error_trap([&]() {
        XSetWindowBorderWidth(display, window, 0);
        XSetWindowBorder(display, window, 0);
        XSetWindowBackground(display, window, bg_pixel);
        XClearWindow(display, window);
        XFlush(display);
    });
}

void set_xembed_window_tree_background(Display* display, Window window, unsigned long bg_pixel) {
    set_xembed_window_background(display, window, bg_pixel);
    if (!display || window == 0) {
        return;
    }

    std::vector<Window> children_to_update;
    with_x11_error_trap([&]() {
        Window root = 0;
        Window parent = 0;
        Window* children = nullptr;
        unsigned int child_count = 0;
        if (XQueryTree(display, window, &root, &parent, &children, &child_count) && children) {
            children_to_update.assign(children, children + child_count);
            XFree(children);
        }
    });

    for (Window child : children_to_update) {
        set_xembed_window_tree_background(display, child, bg_pixel);
    }
}

void send_xembed_expose(GtkWidget* socket, Window window) {
    if (!socket || window == 0) {
        return;
    }

    GdkDisplay* display = gtk_widget_get_display(socket);
    if (!display || !GDK_IS_X11_DISPLAY(display)) {
        return;
    }

    GtkAllocation alloc = {};
    gtk_widget_get_allocation(socket, &alloc);
    XEvent xev = {};
    xev.xexpose.type = Expose;
    xev.xexpose.window = window;
    xev.xexpose.x = 0;
    xev.xexpose.y = 0;
    xev.xexpose.width = alloc.width;
    xev.xexpose.height = alloc.height;
    xev.xexpose.count = 0;

    Display* xdisplay = GDK_DISPLAY_XDISPLAY(display);
    gdk_x11_display_error_trap_push(display);
    XSendEvent(xdisplay, window, False, ExposureMask, &xev);
    XSync(xdisplay, False);
    gdk_x11_display_error_trap_pop_ignored(display);
}

WindowIdentity window_identity(Display* display, Window window) {
    WindowIdentity identity;
    XClassHint hint;
    hint.res_name = nullptr;
    hint.res_class = nullptr;
    if (XGetClassHint(display, window, &hint)) {
        if (hint.res_name) {
            identity.res_name = hint.res_name;
        }
        if (hint.res_class) {
            identity.res_class = hint.res_class;
        }
    }
    if (hint.res_name) {
        XFree(hint.res_name);
    }
    if (hint.res_class) {
        XFree(hint.res_class);
    }
    identity.title = window_text_property(display, window, "_NET_WM_NAME");
    if (identity.title.empty()) {
        identity.title = window_text_property(display, window, "WM_NAME");
    }
    return identity;
}

std::string identity_debug_name(const WindowIdentity& identity) {
    if (!identity.res_class.empty()) {
        return identity.res_class;
    }
    if (!identity.res_name.empty()) {
        return identity.res_name;
    }
    if (!identity.title.empty()) {
        return identity.title;
    }
    return "unknown";
}

bool legacy_tray_icon_allowed(const WindowIdentity& identity) {
    const std::string combined = lowercase_ascii(
        identity.res_name + " " + identity.res_class + " " + identity.title);

    const char* native_battery_tokens[] = {
        "xfce4-power-manager",
        "power-manager",
        "upower",
        "battery",
        nullptr
    };
    for (const char** token = native_battery_tokens; *token; ++token) {
        if (combined.find(*token) != std::string::npos) {
            return false;
        }
    }

    const char* allowed_tokens[] = {
        "nm-applet",
        "networkmanager",
        "network-manager",
        "blueman",
        "bluetooth",
        "udiskie",
        "removable",
        nullptr
    };

    for (const char** token = allowed_tokens; *token; ++token) {
        if (combined.find(*token) != std::string::npos) {
            return true;
        }
    }
    return false;
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
    bg_pixel_ = BlackPixel(display_, DefaultScreen(display_));
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
    GdkVisual* gdk_visual = screen ? gdk_screen_get_system_visual(screen) : nullptr;
    if (!gdk_visual && screen) {
        gdk_visual = gdk_screen_get_rgba_visual(screen);
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

void SystemTrayHost::refresh_background() {
    if (!display_ || !tray_box_) {
        return;
    }

    for (const auto& icon : icons_) {
        apply_background(icon.socket, icon.window);
    }
}

void SystemTrayHost::apply_background(GtkWidget* socket, Window icon_window) {
    if (!display_ || !socket) {
        return;
    }

    GtkWidget* fallback = tray_box_ ? gtk_widget_get_parent(tray_box_) : nullptr;
    GdkRGBA color = xembed_background_color(socket, tray_box_, fallback, filter_window_);
    bg_pixel_ = x11_pixel_for_color(display_, color);

    GdkWindow* gdk_window = gtk_widget_get_window(socket);
    if (gdk_window) {
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        gdk_window_set_background_rgba(gdk_window, &color);
        #pragma GCC diagnostic pop
        set_xembed_window_background(display_, GDK_WINDOW_XID(gdk_window), bg_pixel_);
    }

    GdkWindow* plug_gdk_window = GTK_IS_SOCKET(socket)
        ? gtk_socket_get_plug_window(GTK_SOCKET(socket))
        : nullptr;
    Window plug_window = plug_gdk_window ? GDK_WINDOW_XID(plug_gdk_window) : icon_window;
    set_xembed_window_tree_background(display_, plug_window, bg_pixel_);
    send_xembed_expose(socket, plug_window);
    gtk_widget_queue_draw(socket);
}

void SystemTrayHost::schedule_background_refresh(GtkWidget* socket, Window icon_window, guint delay_ms) {
    if (!socket) {
        return;
    }

    auto* refresh = new DeferredRefresh{this, socket, icon_window};
    g_object_ref(socket);
    if (delay_ms == 0) {
        g_idle_add(on_deferred_background_refresh, refresh);
    } else {
        g_timeout_add(delay_ms, on_deferred_background_refresh, refresh);
    }
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

    WindowIdentity identity = window_identity(display_, icon_window);
    if (!legacy_tray_icon_allowed(identity)) {
        if (tray_debug_enabled()) {
            g_printerr(
                "miloPanel tray ignored app status icon window=0x%lx class=%s\n",
                icon_window,
                identity_debug_name(identity).c_str());
        }
        return;
    }

    if (tray_debug_enabled()) {
        XWindowAttributes attributes;
        const bool has_attributes = XGetWindowAttributes(display_, icon_window, &attributes) != 0;
        g_printerr(
            "miloPanel tray dock request window=0x%lx class=%s map_state=%d size=%dx%d\n",
            icon_window,
            identity_debug_name(identity).c_str(),
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
    GdkVisual* tray_visual = screen ? gdk_screen_get_system_visual(screen) : nullptr;
    if (!tray_visual && screen) {
        tray_visual = gdk_screen_get_rgba_visual(screen);
    }
    if (tray_visual) {
        gtk_widget_set_visual(socket, tray_visual);
    }

    gtk_box_pack_start(GTK_BOX(tray_box_), socket, FALSE, FALSE, 0);
    g_signal_connect(socket, "draw", G_CALLBACK(+[](GtkWidget* widget, cairo_t* cr, gpointer) -> gboolean {
        GtkStyleContext* ctx = gtk_widget_get_style_context(widget);
        gtk_render_background(ctx, cr, 0, 0,
            gtk_widget_get_allocated_width(widget),
            gtk_widget_get_allocated_height(widget));
        return FALSE;
    }), nullptr);
    gtk_widget_realize(socket);

    apply_background(socket, icon_window);
    gtk_socket_add_id(GTK_SOCKET(socket), icon_window);
    gtk_widget_show(socket);
    schedule_background_refresh(socket, icon_window, 0);
    schedule_background_refresh(socket, icon_window, 150);
    icons_.push_back({icon_window, socket});
    g_signal_connect(socket, "plug-added", G_CALLBACK(+[](GtkSocket* socket, gpointer data) {
        auto* host = static_cast<SystemTrayHost*>(data);
        Window plug_window = gtk_socket_get_plug_window(socket)
            ? GDK_WINDOW_XID(gtk_socket_get_plug_window(socket))
            : 0;
        if (plug_window != 0) {
            host->apply_background(GTK_WIDGET(socket), plug_window);
            host->schedule_background_refresh(GTK_WIDGET(socket), plug_window, 0);
            host->schedule_background_refresh(GTK_WIDGET(socket), plug_window, 150);
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

gboolean SystemTrayHost::on_deferred_background_refresh(gpointer data) {
    auto* refresh = static_cast<DeferredRefresh*>(data);
    if (refresh && refresh->host && refresh->socket) {
        refresh->host->apply_background(refresh->socket, refresh->window);
        g_object_unref(refresh->socket);
    }
    delete refresh;
    return G_SOURCE_REMOVE;
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
