#include "panel_settings.hpp"

#include <gtk/gtk.h>

#include <algorithm>

namespace {

void load_from_file(GKeyFile* key_file, const char* path) {
    GError* error = nullptr;
    g_key_file_load_from_file(key_file, path, G_KEY_FILE_KEEP_COMMENTS, &error);
    if (error) {
        g_error_free(error);
    }
}

int get_int_clamped(GKeyFile* key_file, const char* key, int fallback, int min_value, int max_value) {
    GError* error = nullptr;
    int value = g_key_file_get_integer(key_file, "Panel", key, &error);
    if (error) {
        g_error_free(error);
        value = fallback;
    }
    return std::max(min_value, std::min(max_value, value));
}

bool get_bool(GKeyFile* key_file, const char* key, bool fallback) {
    GError* error = nullptr;
    gboolean value = g_key_file_get_boolean(key_file, "Panel", key, &error);
    if (error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

std::string get_string(GKeyFile* key_file, const char* key, const std::string& fallback) {
    GError* error = nullptr;
    char* raw = g_key_file_get_string(key_file, "Panel", key, &error);
    if (error || !raw) {
        if (error) {
            g_error_free(error);
        }
        return fallback;
    }
    std::string value(raw);
    g_free(raw);
    return value;
}

} // namespace

PanelSettings PanelSettings::load() {
    PanelSettings settings;
    GKeyFile* key_file = g_key_file_new();

    load_from_file(key_file, "/etc/xdg/miloPanel/settings.ini");
    std::string user_path = std::string(g_get_user_config_dir()) + "/miloPanel/settings.ini";
    load_from_file(key_file, user_path.c_str());

    settings.height = get_int_clamped(key_file, "height", settings.height, 20, 48);
    settings.icon_size = get_int_clamped(key_file, "icon_size", settings.icon_size, 12, 32);
    settings.reserve_space = get_bool(key_file, "reserve_space", settings.reserve_space);
    settings.theme = get_string(key_file, "theme", settings.theme);
    settings.system_theme = get_string(key_file, "system_theme", settings.system_theme);
    settings.logo_light = get_string(key_file, "logo_light", settings.logo_light);
    settings.logo_dark = get_string(key_file, "logo_dark", settings.logo_dark);
    settings.menu_file = get_string(key_file, "menu_file", settings.menu_file);
    settings.clock_format = get_string(key_file, "clock_format", settings.clock_format);
    settings.clock_tooltip_format = get_string(key_file, "clock_tooltip_format", settings.clock_tooltip_format);
    settings.clock_font = get_string(key_file, "clock_font", settings.clock_font);

    g_key_file_unref(key_file);
    return settings;
}

bool PanelSettings::is_dark() const {
    if (theme == "dark") {
        return true;
    }
    if (theme == "light") {
        return false;
    }
    if (system_theme == "dark") {
        return true;
    }

    GtkSettings* gtk_settings = gtk_settings_get_default();
    char* gtk_theme = nullptr;
    g_object_get(gtk_settings, "gtk-theme-name", &gtk_theme, nullptr);
    std::string gtk_theme_name = gtk_theme ? gtk_theme : "";
    g_free(gtk_theme);

    std::transform(gtk_theme_name.begin(), gtk_theme_name.end(), gtk_theme_name.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return gtk_theme_name.find("dark") != std::string::npos;
}
