#include "settings.hpp"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
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
    int value = g_key_file_get_integer(key_file, "Dock", key, &error);
    if (error) {
        g_error_free(error);
        value = fallback;
    }
    return std::max(min_value, std::min(max_value, value));
}

std::string get_string(GKeyFile* key_file, const char* key, const std::string& fallback) {
    GError* error = nullptr;
    char* raw = g_key_file_get_string(key_file, "Dock", key, &error);
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

bool get_bool(GKeyFile* key_file, const char* key, bool fallback) {
    GError* error = nullptr;
    gboolean value = g_key_file_get_boolean(key_file, "Dock", key, &error);
    if (error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

std::string normalize_effect(const std::string& effect) {
    if (effect == "none") {
        return "none";
    }
    return "magnify";
}

} // namespace

DockSettings DockSettings::load() {
    DockSettings settings;
    GKeyFile* key_file = g_key_file_new();

    load_from_file(key_file, "/etc/xdg/miloDock/settings.ini");
    std::string user_path = std::string(g_get_user_config_dir()) + "/miloDock/settings.ini";
    load_from_file(key_file, user_path.c_str());

    settings.icon_size = get_int_clamped(key_file, "icon_size", settings.icon_size, 28, 64);
    settings.launcher_spacing = get_int_clamped(key_file, "launcher_spacing", settings.launcher_spacing, 0, 16);
    settings.auto_hide = get_bool(key_file, "auto_hide", settings.auto_hide);
    settings.effect = normalize_effect(get_string(key_file, "effect", settings.effect));
    settings.theme = get_string(key_file, "theme", settings.theme);
    settings.system_theme = get_string(key_file, "system_theme", settings.system_theme);

    g_key_file_unref(key_file);
    return settings;
}

void DockSettings::save() const {
    std::string directory = std::string(g_get_user_config_dir()) + "/miloDock";
    g_mkdir_with_parents(directory.c_str(), 0755);

    GKeyFile* key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "Dock", "icon_size", std::max(28, std::min(64, icon_size)));
    g_key_file_set_integer(key_file, "Dock", "launcher_spacing", std::max(0, std::min(16, launcher_spacing)));
    g_key_file_set_boolean(key_file, "Dock", "auto_hide", auto_hide);
    const std::string saved_effect = normalize_effect(effect);
    g_key_file_set_string(key_file, "Dock", "effect", saved_effect.c_str());
    g_key_file_set_string(key_file, "Dock", "theme", theme.c_str());
    g_key_file_set_string(key_file, "Dock", "system_theme", system_theme.c_str());

    gsize length = 0;
    gchar* data = g_key_file_to_data(key_file, &length, nullptr);
    std::string path = directory + "/settings.ini";
    if (data) {
        g_file_set_contents(path.c_str(), data, static_cast<gssize>(length), nullptr);
        g_free(data);
    }
    g_key_file_unref(key_file);
}

bool DockSettings::is_dark() const {
    if (theme == "dark") {
        return true;
    }
    if (theme == "light") {
        return false;
    }

    GtkSettings* gtk_settings = gtk_settings_get_default();
    char* gtk_theme = nullptr;
    g_object_get(gtk_settings, "gtk-theme-name", &gtk_theme, nullptr);
    std::string gtk_theme_name = gtk_theme ? gtk_theme : "";
    g_free(gtk_theme);

    std::string lowered = gtk_theme_name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
    return lowered.find("dark") != std::string::npos || system_theme == "dark";
}
