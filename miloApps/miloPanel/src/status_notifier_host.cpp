#include "status_notifier_host.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace {

constexpr const char* WATCHER_NAME = "org.kde.StatusNotifierWatcher";
constexpr const char* WATCHER_PATH = "/StatusNotifierWatcher";
constexpr const char* WATCHER_IFACE = "org.kde.StatusNotifierWatcher";
constexpr const char* ITEM_IFACE = "org.kde.StatusNotifierItem";

constexpr const char* WATCHER_XML =
    "<node>"
    "  <interface name='org.kde.StatusNotifierWatcher'>"
    "    <method name='RegisterStatusNotifierItem'>"
    "      <arg type='s' name='service' direction='in'/>"
    "    </method>"
    "    <method name='RegisterStatusNotifierHost'>"
    "      <arg type='s' name='service' direction='in'/>"
    "    </method>"
    "    <property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
    "    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
    "    <property name='ProtocolVersion' type='i' access='read'/>"
    "    <signal name='StatusNotifierItemRegistered'>"
    "      <arg type='s' name='service'/>"
    "    </signal>"
    "    <signal name='StatusNotifierItemUnregistered'>"
    "      <arg type='s' name='service'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

void set_image_from_pixbuf(GtkWidget* image, GdkPixbuf* pixbuf, int icon_size) {
    if (!pixbuf) {
        gtk_image_clear(GTK_IMAGE(image));
        return;
    }
    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);
    if (w != icon_size || h != icon_size) {
        GdkPixbuf* scaled = gdk_pixbuf_scale_simple(pixbuf, icon_size, icon_size, GDK_INTERP_BILINEAR);
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), scaled);
        g_object_unref(scaled);
    } else {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
    }
}

} // namespace

StatusNotifierHost::StatusNotifierHost() = default;

StatusNotifierHost::~StatusNotifierHost() {
    if (watcher_watch_id_ > 0) {
        g_bus_unwatch_name(watcher_watch_id_);
    }
    if (own_name_id_ > 0) {
        g_bus_unown_name(own_name_id_);
    }
    items_.clear();
    if (watcher_) g_object_unref(watcher_);
    if (connection_) g_object_unref(connection_);
}

void StatusNotifierHost::start(GtkWidget* tray_box, int icon_size) {
    tray_box_ = tray_box;
    icon_size_ = icon_size;

    watcher_watch_id_ = g_bus_watch_name(
        G_BUS_TYPE_SESSION,
        WATCHER_NAME,
        G_BUS_NAME_WATCHER_FLAGS_NONE,
        on_name_appeared,
        on_name_vanished,
        this, nullptr);
}

void StatusNotifierHost::on_name_appeared(GDBusConnection*, const gchar*,
    const gchar* name_owner, gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);
    host->init_watcher_proxy(name_owner);
}

void StatusNotifierHost::on_name_vanished(GDBusConnection*, const gchar*,
    gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);
    if (host->watcher_) {
        g_object_unref(host->watcher_);
        host->watcher_ = nullptr;
    }
    host->watcher_signal_id_ = 0;
    host->items_.clear();
    host->provide_watcher();
}

void StatusNotifierHost::init_watcher_proxy(const std::string&) {
    g_dbus_proxy_new_for_bus(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_NONE,
        nullptr,
        WATCHER_NAME,
        WATCHER_PATH,
        WATCHER_IFACE,
        nullptr,
        on_watcher_proxy_ready,
        this);
}

void StatusNotifierHost::on_watcher_proxy_ready(GObject*, GAsyncResult* result,
    gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);
    GError* error = nullptr;
    host->watcher_ = g_dbus_proxy_new_for_bus_finish(result, &error);
    if (error) {
        g_error_free(error);
        host->provide_watcher();
        return;
    }
    host->connection_ = g_dbus_proxy_get_connection(host->watcher_);
    g_object_ref(host->connection_);

    host->watcher_signal_id_ = g_signal_connect(
        host->watcher_, "g-signal",
        G_CALLBACK(on_watcher_signal), host);

    host->register_host();

    GVariant* registered = g_dbus_proxy_get_cached_property(
        host->watcher_, "RegisteredStatusNotifierItems");
    if (registered) {
        GVariantIter iter;
        gchar* service;
        g_variant_iter_init(&iter, registered);
        while (g_variant_iter_next(&iter, "s", &service)) {
            host->on_item_registered(service);
            g_free(service);
        }
        g_variant_unref(registered);
    }
}

void StatusNotifierHost::register_host() {
    if (!watcher_ || !connection_) return;
    const gchar* unique = g_dbus_connection_get_unique_name(connection_);
    g_dbus_proxy_call(watcher_, "RegisterStatusNotifierHost",
        g_variant_new("(s)", unique),
        G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
}

void StatusNotifierHost::provide_watcher() {
    if (own_watcher_reg_id_ > 0) return;

    own_name_id_ = g_bus_own_name(G_BUS_TYPE_SESSION, WATCHER_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired, on_name_acquired, on_name_lost,
        this, nullptr);
}

void StatusNotifierHost::on_bus_acquired(GDBusConnection* connection,
    const gchar*, gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);
    host->connection_ = connection;
    g_object_ref(host->connection_);

    GError* error = nullptr;
    GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(WATCHER_XML, &error);
    if (!node) {
        g_error_free(error);
        return;
    }

    const GDBusInterfaceVTable vtable = {
        handle_watcher_method, handle_watcher_get_property, nullptr,
        {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    host->own_watcher_reg_id_ = g_dbus_connection_register_object(
        connection, WATCHER_PATH,
        node->interfaces[0],
        &vtable, host, nullptr, &error);

    if (!error) {
        // Also register org.freedesktop.DBus.Properties for Get/GetAll support
        GDBusNodeInfo* props_node = g_dbus_node_info_new_for_xml(
            "<node>"
            "  <interface name='org.freedesktop.DBus.Properties'>"
            "    <method name='Get'>"
            "      <arg type='s' name='interface_name' direction='in'/>"
            "      <arg type='s' name='property_name' direction='in'/>"
            "      <arg type='v' name='value' direction='out'/>"
            "    </method>"
            "    <method name='GetAll'>"
            "      <arg type='s' name='interface_name' direction='in'/>"
            "      <arg type='a{sv}' name='properties' direction='out'/>"
            "    </method>"
            "  </interface>"
            "</node>", nullptr);
        if (props_node) {
            const GDBusInterfaceVTable props_vtable = {
                handle_watcher_props_method, nullptr, nullptr,
                {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}
            };
            g_dbus_connection_register_object(
                connection, WATCHER_PATH,
                props_node->interfaces[0],
                &props_vtable, host, nullptr, nullptr);
            g_dbus_node_info_unref(props_node);
        }
    }

    g_dbus_node_info_unref(node);

    if (error) {
        g_error_free(error);
        host->own_watcher_reg_id_ = 0;
    }
}

void StatusNotifierHost::on_name_acquired(GDBusConnection*, const gchar*, gpointer) {
    // Successfully acquired the watcher bus name
}

void StatusNotifierHost::on_name_lost(GDBusConnection*, const gchar*,
    gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);
    host->own_watcher_reg_id_ = 0;
    host->own_name_id_ = 0;
}

void StatusNotifierHost::handle_watcher_method(GDBusConnection*,
    const gchar*, const gchar*, const gchar*,
    const gchar* method_name, GVariant* parameters,
    GDBusMethodInvocation* invocation, gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);

    if (g_strcmp0(method_name, "RegisterStatusNotifierItem") == 0) {
        const gchar* service = nullptr;
        g_variant_get(parameters, "(&s)", &service);
        if (service && *service) {
            host->on_item_registered(service);
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
    } else if (g_strcmp0(method_name, "RegisterStatusNotifierHost") == 0) {
        g_dbus_method_invocation_return_value(invocation, nullptr);
    }
}

void StatusNotifierHost::handle_watcher_props_method(GDBusConnection*,
    const gchar*, const gchar*, const gchar*,
    const gchar* method_name, GVariant* parameters,
    GDBusMethodInvocation* invocation, gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);

    if (g_strcmp0(method_name, "Get") == 0) {
        const gchar* iface = nullptr,* prop = nullptr;
        g_variant_get(parameters, "(&s&s)", &iface, &prop);
        GVariant* value = handle_watcher_get_property(nullptr, nullptr, nullptr,
            iface, prop, nullptr, host);
        if (value) {
            g_dbus_method_invocation_return_value(invocation,
                g_variant_new("(v)", value));
            g_variant_unref(value);
        } else {
            g_dbus_method_invocation_return_error(invocation,
                G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "No such property '%s'", prop);
        }
    } else if (g_strcmp0(method_name, "GetAll") == 0) {
        g_dbus_method_invocation_return_error(invocation,
            G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
            "GetAll not supported");
    }
}

GVariant* StatusNotifierHost::handle_watcher_get_property(GDBusConnection*,
    const gchar*, const gchar*, const gchar*,
    const gchar* property_name, GError**, gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);

    if (g_strcmp0(property_name, "ProtocolVersion") == 0) {
        return g_variant_new_int32(0);
    }
    if (g_strcmp0(property_name, "IsStatusNotifierHostRegistered") == 0) {
        return g_variant_new_boolean(TRUE);
    }
    if (g_strcmp0(property_name, "RegisteredStatusNotifierItems") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE_STRING_ARRAY);
        for (const auto& [service, _] : host->items_) {
            g_variant_builder_add(&b, "s", service.c_str());
        }
        return g_variant_builder_end(&b);
    }
    return nullptr;
}

void StatusNotifierHost::on_watcher_signal(GDBusProxy*, gchar*,
    gchar* signal_name, GVariant* parameters, gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);

    if (g_strcmp0(signal_name, "StatusNotifierItemRegistered") == 0) {
        const gchar* service = nullptr;
        g_variant_get(parameters, "(&s)", &service);
        host->on_item_registered(service);
    } else if (g_strcmp0(signal_name, "StatusNotifierItemUnregistered") == 0) {
        const gchar* service = nullptr;
        g_variant_get(parameters, "(&s)", &service);
        if (service) {
            host->untrack_item(service);
        }
    }
}

void StatusNotifierHost::on_item_registered(const std::string& service) {
    if (items_.find(service) != items_.end()) return;
    track_item(service);
}

void StatusNotifierHost::track_item(const std::string& service) {
    auto [it, inserted] = items_.emplace(service, std::make_unique<Item>());
    if (!inserted) return;

    Item* item = it->second.get();
    item->service = service;
    item->object_path = "/StatusNotifierItem";

    g_dbus_proxy_new_for_bus(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_NONE,
        nullptr,
        service.c_str(),
        "/StatusNotifierItem",
        ITEM_IFACE,
        nullptr,
        on_item_proxy_ready,
        new std::pair<StatusNotifierHost*, std::string>(this, service));
}

void StatusNotifierHost::untrack_item(const std::string& service) {
    auto it = items_.find(service);
    if (it == items_.end()) return;

    if (it->second->widget && tray_box_) {
        gtk_container_remove(GTK_CONTAINER(tray_box_), it->second->widget);
    }
    items_.erase(it);
}

void StatusNotifierHost::on_item_proxy_ready(GObject*, GAsyncResult* result,
    gpointer data) {
    auto* pair = static_cast<std::pair<StatusNotifierHost*, std::string>*>(data);
    auto* host = pair->first;
    const std::string service = pair->second;
    delete pair;

    auto it = host->items_.find(service);
    if (it == host->items_.end()) return;

    GError* error = nullptr;
    GDBusProxy* proxy = g_dbus_proxy_new_for_bus_finish(result, &error);
    if (!proxy) {
        if (error) g_error_free(error);
        host->items_.erase(service);
        return;
    }

    it->second->proxy = proxy;
    host->setup_item(it->second.get());
}

void StatusNotifierHost::setup_item(Item* item) {
    // Read initial properties
    GVariant* v = g_dbus_proxy_get_cached_property(item->proxy, "IconName");
    if (v) {
        item->icon_name = g_variant_get_string(v, nullptr);
        g_variant_unref(v);
    }

    v = g_dbus_proxy_get_cached_property(item->proxy, "IconThemePath");
    if (v) {
        g_variant_unref(v);
    }

    v = g_dbus_proxy_get_cached_property(item->proxy, "TooltipTitle");
    if (v) {
        item->tooltip_title = g_variant_get_string(v, nullptr);
        g_variant_unref(v);
    }

    v = g_dbus_proxy_get_cached_property(item->proxy, "TooltipBody");
    if (v) {
        item->tooltip_body = g_variant_get_string(v, nullptr);
        g_variant_unref(v);
    }

    // Read IconPixmap
    v = g_dbus_proxy_get_cached_property(item->proxy, "IconPixmap");
    if (v) {
        GVariantIter iter;
        g_variant_iter_init(&iter, v);
        GVariant* child;
        int best_area = 0;
        while ((child = g_variant_iter_next_value(&iter))) {
            gint32 w = 0, h = 0;
            GVariant* data_v = nullptr;
            g_variant_get(child, "(ii@ay)", &w, &h, &data_v);
            if (data_v) {
                gsize px_len = g_variant_get_size(data_v);
                const guchar* px_data = static_cast<const guchar*>(g_variant_get_data(data_v));
                int area = w * h;
                if (area > best_area && w > 0 && h > 0 && px_len >= static_cast<gsize>(w * h * 4)) {
                    best_area = area;
                    item->icon_pixmap.width = w;
                    item->icon_pixmap.height = h;
                    item->icon_pixmap.data.assign(px_data, px_data + px_len);
                }
                g_variant_unref(data_v);
            }
            g_variant_unref(child);
        }
        g_variant_unref(v);
    }

    // Connect signals
    g_signal_connect(item->proxy, "g-signal",
        G_CALLBACK(on_item_signal), this);
    g_signal_connect(item->proxy, "g-properties-changed",
        G_CALLBACK(on_item_properties_changed), this);

    // Create widget
    item->widget = create_item_widget(item);
    if (item->widget && tray_box_) {
        gtk_box_pack_end(GTK_BOX(tray_box_), item->widget, FALSE, FALSE, 0);
        gtk_widget_show_all(item->widget);
    }

    update_item_icon(item);
}

void StatusNotifierHost::on_item_signal(GDBusProxy* proxy, gchar*,
    gchar* signal_name, GVariant*, gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);

    // Find item by proxy
    Item* item = nullptr;
    for (auto& [_, it] : host->items_) {
        if (it->proxy == proxy) { item = it.get(); break; }
    }
    if (!item) return;

    if (g_strcmp0(signal_name, "NewIcon") == 0 ||
        g_strcmp0(signal_name, "NewOverlayIcon") == 0) {
        // Re-read properties
        GVariant* v = g_dbus_proxy_get_cached_property(proxy, "IconName");
        if (v) {
            item->icon_name = g_variant_get_string(v, nullptr);
            g_variant_unref(v);
        }
        v = g_dbus_proxy_get_cached_property(proxy, "IconPixmap");
        if (v) {
            item->icon_pixmap.data.clear();
            GVariantIter iter;
            g_variant_iter_init(&iter, v);
            GVariant* child;
            int best_area = 0;
            while ((child = g_variant_iter_next_value(&iter))) {
                gint32 w = 0, h = 0;
                GVariant* data_v = nullptr;
                g_variant_get(child, "(ii@ay)", &w, &h, &data_v);
                if (data_v) {
                    gsize px_len = g_variant_get_size(data_v);
                    const guchar* px_data = static_cast<const guchar*>(g_variant_get_data(data_v));
                    int area = w * h;
                    if (area > best_area && w > 0 && h > 0 && px_len >= static_cast<gsize>(w * h * 4)) {
                        best_area = area;
                        item->icon_pixmap.width = w;
                        item->icon_pixmap.height = h;
                        item->icon_pixmap.data.assign(px_data, px_data + px_len);
                    }
                    g_variant_unref(data_v);
                }
                g_variant_unref(child);
            }
            g_variant_unref(v);
        }
        host->update_item_icon(item);
    } else if (g_strcmp0(signal_name, "NewTooltip") == 0) {
        GVariant* v = g_dbus_proxy_get_cached_property(proxy, "TooltipTitle");
        if (v) { item->tooltip_title = g_variant_get_string(v, nullptr); g_variant_unref(v); }
        v = g_dbus_proxy_get_cached_property(proxy, "TooltipBody");
        if (v) { item->tooltip_body = g_variant_get_string(v, nullptr); g_variant_unref(v); }
    }
}

void StatusNotifierHost::on_item_properties_changed(GDBusProxy* proxy,
    GVariant* changed, GStrv, gpointer data) {
    auto* host = static_cast<StatusNotifierHost*>(data);
    Item* item = nullptr;
    for (auto& [_, it] : host->items_) {
        if (it->proxy == proxy) { item = it.get(); break; }
    }
    if (!item || !changed) return;

    GVariantIter iter;
    const gchar* key;
    GVariant* value;
    g_variant_iter_init(&iter, changed);
    while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
        if (g_strcmp0(key, "IconName") == 0 && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
            item->icon_name = g_variant_get_string(value, nullptr);
        } else if (g_strcmp0(key, "IconPixmap") == 0) {
            item->icon_pixmap.data.clear();
            GVariantIter pi;
            g_variant_iter_init(&pi, value);
            GVariant* child;
            int best_area = 0;
            while ((child = g_variant_iter_next_value(&pi))) {
                gint32 w = 0, h = 0;
                GVariant* data_v = nullptr;
                g_variant_get(child, "(ii@ay)", &w, &h, &data_v);
                if (data_v) {
                    gsize px_len = g_variant_get_size(data_v);
                    const guchar* px_data = static_cast<const guchar*>(g_variant_get_data(data_v));
                    int area = w * h;
                    if (area > best_area && w > 0 && h > 0 && px_len >= static_cast<gsize>(w * h * 4)) {
                        best_area = area;
                        item->icon_pixmap.width = w;
                        item->icon_pixmap.height = h;
                        item->icon_pixmap.data.assign(px_data, px_data + px_len);
                    }
                    g_variant_unref(data_v);
                }
                g_variant_unref(child);
            }
        } else if (g_strcmp0(key, "TooltipTitle") == 0 && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
            item->tooltip_title = g_variant_get_string(value, nullptr);
        } else if (g_strcmp0(key, "TooltipBody") == 0 && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
            item->tooltip_body = g_variant_get_string(value, nullptr);
        }
        g_variant_unref(value);
    }
    host->update_item_icon(item);
}

GtkWidget* StatusNotifierHost::create_item_widget(Item* item) {
    GtkWidget* event_box = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), TRUE);
    gtk_widget_set_size_request(event_box, icon_size_, icon_size_);
    gtk_widget_set_valign(event_box, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(event_box), "milopanel-tray-socket");

    GtkWidget* img = gtk_image_new();
    gtk_container_add(GTK_CONTAINER(event_box), img);

    item->image = img;

    gtk_widget_set_app_paintable(event_box, TRUE);
    g_signal_connect(event_box, "draw", G_CALLBACK(+[](GtkWidget* widget, cairo_t* cr, gpointer) -> gboolean {
        GtkStyleContext* ctx = gtk_widget_get_style_context(widget);
        gtk_render_background(ctx, cr, 0, 0,
            gtk_widget_get_allocated_width(widget),
            gtk_widget_get_allocated_height(widget));
        return FALSE;
    }), nullptr);

    // Mouse events for Activate/ContextMenu
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEvent* event, gpointer) -> gboolean {
        guint button = 0;
        gdk_event_get_button(event, &button);
        if (button == 1) {
            // left click could trigger Activate - handled at higher level for now
        } else if (button == 3) {
            // right click could trigger ContextMenu
        }
        return TRUE;
    }), nullptr);

    return event_box;
}

void StatusNotifierHost::update_item_icon(Item* item) {
    if (!item->widget || !item->image) return;

    GdkPixbuf* pixbuf = load_icon(item);
    if (pixbuf) {
        set_image_from_pixbuf(item->image, pixbuf, icon_size_);
        g_object_unref(pixbuf);
    } else {
        gtk_image_clear(GTK_IMAGE(item->image));
    }

    // Tooltip
    std::string tooltip;
    if (!item->tooltip_title.empty()) {
        tooltip = item->tooltip_title;
    }
    if (!item->tooltip_body.empty()) {
        if (!tooltip.empty()) tooltip += "\n";
        tooltip += item->tooltip_body;
    }
    if (!tooltip.empty()) {
        gtk_widget_set_tooltip_text(item->widget, tooltip.c_str());
    } else {
        gtk_widget_set_has_tooltip(item->widget, FALSE);
    }
}

GdkPixbuf* StatusNotifierHost::load_icon(Item* item) {
    // Priority 1: IconPixmap (raw pixel data from the app)
    if (!item->icon_pixmap.data.empty()) {
        return pixbuf_from_pixmap(item->icon_pixmap);
    }

    // Priority 2: IconName (load from system icon theme)
    if (!item->icon_name.empty()) {
        GtkIconTheme* theme = gtk_icon_theme_get_default();
        int icon_size = icon_size_;
        GError* error = nullptr;
        GdkPixbuf* pixbuf = gtk_icon_theme_load_icon(theme, item->icon_name.c_str(),
            icon_size, GTK_ICON_LOOKUP_FORCE_SIZE, &error);
        if (pixbuf) return pixbuf;
        if (error) {
            g_error_free(error);
        }
    }

    return nullptr;
}

GdkPixbuf* StatusNotifierHost::pixbuf_from_pixmap(const Pixmap& pixmap) {
    if (pixmap.width <= 0 || pixmap.height <= 0 || pixmap.data.size() < 4) {
        return nullptr;
    }

    // The StatusNotifierItem spec says pixel data is in ARGB32 format
    // For Cairo CAIRO_FORMAT_ARGB32, on little-endian (x86) the byte order is B,G,R,A
    // which matches the KDE reference implementation's B,G,R,A byte order
    // We convert to GdkPixbuf format (R,G,B,A non-premultiplied)

    gsize expected = static_cast<gsize>(pixmap.width) * pixmap.height * 4;
    if (pixmap.data.size() < expected) return nullptr;

    GdkPixbuf* pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8,
        pixmap.width, pixmap.height);
    if (!pixbuf) return nullptr;

    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);

    for (int y = 0; y < pixmap.height; y++) {
        for (int x = 0; x < pixmap.width; x++) {
            gsize src_idx = (y * pixmap.width + x) * 4;
            int dst_idx = y * rowstride + x * 4;

            guchar b = pixmap.data[src_idx];
            guchar g = pixmap.data[src_idx + 1];
            guchar r = pixmap.data[src_idx + 2];
            guchar a = pixmap.data[src_idx + 3];

            // Convert from premultiplied alpha to non-premultiplied if needed
            if (a > 0) {
                pixels[dst_idx]     = (r * 255 + a / 2) / a;  // R
                pixels[dst_idx + 1] = (g * 255 + a / 2) / a;  // G
                pixels[dst_idx + 2] = (b * 255 + a / 2) / a;  // B
                pixels[dst_idx + 3] = a;                       // A
            } else {
                pixels[dst_idx]     = r;
                pixels[dst_idx + 1] = g;
                pixels[dst_idx + 2] = b;
                pixels[dst_idx + 3] = 0;
            }
        }
    }

    return pixbuf;
}
