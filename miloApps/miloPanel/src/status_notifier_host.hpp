#pragma once

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <memory>
#include <map>

class StatusNotifierHost {
public:
    StatusNotifierHost();
    ~StatusNotifierHost();

    void start(GtkWidget* tray_box, int icon_size);
    bool running() const { return connection_ != nullptr; }

private:
    struct Pixmap {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> data;
    };

    struct Item {
        ~Item() {
            if (proxy) {
                g_object_unref(proxy);
            }
        }

        std::string service;
        std::string object_path;
        GDBusProxy* proxy = nullptr;
        GtkWidget* widget = nullptr;
        GtkWidget* image = nullptr;

        std::string category;
        std::string id;
        std::string title;
        std::string icon_name;
        std::string tooltip_title;
        std::string tooltip_body;
        Pixmap icon_pixmap;
    };

    GtkWidget* tray_box_ = nullptr;
    int icon_size_ = 16;
    GDBusConnection* connection_ = nullptr;
    GDBusProxy* watcher_ = nullptr;
    guint watcher_watch_id_ = 0;
    guint own_name_id_ = 0;
    guint own_watcher_reg_id_ = 0;
    guint watcher_signal_id_ = 0;

    std::map<std::string, std::unique_ptr<Item>> items_;

    void init_watcher_proxy(const std::string& name_owner);
    void provide_watcher();
    void register_host();
    void on_item_registered(const std::string& service);

    void track_item(const std::string& service);
    void untrack_item(const std::string& service);
    void clear_items();
    void setup_item(Item* item);
    void update_item_icon(Item* item);
    void read_icon_pixmap(Item* item, GVariant* value);
    GtkWidget* create_item_widget(Item* item);

    GdkPixbuf* load_icon(Item* item);
    GdkPixbuf* pixbuf_from_pixmap(const Pixmap& pixmap);

    static void on_name_appeared(GDBusConnection* connection, const gchar* name,
        const gchar* name_owner, gpointer data);
    static void on_name_vanished(GDBusConnection* connection, const gchar* name,
        gpointer data);
    static void on_watcher_signal(GDBusProxy* proxy, gchar* sender_name,
        gchar* signal_name, GVariant* parameters, gpointer data);
    static void on_watcher_proxy_ready(GObject* source, GAsyncResult* result,
        gpointer data);
    static void on_item_proxy_ready(GObject* source, GAsyncResult* result,
        gpointer data);
    static void on_item_signal(GDBusProxy* proxy, gchar* sender_name,
        gchar* signal_name, GVariant* parameters, gpointer data);
    static void on_item_properties_changed(GDBusProxy* proxy,
        GVariant* changed_properties, GStrv invalidated, gpointer data);
    static void on_bus_acquired(GDBusConnection* connection, const gchar* name,
        gpointer data);
    static void on_name_acquired(GDBusConnection* connection, const gchar* name,
        gpointer data);
    static void on_name_lost(GDBusConnection* connection, const gchar* name,
        gpointer data);
    static void handle_watcher_method(GDBusConnection* connection,
        const gchar* sender, const gchar* object_path, const gchar* interface_name,
        const gchar* method_name, GVariant* parameters,
        GDBusMethodInvocation* invocation, gpointer data);
    static void handle_watcher_props_method(GDBusConnection* connection,
        const gchar* sender, const gchar* object_path, const gchar* interface_name,
        const gchar* method_name, GVariant* parameters,
        GDBusMethodInvocation* invocation, gpointer data);
    static GVariant* handle_watcher_get_property(GDBusConnection* connection,
        const gchar* sender, const gchar* object_path, const gchar* interface_name,
        const gchar* property_name, GError** error, gpointer data);
};
