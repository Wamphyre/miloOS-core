#include "appmenu_registrar.hpp"

#include <cstring>
#include <vector>

namespace {

constexpr const char* REGISTRAR_NAME = "com.canonical.AppMenu.Registrar";
constexpr const char* REGISTRAR_PATH = "/com/canonical/AppMenu/Registrar";
constexpr const char* REGISTRAR_INTERFACE = "com.canonical.AppMenu.Registrar";

constexpr const char* REGISTRAR_XML = R"XML(
<node>
  <interface name="com.canonical.AppMenu.Registrar">
    <method name="RegisterWindow">
      <arg type="u" name="window_id" direction="in"/>
      <arg type="o" name="menu_object_path" direction="in"/>
    </method>
    <method name="UnregisterWindow">
      <arg type="u" name="window_id" direction="in"/>
    </method>
    <method name="GetMenuForWindow">
      <arg type="u" name="window_id" direction="in"/>
      <arg type="s" name="service" direction="out"/>
      <arg type="o" name="menu_object_path" direction="out"/>
    </method>
    <method name="GetMenus">
      <arg type="a(uso)" name="menus" direction="out"/>
    </method>
    <signal name="WindowRegistered">
      <arg type="u" name="window_id"/>
      <arg type="s" name="service"/>
      <arg type="o" name="menu_object_path"/>
    </signal>
    <signal name="WindowUnregistered">
      <arg type="u" name="window_id"/>
    </signal>
  </interface>
</node>
)XML";

constexpr guint32 DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER = 1;
constexpr guint32 DBUS_REQUEST_NAME_REPLY_IN_QUEUE = 2;
constexpr guint32 DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER = 4;

} // namespace

AppMenuRegistrar::AppMenuRegistrar() {
    start();
}

AppMenuRegistrar::~AppMenuRegistrar() {
    stop();
}

bool AppMenuRegistrar::owns_name() const {
    return owns_name_;
}

bool AppMenuRegistrar::menu_for_window(
    guint32 window_id,
    std::string* service,
    std::string* path) const {
    const auto menu = menus_.find(window_id);
    if (menu == menus_.end()) {
        return false;
    }
    if (service) {
        *service = menu->second.service;
    }
    if (path) {
        *path = menu->second.path;
    }
    return true;
}

void AppMenuRegistrar::start() {
    static const GDBusInterfaceVTable registrar_vtable = {
        on_method_call,
        nullptr,
        nullptr,
        {nullptr}
    };

    GError* error = nullptr;
    connection_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection_) {
        if (error) {
            g_warning("miloPanel: no se pudo iniciar el registrador AppMenu: %s", error->message);
            g_error_free(error);
        }
        return;
    }

    introspection_ = g_dbus_node_info_new_for_xml(REGISTRAR_XML, &error);
    if (!introspection_) {
        if (error) {
            g_warning("miloPanel: introspeccion AppMenu no valida: %s", error->message);
            g_error_free(error);
        }
        stop();
        return;
    }

    GDBusInterfaceInfo* interface_info =
        g_dbus_node_info_lookup_interface(introspection_, REGISTRAR_INTERFACE);
    object_registration_id_ = g_dbus_connection_register_object(
        connection_,
        REGISTRAR_PATH,
        interface_info,
        &registrar_vtable,
        this,
        nullptr,
        &error);
    if (!object_registration_id_) {
        if (error) {
            g_warning("miloPanel: no se pudo publicar el registrador AppMenu: %s", error->message);
            g_error_free(error);
        }
        stop();
        return;
    }

    name_owner_changed_signal_id_ = g_dbus_connection_signal_subscribe(
        connection_,
        "org.freedesktop.DBus",
        "org.freedesktop.DBus",
        "NameOwnerChanged",
        "/org/freedesktop/DBus",
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_name_owner_changed,
        this,
        nullptr);

    GVariant* result = g_dbus_connection_call_sync(
        connection_,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "RequestName",
        g_variant_new("(su)", REGISTRAR_NAME, 0u),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        &error);
    if (!result) {
        if (error) {
            g_warning("miloPanel: no se pudo reservar el nombre AppMenu: %s", error->message);
            g_error_free(error);
        }
        stop();
        return;
    }

    guint32 reply = 0;
    g_variant_get(result, "(u)", &reply);
    g_variant_unref(result);
    name_requested_ =
        reply == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
        reply == DBUS_REQUEST_NAME_REPLY_IN_QUEUE ||
        reply == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER;
    owns_name_ =
        reply == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
        reply == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER;
    if (!name_requested_) {
        g_warning("miloPanel: D-Bus rechazo el nombre del registrador AppMenu");
        stop();
    }
}

void AppMenuRegistrar::stop() {
    if (connection_ && name_requested_) {
        GError* error = nullptr;
        GVariant* result = g_dbus_connection_call_sync(
            connection_,
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "ReleaseName",
            g_variant_new("(s)", REGISTRAR_NAME),
            G_VARIANT_TYPE("(u)"),
            G_DBUS_CALL_FLAGS_NONE,
            1000,
            nullptr,
            &error);
        if (result) {
            g_variant_unref(result);
        }
        if (error) {
            g_error_free(error);
        }
        name_requested_ = false;
    }
    owns_name_ = false;
    if (connection_ && name_owner_changed_signal_id_) {
        g_dbus_connection_signal_unsubscribe(connection_, name_owner_changed_signal_id_);
        name_owner_changed_signal_id_ = 0;
    }
    if (connection_ && object_registration_id_) {
        g_dbus_connection_unregister_object(connection_, object_registration_id_);
        object_registration_id_ = 0;
    }
    if (introspection_) {
        g_dbus_node_info_unref(introspection_);
        introspection_ = nullptr;
    }
    if (connection_) {
        g_object_unref(connection_);
        connection_ = nullptr;
    }
    menus_.clear();
}

void AppMenuRegistrar::register_window(
    guint32 window_id,
    const char* service,
    const char* path) {
    MenuAddress address{service ? service : "", path ? path : "/"};
    menus_[window_id] = address;
    emit_window_registered(window_id, address);
}

void AppMenuRegistrar::unregister_window(guint32 window_id) {
    menus_.erase(window_id);
    emit_window_unregistered(window_id);
}

void AppMenuRegistrar::remove_service(const char* service) {
    if (!service || !*service) {
        return;
    }
    std::vector<guint32> removed_windows;
    for (const auto& entry : menus_) {
        if (entry.second.service == service) {
            removed_windows.push_back(entry.first);
        }
    }
    for (guint32 window_id : removed_windows) {
        menus_.erase(window_id);
        emit_window_unregistered(window_id);
    }
}

void AppMenuRegistrar::emit_window_registered(
    guint32 window_id,
    const MenuAddress& address) {
    if (!connection_) {
        return;
    }
    g_dbus_connection_emit_signal(
        connection_,
        nullptr,
        REGISTRAR_PATH,
        REGISTRAR_INTERFACE,
        "WindowRegistered",
        g_variant_new("(uso)", window_id, address.service.c_str(), address.path.c_str()),
        nullptr);
}

void AppMenuRegistrar::emit_window_unregistered(guint32 window_id) {
    if (!connection_) {
        return;
    }
    g_dbus_connection_emit_signal(
        connection_,
        nullptr,
        REGISTRAR_PATH,
        REGISTRAR_INTERFACE,
        "WindowUnregistered",
        g_variant_new("(u)", window_id),
        nullptr);
}

void AppMenuRegistrar::on_method_call(
    GDBusConnection*,
    const gchar* sender,
    const gchar*,
    const gchar*,
    const gchar* method_name,
    GVariant* parameters,
    GDBusMethodInvocation* invocation,
    gpointer user_data) {
    auto* self = static_cast<AppMenuRegistrar*>(user_data);

    if (std::strcmp(method_name, "RegisterWindow") == 0) {
        guint32 window_id = 0;
        const gchar* path = nullptr;
        g_variant_get(parameters, "(u&o)", &window_id, &path);
        self->register_window(window_id, sender, path);
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    if (std::strcmp(method_name, "UnregisterWindow") == 0) {
        guint32 window_id = 0;
        g_variant_get(parameters, "(u)", &window_id);
        self->unregister_window(window_id);
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    if (std::strcmp(method_name, "GetMenuForWindow") == 0) {
        guint32 window_id = 0;
        g_variant_get(parameters, "(u)", &window_id);
        const auto menu = self->menus_.find(window_id);
        if (menu == self->menus_.end()) {
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new("(so)", "", "/"));
        } else {
            g_dbus_method_invocation_return_value(
                invocation,
                g_variant_new(
                    "(so)",
                    menu->second.service.c_str(),
                    menu->second.path.c_str()));
        }
        return;
    }
    if (std::strcmp(method_name, "GetMenus") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(uso)"));
        for (const auto& menu : self->menus_) {
            g_variant_builder_add(
                &builder,
                "(uso)",
                menu.first,
                menu.second.service.c_str(),
                menu.second.path.c_str());
        }
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(@a(uso))", g_variant_builder_end(&builder)));
        return;
    }

    g_dbus_method_invocation_return_error(
        invocation,
        G_DBUS_ERROR,
        G_DBUS_ERROR_UNKNOWN_METHOD,
        "Metodo AppMenu desconocido: %s",
        method_name);
}

void AppMenuRegistrar::on_name_owner_changed(
    GDBusConnection*,
    const gchar*,
    const gchar*,
    const gchar*,
    const gchar*,
    GVariant* parameters,
    gpointer user_data) {
    const gchar* name = nullptr;
    const gchar* old_owner = nullptr;
    const gchar* new_owner = nullptr;
    g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
    auto* self = static_cast<AppMenuRegistrar*>(user_data);
    if (g_strcmp0(name, REGISTRAR_NAME) == 0) {
        const char* own_unique_name = self->connection_
            ? g_dbus_connection_get_unique_name(self->connection_)
            : nullptr;
        self->owns_name_ =
            own_unique_name && new_owner && g_strcmp0(own_unique_name, new_owner) == 0;
        if (!self->owns_name_) {
            self->menus_.clear();
        }
        return;
    }
    if (name && name[0] == ':' && old_owner && *old_owner && (!new_owner || !*new_owner)) {
        self->remove_service(name);
    }
}
