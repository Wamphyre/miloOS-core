#pragma once

#include <gio/gio.h>

#include <map>
#include <string>

class AppMenuRegistrar {
public:
    AppMenuRegistrar();
    ~AppMenuRegistrar();

    AppMenuRegistrar(const AppMenuRegistrar&) = delete;
    AppMenuRegistrar& operator=(const AppMenuRegistrar&) = delete;

    bool owns_name() const;
    bool menu_for_window(guint32 window_id, std::string* service, std::string* path) const;

private:
    struct MenuAddress {
        std::string service;
        std::string path;
    };

    GDBusConnection* connection_ = nullptr;
    GDBusNodeInfo* introspection_ = nullptr;
    guint object_registration_id_ = 0;
    guint name_owner_changed_signal_id_ = 0;
    bool name_requested_ = false;
    bool owns_name_ = false;
    std::map<guint32, MenuAddress> menus_;

    void start();
    void stop();
    void register_window(guint32 window_id, const char* service, const char* path);
    void unregister_window(guint32 window_id);
    void remove_service(const char* service);
    void emit_window_registered(guint32 window_id, const MenuAddress& address);
    void emit_window_unregistered(guint32 window_id);

    static void on_method_call(
        GDBusConnection* connection,
        const gchar* sender,
        const gchar* object_path,
        const gchar* interface_name,
        const gchar* method_name,
        GVariant* parameters,
        GDBusMethodInvocation* invocation,
        gpointer user_data);
    static void on_name_owner_changed(
        GDBusConnection* connection,
        const gchar* sender_name,
        const gchar* object_path,
        const gchar* interface_name,
        const gchar* signal_name,
        GVariant* parameters,
        gpointer user_data);
};
