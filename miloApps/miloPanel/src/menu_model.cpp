#include "menu_model.hpp"

#include "i18n.hpp"

#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

struct ParseState {
    std::vector<MenuEntry> entries;
    bool in_layout = false;
    bool in_filename = false;
    std::string text;
};

void start_element(GMarkupParseContext*, const gchar* element_name, const gchar**, const gchar**, gpointer user_data, GError**) {
    auto* state = static_cast<ParseState*>(user_data);
    if (g_strcmp0(element_name, "Layout") == 0) {
        state->in_layout = true;
        return;
    }
    if (!state->in_layout) {
        return;
    }
    if (g_strcmp0(element_name, "Filename") == 0) {
        state->in_filename = true;
        state->text.clear();
    } else if (g_strcmp0(element_name, "Separator") == 0) {
        state->entries.push_back(MenuEntry{true, ""});
    }
}

void end_element(GMarkupParseContext*, const gchar* element_name, gpointer user_data, GError**) {
    auto* state = static_cast<ParseState*>(user_data);
    if (g_strcmp0(element_name, "Layout") == 0) {
        state->in_layout = false;
        return;
    }
    if (state->in_layout && state->in_filename && g_strcmp0(element_name, "Filename") == 0) {
        std::string value = state->text;
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); }));
        value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), value.end());
        if (!value.empty()) {
            state->entries.push_back(MenuEntry{false, value});
        }
        state->in_filename = false;
        state->text.clear();
    }
}

void text_node(GMarkupParseContext*, const gchar* text, gsize text_len, gpointer user_data, GError**) {
    auto* state = static_cast<ParseState*>(user_data);
    if (state->in_layout && state->in_filename) {
        state->text.append(text, text_len);
    }
}

GDesktopAppInfo* app_info_for_id(const std::string& desktop_id) {
    GDesktopAppInfo* info = g_desktop_app_info_new(desktop_id.c_str());
    if (info) {
        return info;
    }

    std::vector<fs::path> dirs;
    dirs.emplace_back(std::string(g_get_home_dir()) + "/.local/share/applications");
    const char* xdg_data_dirs = g_getenv("XDG_DATA_DIRS");
    std::string raw_dirs = xdg_data_dirs ? xdg_data_dirs : "/usr/local/share:/usr/share";
    size_t start = 0;
    while (start <= raw_dirs.size()) {
        size_t end = raw_dirs.find(':', start);
        std::string dir = raw_dirs.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!dir.empty()) {
            dirs.emplace_back(fs::path(dir) / "applications");
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    for (const auto& dir : dirs) {
        fs::path path = dir / desktop_id;
        if (fs::exists(path)) {
            return g_desktop_app_info_new_from_filename(path.c_str());
        }
    }
    return nullptr;
}

const char* fallback_icon_for_id(const std::string& desktop_id) {
    if (desktop_id == "miloupdate.desktop") {
        return "system-software-update";
    }
    if (desktop_id == "milo-settings.desktop") {
        return "preferences-desktop";
    }
    if (desktop_id == "milo-sleep.desktop") {
        return "system-suspend";
    }
    if (desktop_id == "milo-restart.desktop") {
        return "system-reboot";
    }
    if (desktop_id == "milo-shutdown.desktop") {
        return "system-shutdown";
    }
    if (desktop_id == "milo-logout.desktop") {
        return "system-log-out";
    }
    if (desktop_id == "milo-about.desktop") {
        return "help-about";
    }
    return "application-x-executable";
}

GtkWidget* image_for_app_info(GDesktopAppInfo* info, const std::string& desktop_id) {
    GtkWidget* image = nullptr;
    GIcon* icon = g_app_info_get_icon(G_APP_INFO(info));
    if (icon) {
        image = gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_MENU);
    } else {
        image = gtk_image_new_from_icon_name(fallback_icon_for_id(desktop_id), GTK_ICON_SIZE_MENU);
    }
    gtk_image_set_pixel_size(GTK_IMAGE(image), 16);
    return image;
}

GtkWidget* menu_item_with_icon(const char* label, GDesktopAppInfo* info, const std::string& desktop_id) {
    GtkWidget* item = gtk_menu_item_new();
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* image = image_for_app_info(info, desktop_id);
    GtkWidget* text = gtk_label_new(label ? label : desktop_id.c_str());

    gtk_label_set_xalign(GTK_LABEL(text), 0.0f);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_box_pack_start(GTK_BOX(row), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), text, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(item), row);
    return item;
}

std::string localized_desktop_string(GDesktopAppInfo* info, const char* key) {
    const char* filename = g_desktop_app_info_get_filename(info);
    if (!filename || !*filename) {
        return {};
    }

    GKeyFile* key_file = g_key_file_new();
    std::string result;
    if (g_key_file_load_from_file(key_file, filename, G_KEY_FILE_KEEP_TRANSLATIONS, nullptr)) {
        gchar* localized = g_key_file_get_locale_string(
            key_file,
            "Desktop Entry",
            key,
            i18n::language().c_str(),
            nullptr);
        if (localized) {
            result = localized;
            g_free(localized);
        }
    }
    g_key_file_unref(key_file);
    return result;
}

void normalize_process_cwd() {
    const char* home = g_get_home_dir();
    if (!home) {
        return;
    }
    if (g_chdir(home) == 0) {
        g_setenv("PWD", home, TRUE);
    }
}

} // namespace

std::vector<MenuEntry> load_menu_entries(const std::string& menu_file) {
    gchar* contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(menu_file.c_str(), &contents, &length, nullptr)) {
        return {
            {false, "milo-about.desktop"},
            {true, ""},
            {false, "milo-settings.desktop"},
            {false, "miloupdate.desktop"},
            {true, ""},
            {false, "milo-sleep.desktop"},
            {false, "milo-restart.desktop"},
            {false, "milo-shutdown.desktop"},
            {true, ""},
            {false, "milo-logout.desktop"},
        };
    }

    ParseState state;
    GMarkupParser parser{};
    parser.start_element = start_element;
    parser.end_element = end_element;
    parser.text = text_node;

    GMarkupParseContext* context = g_markup_parse_context_new(&parser, G_MARKUP_TREAT_CDATA_AS_TEXT, &state, nullptr);
    GError* error = nullptr;
    g_markup_parse_context_parse(context, contents, length, &error);
    g_markup_parse_context_end_parse(context, nullptr);
    if (error) {
        g_error_free(error);
    }
    g_markup_parse_context_free(context);
    g_free(contents);
    return state.entries;
}

GtkWidget* build_app_menu(const std::vector<MenuEntry>& entries, GtkWindow*) {
    GtkWidget* menu = gtk_menu_new();

    for (const auto& entry : entries) {
        if (entry.separator) {
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
            continue;
        }

        GDesktopAppInfo* info = app_info_for_id(entry.desktop_id);
        if (!info) {
            continue;
        }

        std::string localized_label = localized_desktop_string(info, "Name");
        const char* label = localized_label.empty() ? g_app_info_get_display_name(G_APP_INFO(info)) : localized_label.c_str();
        if (!label || !*label) {
            label = g_app_info_get_name(G_APP_INFO(info));
        }
        GtkWidget* item = menu_item_with_icon(label, info, entry.desktop_id);
        std::string localized_comment = localized_desktop_string(info, "Comment");
        gtk_widget_set_tooltip_text(
            item,
            localized_comment.empty() ? g_app_info_get_description(G_APP_INFO(info)) : localized_comment.c_str());
        g_signal_connect_data(item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer data) {
            auto* app_info = static_cast<GDesktopAppInfo*>(data);
            normalize_process_cwd();
            GError* error = nullptr;
            GAppLaunchContext* context = g_app_launch_context_new();
            g_app_info_launch(G_APP_INFO(app_info), nullptr, context, &error);
            if (error) {
                g_error_free(error);
            }
            g_object_unref(context);
        }), g_object_ref(info), [](gpointer data, GClosure*) {
            g_object_unref(data);
        }, G_CONNECT_DEFAULT);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_object_unref(info);
    }

    return menu;
}
