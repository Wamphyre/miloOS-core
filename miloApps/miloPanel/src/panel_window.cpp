#include "panel_window.hpp"

#include "i18n.hpp"

#include <gdk/gdkx.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>


namespace {

constexpr int PANEL_REALIZED_HEIGHT = 24;
constexpr int LEADING_SEPARATOR_WIDTH = 6;
constexpr int MENU_BUTTON_WIDTH = 24;
constexpr int AFTER_MENU_SEPARATOR_WIDTH = 8;
constexpr int STATUS_SEPARATOR_WIDTH = 8;
constexpr int SYSTRAY_WIDTH = 25;
constexpr int STATUS_BUTTON_WIDTH = 24;
constexpr int CLOCK_WIDTH = 117;
constexpr int ACTIVE_WINDOW_FALLBACK_POLL_SECONDS = 1;
constexpr int BATTERY_POLL_SECONDS = 30;
constexpr int MENU_MAX_RETRIES_PER_WINDOW = 8;
constexpr int DBUSMENU_LAYOUT_DEPTH = 2;

std::string css_for_settings(const PanelSettings& settings) {
    const bool dark = settings.is_dark();
    const char* bg = dark ? "rgba(28, 28, 28, 0.94)" : "rgba(246, 246, 246, 0.94)";
    const char* border = dark ? "rgba(0, 0, 0, 0.55)" : "rgba(0, 0, 0, 0.22)";
    const char* fg = dark ? "#f2f2f2" : "#1d1d1f";
    const char* hover = dark ? "rgba(255,255,255,0.10)" : "rgba(0,0,0,0.08)";
    const char* menu_hover = dark ? "rgba(0, 122, 255, 0.72)" : "rgba(0, 122, 255, 0.85)";
    const char* menu_hover_fg = "#ffffff";

    std::ostringstream css;
    css
        << "window.milopanel-window { background-color: transparent; }\n"
        << ".milopanel-bar {\n"
        << "  background-color: " << bg << ";\n"
        << "  border-bottom: 1px solid " << border << ";\n"
        << "  color: " << fg << ";\n"
        << "  min-height: " << settings.height << "px;\n"
        << "}\n"
        << ".milopanel-tray-socket {\n"
        << "  background-color: transparent;\n"
        << "  border: 0;\n"
        << "  padding: 0;\n"
        << "  margin: 0;\n"
        << "}\n"
        << ".milopanel-button {\n"
        << "  background: transparent;\n"
        << "  border: 0;\n"
        << "  border-radius: 4px;\n"
        << "  box-shadow: none;\n"
        << "  color: " << fg << ";\n"
        << "  margin: 0;\n"
        << "  padding: 0;\n"
        << "  min-width: " << STATUS_BUTTON_WIDTH << "px;\n"
        << "  min-height: " << PANEL_REALIZED_HEIGHT << "px;\n"
        << "}\n"
        << ".milopanel-button:hover { background: " << hover << "; }\n"
        << ".milopanel-active {\n"
        << "  color: " << fg << ";\n"
        << "  padding: 0 8px;\n"
        << "  font-family: 'SF Pro Text', sans-serif;\n"
        << "  font-weight: 500;\n"
        << "  font-size: 10pt;\n"
        << "}\n"
        << ".milopanel-clock {\n"
        << "  color: " << fg << ";\n"
        << "  padding: 0;\n"
        << "  font-family: 'SF Pro Text', sans-serif;\n"
        << "  font-weight: 500;\n"
        << "  font-size: 10pt;\n"
        << "}\n"
        << ".milopanel-menubar {\n"
        << "  background: transparent;\n"
        << "  border: none;\n"
        << "  box-shadow: none;\n"
        << "  padding: 0;\n"
        << "  margin: 0;\n"
        << "  min-height: " << PANEL_REALIZED_HEIGHT << "px;\n"
        << "}\n"
        << ".milopanel-menubar > menuitem, .milopanel-menubar menuitem {\n"
        << "  color: " << fg << ";\n"
        << "  padding: 0 8px;\n"
        << "  min-height: " << PANEL_REALIZED_HEIGHT << "px;\n"
        << "  font-family: 'SF Pro Text', sans-serif;\n"
        << "  font-weight: 500;\n"
        << "  font-size: 10pt;\n"
        << "}\n"
        << ".milopanel-menubar > menuitem:hover, .milopanel-menubar menuitem:hover {\n"
        << "  background: " << menu_hover << ";\n"
        << "  color: " << menu_hover_fg << ";\n"
        << "  border-radius: 4px;\n"
        << "}\n"
        << ".milopanel-menu-button {\n"
        << "  background: transparent;\n"
        << "  border: 0;\n"
        << "  border-radius: 4px;\n"
        << "  box-shadow: none;\n"
        << "  color: " << fg << ";\n"
        << "  margin: 0;\n"
        << "  padding: 0 8px;\n"
        << "  min-height: " << PANEL_REALIZED_HEIGHT << "px;\n"
        << "  font-family: 'SF Pro Text', sans-serif;\n"
        << "  font-weight: 500;\n"
        << "  font-size: 10pt;\n"
        << "}\n"
        << ".milopanel-menu-button:hover { background: " << hover << "; }\n"
        << "menu {\n"
        << "  background-color: " << bg << ";\n"
        << "  border: 1px solid " << border << ";\n"
        << "  border-radius: 6px;\n"
        << "  padding: 4px 0;\n"
        << "  color: " << fg << ";\n"
        << "}\n"
        << "menu menuitem {\n"
        << "  color: " << fg << ";\n"
        << "  padding: 4px 10px;\n"
        << "  min-height: 20px;\n"
        << "  font-family: 'SF Pro Text', sans-serif;\n"
        << "  font-weight: 400;\n"
        << "  font-size: 10pt;\n"
        << "}\n"
        << "menu menuitem:hover {\n"
        << "  background: " << menu_hover << ";\n"
        << "  color: " << menu_hover_fg << ";\n"
        << "  border-radius: 4px;\n"
        << "}\n"
        << "menu menuitem:checked {\n"
        << "  background: " << menu_hover << ";\n"
        << "  color: " << menu_hover_fg << ";\n"
        << "}\n"
        << "menu separator {\n"
        << "  background-color: " << border << ";\n"
        << "  min-height: 1px;\n"
        << "  margin: 4px 10px;\n"
        << "}\n"
        << ".milopanel-tray {\n"
        << "  background: transparent;\n"
        << "  background-color: transparent;\n"
        << "  background-image: none;\n"
        << "  border: 0;\n"
        << "  box-shadow: none;\n"
        << "  padding: 0;\n"
        << "  margin: 0;\n"
        << "  min-height: " << PANEL_REALIZED_HEIGHT << "px;\n"
        << "}\n"
        << ".milopanel-tray-socket {\n"
        << "  background: transparent;\n"
        << "  background-color: transparent;\n"
        << "  background-image: none;\n"
        << "  border: 0;\n"
        << "  box-shadow: none;\n"
        << "  padding: 0;\n"
        << "  margin: 0;\n"
        << "  min-width: " << settings.icon_size << "px;\n"
        << "  min-height: " << PANEL_REALIZED_HEIGHT << "px;\n"
        << "}\n"
        << ".milopanel-tray *, .milopanel-tray-socket * {\n"
        << "  border: 0;\n"
        << "  box-shadow: none;\n"
        << "}\n"
        << "window.milopanel-popup {\n"
        << "  background-color: " << bg << ";\n"
        << "  color: " << fg << ";\n"
        << "  border-radius: 8px;\n"
        << "}\n"
        << "window.milopanel-popup frame {\n"
        << "  border: 1px solid " << border << ";\n"
        << "  border-radius: 8px;\n"
        << "  background-color: " << bg << ";\n"
        << "}\n";
    return css.str();
}

GtkWidget* fixed_spacer(int width) {
    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(spacer, width, PANEL_REALIZED_HEIGHT);
    gtk_widget_set_hexpand(spacer, FALSE);
    gtk_widget_set_opacity(spacer, 0.0);
    return spacer;
}

GdkRectangle primary_monitor_geometry() {
    GdkRectangle geometry{0, 0, 0, 0};
    GdkDisplay* display = gdk_display_get_default();
    GdkMonitor* monitor = display ? gdk_display_get_primary_monitor(display) : nullptr;
    if (!monitor && display && gdk_display_get_n_monitors(display) > 0) {
        monitor = gdk_display_get_monitor(display, 0);
    }
    if (monitor) {
        gdk_monitor_get_geometry(monitor, &geometry);
    }
    return geometry;
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

std::string shell_quote(const std::string& value) {
    gchar* quoted = g_shell_quote(value.c_str());
    std::string result = quoted ? quoted : "''";
    g_free(quoted);
    return result;
}

std::string user_special_dir(GUserDirectory directory, const std::string& fallback) {
    const char* path = g_get_user_special_dir(directory);
    if (path && *path) {
        return path;
    }
    return fallback;
}

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim_ascii(std::string value) {
    auto begin = std::find_if(value.begin(), value.end(), [](unsigned char c) {
        return !std::isspace(c);
    });
    auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::string read_text_file(const std::string& path) {
    gchar* contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(path.c_str(), &contents, &length, nullptr) || !contents) {
        return {};
    }
    std::string value(contents, length);
    g_free(contents);
    return trim_ascii(value);
}

int read_int_file(const std::string& path, int fallback = -1) {
    std::string value = read_text_file(path);
    if (value.empty()) {
        return fallback;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

struct BatteryInfo {
    bool present = false;
    int capacity = -1;
    std::string status;
};

BatteryInfo current_battery_info() {
    BatteryInfo info;
    GDir* dir = g_dir_open("/sys/class/power_supply", 0, nullptr);
    if (!dir) {
        return info;
    }

    const gchar* name = nullptr;
    while ((name = g_dir_read_name(dir)) != nullptr) {
        const std::string base = std::string("/sys/class/power_supply/") + name;
        if (lowercase_ascii(read_text_file(base + "/type")) != "battery") {
            continue;
        }

        const int capacity = read_int_file(base + "/capacity", -1);
        if (capacity < 0) {
            continue;
        }

        info.present = true;
        info.capacity = std::clamp(capacity, 0, 100);
        info.status = read_text_file(base + "/status");
        if (info.status.empty()) {
            info.status = "Unknown";
        }
        break;
    }

    g_dir_close(dir);
    return info;
}

std::string localized_battery_status(const std::string& status) {
    const std::string lower = lowercase_ascii(status);
    if (lower == "charging") {
        return i18n::tr("battery_charging");
    }
    if (lower == "discharging") {
        return i18n::tr("battery_discharging");
    }
    if (lower == "full") {
        return i18n::tr("battery_charged");
    }
    if (lower == "not charging") {
        return i18n::tr("battery_not_charging");
    }
    return i18n::tr("battery_unknown");
}

std::string first_available_icon(const std::vector<std::string>& candidates) {
    GtkIconTheme* theme = gtk_icon_theme_get_default();
    for (const std::string& candidate : candidates) {
        if (!theme || gtk_icon_theme_has_icon(theme, candidate.c_str())) {
            return candidate;
        }
    }
    return candidates.empty() ? "battery-missing-symbolic" : candidates.back();
}

std::string battery_icon_name(int capacity, const std::string& status) {
    const std::string lower_status = lowercase_ascii(status);
    const bool charging = lower_status == "charging";
    const bool full = lower_status == "full" || capacity >= 95;
    std::string bucket = "full";
    if (capacity <= 5) {
        bucket = "empty";
    } else if (capacity <= 15) {
        bucket = "caution";
    } else if (capacity <= 35) {
        bucket = "low";
    } else if (capacity <= 80) {
        bucket = "good";
    }

    std::vector<std::string> candidates;
    if (full) {
        candidates.push_back("battery-full-charged-symbolic");
    }
    if (charging && !full) {
        candidates.push_back("battery-" + bucket + "-charging-symbolic");
    }
    candidates.push_back("battery-" + bucket + "-symbolic");
    candidates.push_back("battery-good-symbolic");
    candidates.push_back("battery-missing-symbolic");
    return first_available_icon(candidates);
}

void add_unique_candidate(std::vector<std::string>* candidates, const std::string& value) {
    if (value.empty() || !candidates) {
        return;
    }
    if (std::find(candidates->begin(), candidates->end(), value) == candidates->end()) {
        candidates->push_back(value);
    }
}

void add_desktop_id_candidates(std::vector<std::string>* candidates, const std::string& raw_value) {
    if (!candidates || raw_value.empty()) {
        return;
    }

    std::string value = raw_value;
    const size_t slash = value.find_last_of('/');
    if (slash != std::string::npos) {
        value = value.substr(slash + 1);
    }
    std::replace(value.begin(), value.end(), ' ', '-');
    const std::string lower = lowercase_ascii(value);

    add_unique_candidate(candidates, value);
    add_unique_candidate(candidates, lower);
    if (value.size() < 8 || value.substr(value.size() - 8) != ".desktop") {
        add_unique_candidate(candidates, value + ".desktop");
    }
    if (lower.size() < 8 || lower.substr(lower.size() - 8) != ".desktop") {
        add_unique_candidate(candidates, lower + ".desktop");
    }
}

long x11_window_cardinal_property(
    Display* display,
    Window window,
    const char* property_name,
    long fallback = 0) {
    if (!display || window == 0 || !property_name) {
        return fallback;
    }
    Atom property = XInternAtom(display, property_name, False);
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    long result = fallback;
    GdkDisplay* gdk_display = gdk_display_get_default();
    const bool trap_x_error =
        gdk_display &&
        GDK_IS_X11_DISPLAY(gdk_display) &&
        GDK_DISPLAY_XDISPLAY(gdk_display) == display;
    if (trap_x_error) {
        gdk_x11_display_error_trap_push(gdk_display);
    }
    const int property_status = XGetWindowProperty(
            display,
            window,
            property,
            0,
            1,
            False,
            XA_CARDINAL,
            &actual_type,
            &actual_format,
            &nitems,
            &bytes_after,
            &data);
    const int x_error =
        trap_x_error ? gdk_x11_display_error_trap_pop(gdk_display) : 0;
    if (x_error == 0 &&
        property_status == Success &&
        data &&
        actual_format == 32 &&
        nitems > 0) {
        result = static_cast<long>(*reinterpret_cast<unsigned long*>(data));
    }
    if (data) {
        XFree(data);
    }
    return result;
}

std::string process_environment_value(long pid, const std::string& key) {
    if (pid <= 0 || key.empty()) {
        return {};
    }
    std::ifstream input(
        "/proc/" + std::to_string(pid) + "/environ",
        std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string environment = contents.str();
    const std::string prefix = key + "=";
    size_t offset = 0;
    while (offset < environment.size()) {
        const size_t end = environment.find('\0', offset);
        const size_t length =
            end == std::string::npos ? environment.size() - offset : end - offset;
        const std::string entry = environment.substr(offset, length);
        if (entry.rfind(prefix, 0) == 0) {
            return entry.substr(prefix.size());
        }
        if (end == std::string::npos) {
            break;
        }
        offset = end + 1;
    }
    return {};
}

std::string display_name_for_desktop_id(const std::string& desktop_id) {
    GDesktopAppInfo* info = g_desktop_app_info_new(desktop_id.c_str());
    if (!info) {
        return {};
    }

    std::string result;
    const char* filename = g_desktop_app_info_get_filename(info);
    if (filename && *filename) {
        GKeyFile* key_file = g_key_file_new();
        if (g_key_file_load_from_file(key_file, filename, G_KEY_FILE_KEEP_TRANSLATIONS, nullptr)) {
            gchar* localized = g_key_file_get_locale_string(
                key_file,
                "Desktop Entry",
                "Name",
                i18n::language().c_str(),
                nullptr);
            if (localized) {
                result = localized;
                g_free(localized);
            }
        }
        g_key_file_unref(key_file);
    }

    const char* label = g_app_info_get_display_name(G_APP_INFO(info));
    if (!label || !*label) {
        label = g_app_info_get_name(G_APP_INFO(info));
    }
    if (result.empty()) {
        result = label ? label : "";
    }
    g_object_unref(info);
    return result;
}

std::string resolve_app_display_name(
    const std::string& desktop_file,
    const std::string& gtk_app_id,
    const std::string& res_class,
    const std::string& res_name) {
    std::vector<std::string> candidates;
    add_desktop_id_candidates(&candidates, desktop_file);
    add_desktop_id_candidates(&candidates, gtk_app_id);
    add_desktop_id_candidates(&candidates, res_class);
    add_desktop_id_candidates(&candidates, res_name);

    for (const std::string& candidate : candidates) {
        std::string display_name = display_name_for_desktop_id(candidate);
        if (!display_name.empty()) {
            return display_name;
        }
    }
    return {};
}

std::string pretty_app_name(std::string value) {
    if (value.empty()) {
        return "miloOS";
    }
    std::replace(value.begin(), value.end(), '_', ' ');
    std::replace(value.begin(), value.end(), '-', ' ');
    const std::string lower = lowercase_ascii(value);
    if (lower == "xfce4 terminal") {
        return "Terminal";
    }
    if (lower == "firefox esr") {
        return "Firefox";
    }
    if (lower == "code") {
        return "Code";
    }
    bool capitalize_next = true;
    for (char& ch : value) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            capitalize_next = true;
            continue;
        }
        if (capitalize_next) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            capitalize_next = false;
        } else {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    return value;
}

bool string_identity_is_desktop(const std::string& value) {
    const std::string lower = lowercase_ascii(value);
    return lower == "desktop" ||
        lower == "xfdesktop" ||
        lower == "xfdesktop4" ||
        lower == "xfdesktop.desktop" ||
        lower == "org.xfce.xfdesktop" ||
        lower.find("xfdesktop") != std::string::npos;
}

bool window_has_type(Display* display, Window window, const char* type_name) {
    if (!display || window == 0) {
        return false;
    }

    Atom type_atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom requested_atom = XInternAtom(display, type_name, False);
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    const int status = XGetWindowProperty(
        display,
        window,
        type_atom,
        0,
        32,
        False,
        XA_ATOM,
        &actual_type,
        &actual_format,
        &nitems,
        &bytes_after,
        &data);

    bool found = false;
    if (status == Success && data && actual_format == 32) {
        auto* atoms = reinterpret_cast<Atom*>(data);
        for (unsigned long i = 0; i < nitems; ++i) {
            if (atoms[i] == requested_atom) {
                found = true;
                break;
            }
        }
    }
    if (data) {
        XFree(data);
    }
    return found;
}

bool menu_debug_enabled() {
    const char* value = g_getenv("MILO_PANEL_DEBUG_MENU");
    return value && *value;
}

bool appmenu_debug_enabled() {
    const char* value = g_getenv("MILO_PANEL_DEBUG_APPMENU");
    return value && *value;
}

bool external_appmenu_enabled() {
    const char* value = g_getenv("MILO_PANEL_USE_EXTERNAL_APPMENU");
    return value && *value && g_strcmp0(value, "0") != 0;
}

void startup_trace(const char* message) {
    const char* value = g_getenv("MILO_PANEL_DEBUG_STARTUP");
    if (value && *value) {
        g_printerr("miloPanel startup: %s\n", message);
    }
}

void dump_widget_tree(GtkWidget* widget, int depth = 0) {
    if (!widget) {
        return;
    }
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    int min_width = 0;
    int natural_width = 0;
    int min_height = 0;
    int natural_height = 0;
    gtk_widget_get_preferred_width(widget, &min_width, &natural_width);
    gtk_widget_get_preferred_height(widget, &min_height, &natural_height);
    g_printerr(
        "%*s%s visible=%d child-visible=%d mapped=%d alloc=%dx%d pref=%dx%d nat=%dx%d\n",
        depth * 2,
        "",
        G_OBJECT_TYPE_NAME(widget),
        gtk_widget_get_visible(widget) ? 1 : 0,
        gtk_widget_get_child_visible(widget) ? 1 : 0,
        gtk_widget_get_mapped(widget) ? 1 : 0,
        allocation.width,
        allocation.height,
        min_width,
        min_height,
        natural_width,
        natural_height);
    if (!GTK_IS_CONTAINER(widget)) {
        return;
    }
    GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (GList* item = children; item != nullptr; item = item->next) {
        dump_widget_tree(GTK_WIDGET(item->data), depth + 1);
    }
    g_list_free(children);
}

void prepare_appmenu_widget_tree(GtkWidget* widget) {
    if (!widget) {
        return;
    }
    gtk_widget_set_valign(widget, GTK_ALIGN_FILL);
    gtk_widget_set_vexpand(widget, TRUE);

    if (GTK_IS_SCROLLED_WINDOW(widget)) {
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(widget), PANEL_REALIZED_HEIGHT);
        gtk_widget_set_size_request(widget, -1, PANEL_REALIZED_HEIGHT);
    } else if (GTK_IS_VIEWPORT(widget)) {
        gtk_widget_set_size_request(widget, -1, PANEL_REALIZED_HEIGHT);
    } else if (GTK_IS_MENU_BAR(widget)) {
        gtk_style_context_add_class(gtk_widget_get_style_context(widget), "milopanel-menubar");
        gtk_widget_set_size_request(widget, 1, PANEL_REALIZED_HEIGHT);
        gtk_widget_set_hexpand(widget, TRUE);
    } else if (GTK_IS_MENU_ITEM(widget)) {
        gtk_widget_set_size_request(widget, -1, PANEL_REALIZED_HEIGHT);
    }

    if (!GTK_IS_CONTAINER(widget)) {
        return;
    }
    GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (GList* item = children; item != nullptr; item = item->next) {
        prepare_appmenu_widget_tree(GTK_WIDGET(item->data));
    }
    g_list_free(children);
}

std::string menu_item_label(GtkWidget* item) {
    if (!GTK_IS_BIN(item)) {
        return "";
    }
    GtkWidget* child = gtk_bin_get_child(GTK_BIN(item));
    if (child && GTK_IS_LABEL(child)) {
        const gchar* text = gtk_label_get_text(GTK_LABEL(child));
        return text ? text : "";
    }
    return "";
}

GtkWidget* find_descendant_menu_bar(GtkWidget* widget) {
    if (!widget) {
        return nullptr;
    }
    if (GTK_IS_MENU_BAR(widget)) {
        return widget;
    }
    if (!GTK_IS_CONTAINER(widget)) {
        return nullptr;
    }
    GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
    for (GList* item = children; item != nullptr; item = item->next) {
        GtkWidget* found = find_descendant_menu_bar(GTK_WIDGET(item->data));
        if (found) {
            g_list_free(children);
            return found;
        }
    }
    g_list_free(children);
    return nullptr;
}

void flatten_appmenu_widget(GtkWidget* widget) {
    if (!GTK_IS_BIN(widget)) {
        return;
    }
    GtkWidget* menu_bar = find_descendant_menu_bar(widget);
    GtkWidget* current_child = gtk_bin_get_child(GTK_BIN(widget));
    if (!menu_bar || !current_child || menu_bar == current_child) {
        return;
    }

    GtkWidget* menu_parent = gtk_widget_get_parent(menu_bar);
    if (!menu_parent || !GTK_IS_CONTAINER(menu_parent)) {
        return;
    }

    g_object_ref(menu_bar);
    gtk_container_remove(GTK_CONTAINER(menu_parent), menu_bar);
    gtk_container_remove(GTK_CONTAINER(widget), current_child);
    gtk_container_add(GTK_CONTAINER(widget), menu_bar);
    gtk_widget_set_size_request(menu_bar, 1, PANEL_REALIZED_HEIGHT);
    gtk_widget_set_hexpand(menu_bar, TRUE);
    gtk_widget_set_halign(menu_bar, GTK_ALIGN_FILL);
    gtk_widget_set_valign(menu_bar, GTK_ALIGN_FILL);
    gtk_widget_set_child_visible(menu_bar, TRUE);
    gtk_widget_show(menu_bar);
    g_object_unref(menu_bar);
}

struct GMenuActionBinding {
    std::string detailed_action;
    GVariant* target = nullptr;
};

void free_gmenu_action_binding(gpointer data) {
    auto* binding = static_cast<GMenuActionBinding*>(data);
    if (!binding) {
        return;
    }
    if (binding->target) {
        g_variant_unref(binding->target);
    }
    delete binding;
}

} // namespace

PanelWindow::PanelWindow(GtkApplication* app, bool reserve_space_override)
    : app_(app), settings_(PanelSettings::load()), reserve_space_override_(reserve_space_override) {
    startup_trace("constructor begin");
    build_ui();
    startup_trace("ui built");
    load_css();
    startup_trace("css loaded");
    update_clock();
    startup_trace("clock updated");
    update_battery();
    startup_trace("battery updated");
    update_volume();
    startup_trace("volume updated");
    update_active_window();
    startup_trace("active window updated");
    clock_source_id_ = g_timeout_add_seconds(10, on_clock_timeout, this);
    battery_source_id_ = g_timeout_add_seconds(BATTERY_POLL_SECONDS, on_battery_timeout, this);
    volume_source_id_ = g_timeout_add_seconds(15, on_volume_timeout, this);
    setup_active_window_tracking();
    setup_registrar_signals();
    if (appmenu_debug_enabled()) {
        g_timeout_add(1200, +[](gpointer data) -> gboolean {
            auto* panel = static_cast<PanelWindow*>(data);
            g_printerr("miloPanel bar widget tree:\n");
            dump_widget_tree(panel->bar_);
            g_printerr("miloPanel AppMenu widget tree:\n");
            dump_widget_tree(panel->menu_bar_);
            return G_SOURCE_REMOVE;
        }, this);
    }
}

PanelWindow::~PanelWindow() {
    if (clock_source_id_) {
        g_source_remove(clock_source_id_);
    }
    if (battery_source_id_) {
        g_source_remove(battery_source_id_);
    }
    if (volume_source_id_) {
        g_source_remove(volume_source_id_);
    }
    if (active_source_id_) {
        g_source_remove(active_source_id_);
    }
    if (active_refresh_source_id_) {
        g_source_remove(active_refresh_source_id_);
    }
    if (tray_background_refresh_source_id_) {
        g_source_remove(tray_background_refresh_source_id_);
    }
    if (root_filter_installed_) {
        gdk_window_remove_filter(root_filter_window_, on_root_event, this);
    }
    if (root_filter_window_) {
        g_object_unref(root_filter_window_);
    }
    if (css_provider_) {
        g_object_unref(css_provider_);
    }
    if (volume_popup_) {
        gtk_widget_destroy(volume_popup_);
    }
    if (clock_popup_) {
        gtk_widget_destroy(clock_popup_);
    }
    cleanup_menu_client();
    if (session_bus_ && registrar_window_registered_signal_id_) {
        g_dbus_connection_signal_unsubscribe(session_bus_, registrar_window_registered_signal_id_);
    }
    if (session_bus_ && registrar_window_unregistered_signal_id_) {
        g_dbus_connection_signal_unsubscribe(session_bus_, registrar_window_unregistered_signal_id_);
    }
    if (menu_refresh_source_id_) {
        g_source_remove(menu_refresh_source_id_);
    }
    if (session_bus_) {
        g_object_unref(session_bus_);
    }
}

void PanelWindow::build_ui() {
    startup_trace("build_ui begin");
    window_ = gtk_application_window_new(app_);
    startup_trace("window created");
    gtk_window_set_title(GTK_WINDOW(window_), "miloPanel");
    gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window_), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(window_), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(window_), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window_), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window_), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(window_), TRUE);
    gtk_window_stick(GTK_WINDOW(window_));
    gtk_window_set_type_hint(GTK_WINDOW(window_), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_widget_set_app_paintable(window_, FALSE);
    gtk_widget_set_name(window_, "milopanel-window");
    gtk_style_context_add_class(gtk_widget_get_style_context(window_), "milopanel-window");
    startup_trace("window hints set");

    GdkScreen* screen = gtk_widget_get_screen(window_);
    GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
    if (visual) {
        gtk_widget_set_visual(window_, visual);
    }
    g_signal_connect(screen, "size-changed", G_CALLBACK(on_screen_size_changed), this);
    startup_trace("screen connected");

    bar_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(bar_, -1, settings_.height);
    gtk_widget_set_hexpand(bar_, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar_), "milopanel-bar");
    gtk_container_add(GTK_CONTAINER(window_), bar_);
    startup_trace("bar created");

    gtk_box_pack_start(GTK_BOX(bar_), fixed_spacer(LEADING_SEPARATOR_WIDTH), FALSE, FALSE, 0);

    build_menu_button();
    startup_trace("menu button built");

    gtk_box_pack_start(GTK_BOX(bar_), fixed_spacer(AFTER_MENU_SEPARATOR_WIDTH), FALSE, FALSE, 0);

    active_label_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(active_label_), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(active_label_), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(active_label_), 22);
    gtk_style_context_add_class(gtk_widget_get_style_context(active_label_), "milopanel-active");
    gtk_widget_set_size_request(active_label_, -1, PANEL_REALIZED_HEIGHT);
    gtk_widget_set_hexpand(active_label_, FALSE);
    gtk_box_pack_start(GTK_BOX(bar_), active_label_, FALSE, FALSE, 0);

    native_appmenu_ = create_appmenu_widget();
    menu_bar_ = gtk_menu_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(menu_bar_), "milopanel-menubar");
    gtk_widget_set_size_request(menu_bar_, 1, PANEL_REALIZED_HEIGHT);
    gtk_widget_set_hexpand(menu_bar_, TRUE);
    gtk_widget_set_halign(menu_bar_, GTK_ALIGN_FILL);
    gtk_widget_set_valign(menu_bar_, GTK_ALIGN_FILL);
    GtkWidget* menu_slot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(menu_slot, 1, PANEL_REALIZED_HEIGHT);
    gtk_widget_set_hexpand(menu_slot, TRUE);
    if (native_appmenu_) {
        gtk_style_context_add_class(gtk_widget_get_style_context(native_appmenu_), "milopanel-menubar");
        gtk_widget_set_size_request(native_appmenu_, 1, PANEL_REALIZED_HEIGHT);
        gtk_widget_set_hexpand(native_appmenu_, TRUE);
        gtk_widget_set_halign(native_appmenu_, GTK_ALIGN_FILL);
        gtk_widget_set_valign(native_appmenu_, GTK_ALIGN_FILL);
        gtk_box_pack_start(GTK_BOX(menu_slot), native_appmenu_, TRUE, TRUE, 0);
        gtk_widget_hide(active_label_);
        gtk_widget_hide(menu_bar_);
    }
    gtk_box_pack_start(GTK_BOX(menu_slot), menu_bar_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bar_), menu_slot, TRUE, TRUE, 0);

    tray_box_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_valign(tray_box_, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(tray_box_, SYSTRAY_WIDTH, PANEL_REALIZED_HEIGHT);
    gtk_style_context_add_class(gtk_widget_get_style_context(tray_box_), "milopanel-tray");
    gtk_box_pack_start(GTK_BOX(bar_), tray_box_, FALSE, FALSE, 0);

    battery_button_ = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(battery_button_), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(battery_button_, FALSE);
    gtk_widget_set_no_show_all(battery_button_, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(battery_button_), "milopanel-button");
    battery_image_ = gtk_image_new_from_icon_name("battery-good-symbolic", GTK_ICON_SIZE_MENU);
    gtk_image_set_pixel_size(GTK_IMAGE(battery_image_), settings_.icon_size);
    gtk_widget_set_size_request(battery_button_, STATUS_BUTTON_WIDTH, PANEL_REALIZED_HEIGHT);
    gtk_container_add(GTK_CONTAINER(battery_button_), battery_image_);
    gtk_widget_set_tooltip_text(battery_button_, i18n::tr("battery").c_str());
    gtk_box_pack_start(GTK_BOX(bar_), battery_button_, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(bar_), fixed_spacer(STATUS_SEPARATOR_WIDTH), FALSE, FALSE, 0);

    volume_button_ = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(volume_button_), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(volume_button_, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(volume_button_), "milopanel-button");
    volume_image_ = gtk_image_new_from_icon_name("audio-volume-high-symbolic", GTK_ICON_SIZE_MENU);
    gtk_image_set_pixel_size(GTK_IMAGE(volume_image_), settings_.icon_size);
    volume_label_ = nullptr;
    gtk_widget_set_size_request(volume_button_, STATUS_BUTTON_WIDTH, PANEL_REALIZED_HEIGHT);
    gtk_container_add(GTK_CONTAINER(volume_button_), volume_image_);
    gtk_widget_add_events(volume_button_, GDK_SCROLL_MASK | GDK_BUTTON_PRESS_MASK);
    gtk_widget_set_tooltip_text(volume_button_, "PulseAudio");
    gtk_box_pack_start(GTK_BOX(bar_), volume_button_, FALSE, FALSE, 0);

    // Initialize Volume Popup Window
    volume_popup_ = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_transient_for(GTK_WINDOW(volume_popup_), GTK_WINDOW(window_));
    gtk_window_set_keep_above(GTK_WINDOW(volume_popup_), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(volume_popup_), "milopanel-popup");

    GtkWidget* vol_frame = gtk_frame_new(nullptr);
    gtk_frame_set_shadow_type(GTK_FRAME(vol_frame), GTK_SHADOW_OUT);
    gtk_container_add(GTK_CONTAINER(volume_popup_), vol_frame);

    GtkWidget* vol_pop_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vol_pop_box), 8);
    GtkWidget* vol_output_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* vol_output_title = gtk_label_new(i18n::tr("volume_output").c_str());
    gtk_label_set_xalign(GTK_LABEL(vol_output_title), 0.0f);
    gtk_widget_set_size_request(vol_output_title, 48, -1);
    vol_popup_mute_btn_ = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(vol_popup_mute_btn_), GTK_RELIEF_NONE);
    GtkWidget* vol_mute_img = gtk_image_new_from_icon_name("audio-volume-high-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_image(GTK_BUTTON(vol_popup_mute_btn_), vol_mute_img);
    gtk_button_set_always_show_image(GTK_BUTTON(vol_popup_mute_btn_), TRUE);
    vol_popup_scale_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
    gtk_widget_set_size_request(vol_popup_scale_, 130, -1);
    gtk_scale_set_draw_value(GTK_SCALE(vol_popup_scale_), FALSE);
    vol_popup_label_ = gtk_label_new("0%");
    gtk_widget_set_size_request(vol_popup_label_, 36, -1);
    gtk_box_pack_start(GTK_BOX(vol_output_box), vol_output_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vol_output_box), vol_popup_mute_btn_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vol_output_box), vol_popup_scale_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vol_output_box), vol_popup_label_, FALSE, FALSE, 0);

    GtkWidget* vol_input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* vol_input_title = gtk_label_new(i18n::tr("volume_input").c_str());
    gtk_label_set_xalign(GTK_LABEL(vol_input_title), 0.0f);
    gtk_widget_set_size_request(vol_input_title, 48, -1);
    input_popup_mute_btn_ = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(input_popup_mute_btn_), GTK_RELIEF_NONE);
    GtkWidget* input_mute_img = gtk_image_new_from_icon_name("microphone-sensitivity-high-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_image(GTK_BUTTON(input_popup_mute_btn_), input_mute_img);
    gtk_button_set_always_show_image(GTK_BUTTON(input_popup_mute_btn_), TRUE);
    input_popup_scale_ = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
    gtk_widget_set_size_request(input_popup_scale_, 130, -1);
    gtk_scale_set_draw_value(GTK_SCALE(input_popup_scale_), FALSE);
    input_popup_label_ = gtk_label_new("0%");
    gtk_widget_set_size_request(input_popup_label_, 36, -1);
    gtk_box_pack_start(GTK_BOX(vol_input_box), vol_input_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vol_input_box), input_popup_mute_btn_, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vol_input_box), input_popup_scale_, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vol_input_box), input_popup_label_, FALSE, FALSE, 0);

    GtkWidget* vol_sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget* vol_pavu_btn = gtk_button_new_with_label(i18n::tr("audio_mixer").c_str());
    gtk_button_set_relief(GTK_BUTTON(vol_pavu_btn), GTK_RELIEF_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(vol_pavu_btn), "milopanel-button");
    gtk_box_pack_start(GTK_BOX(vol_pop_box), vol_output_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vol_pop_box), vol_input_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vol_pop_box), vol_sep, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vol_pop_box), vol_pavu_btn, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(vol_frame), vol_pop_box);
    gtk_widget_show_all(vol_frame);
    startup_trace("volume popup built");

    g_signal_connect(volume_popup_, "map", G_CALLBACK(on_popup_map), this);
    g_signal_connect(volume_popup_, "unmap", G_CALLBACK(on_popup_unmap), this);
    g_signal_connect(volume_popup_, "button-press-event", G_CALLBACK(on_popup_button_press), this);

    gtk_box_pack_start(GTK_BOX(bar_), fixed_spacer(STATUS_SEPARATOR_WIDTH), FALSE, FALSE, 0);

    // Wrap clock in a button for clicking
    clock_button_ = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(clock_button_), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(clock_button_, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(clock_button_), "milopanel-button");

    clock_label_ = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(clock_label_), 0.5f);
    gtk_style_context_add_class(gtk_widget_get_style_context(clock_label_), "milopanel-clock");
    gtk_widget_set_size_request(clock_label_, CLOCK_WIDTH, PANEL_REALIZED_HEIGHT);
    gtk_container_add(GTK_CONTAINER(clock_button_), clock_label_);
    gtk_box_pack_start(GTK_BOX(bar_), clock_button_, FALSE, FALSE, 0);

    // Initialize Clock Popup Window
    clock_popup_ = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_transient_for(GTK_WINDOW(clock_popup_), GTK_WINDOW(window_));
    gtk_window_set_keep_above(GTK_WINDOW(clock_popup_), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(clock_popup_), "milopanel-popup");

    GtkWidget* clock_frame = gtk_frame_new(nullptr);
    gtk_frame_set_shadow_type(GTK_FRAME(clock_frame), GTK_SHADOW_OUT);
    gtk_container_add(GTK_CONTAINER(clock_popup_), clock_frame);

    GtkWidget* calendar = gtk_calendar_new();
    gtk_container_add(GTK_CONTAINER(clock_frame), calendar);
    gtk_widget_show_all(clock_frame);

    g_signal_connect(clock_popup_, "map", G_CALLBACK(on_popup_map), this);
    g_signal_connect(clock_popup_, "unmap", G_CALLBACK(on_popup_unmap), this);
    g_signal_connect(clock_popup_, "button-press-event", G_CALLBACK(on_popup_button_press), this);

    gtk_box_pack_start(GTK_BOX(bar_), fixed_spacer(STATUS_SEPARATOR_WIDTH), FALSE, FALSE, 0);

    notification_button_ = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(notification_button_), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(notification_button_, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(notification_button_), "milopanel-button");
    GtkWidget* notification_image = gtk_image_new_from_icon_name("preferences-system-notifications-symbolic", GTK_ICON_SIZE_MENU);
    gtk_image_set_pixel_size(GTK_IMAGE(notification_image), settings_.icon_size);
    gtk_widget_set_size_request(notification_button_, STATUS_BUTTON_WIDTH, PANEL_REALIZED_HEIGHT);
    gtk_container_add(GTK_CONTAINER(notification_button_), notification_image);
    gtk_widget_set_tooltip_text(notification_button_, i18n::tr("notifications").c_str());
    gtk_box_pack_start(GTK_BOX(bar_), notification_button_, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(bar_), fixed_spacer(STATUS_SEPARATOR_WIDTH), FALSE, FALSE, 0);

    g_signal_connect(window_, "realize", G_CALLBACK(on_realize), this);
    g_signal_connect(window_, "size-allocate", G_CALLBACK(on_size_allocate), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(on_destroy), this);
    g_signal_connect(menu_button_, "button-press-event", G_CALLBACK(on_menu_button_press), this);
    g_signal_connect(volume_button_, "button-press-event", G_CALLBACK(on_volume_button_press), this);
    g_signal_connect(volume_button_, "scroll-event", G_CALLBACK(on_volume_scroll), this);
    g_signal_connect(vol_popup_scale_, "value-changed", G_CALLBACK(on_popover_volume_changed), this);
    g_signal_connect(vol_popup_mute_btn_, "clicked", G_CALLBACK(on_popover_mute_clicked), this);
    g_signal_connect(input_popup_scale_, "value-changed", G_CALLBACK(on_popover_input_volume_changed), this);
    g_signal_connect(input_popup_mute_btn_, "clicked", G_CALLBACK(on_popover_input_mute_clicked), this);
    g_signal_connect(vol_pavu_btn, "clicked", G_CALLBACK(on_popover_pavucontrol_clicked), this);
    g_signal_connect(clock_button_, "button-press-event", G_CALLBACK(on_clock_button_press), this);
    g_signal_connect(notification_button_, "button-press-event", G_CALLBACK(on_notification_button_press), this);
    startup_trace("build_ui done");
}

void PanelWindow::build_menu_button() {
    menu_button_ = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(menu_button_), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(menu_button_, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(menu_button_), "milopanel-button");
    gtk_widget_set_size_request(menu_button_, MENU_BUTTON_WIDTH, PANEL_REALIZED_HEIGHT);

    menu_image_ = gtk_image_new();
    update_menu_logo();
    gtk_container_add(GTK_CONTAINER(menu_button_), menu_image_);
    gtk_widget_set_tooltip_text(menu_button_, "miloOS");
    gtk_box_pack_start(GTK_BOX(bar_), menu_button_, FALSE, FALSE, 0);

    menu_entries_ = load_menu_entries(settings_.menu_file);
}

void PanelWindow::update_menu_logo() {
    if (!menu_image_) {
        return;
    }

    const std::string logo = settings_.is_dark() ? settings_.logo_dark : settings_.logo_light;
    GError* error = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(
        logo.c_str(),
        settings_.icon_size,
        settings_.icon_size,
        TRUE,
        &error);
    if (pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(menu_image_), pixbuf);
        g_object_unref(pixbuf);
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(menu_image_), "start-here-symbolic", GTK_ICON_SIZE_MENU);
        gtk_image_set_pixel_size(GTK_IMAGE(menu_image_), settings_.icon_size);
        if (error) {
            g_error_free(error);
        }
    }
    gtk_widget_set_size_request(menu_image_, settings_.icon_size, settings_.icon_size);
}

void PanelWindow::load_css() {
    if (!css_provider_) {
        css_provider_ = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(
            gtk_widget_get_screen(window_),
            GTK_STYLE_PROVIDER(css_provider_),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    std::string css = css_for_settings(settings_);
    gtk_css_provider_load_from_data(css_provider_, css.c_str(), -1, nullptr);
}

void PanelWindow::setup_active_window_tracking() {
    GdkDisplay* gdk_display = gdk_display_get_default();
    if (gdk_display && GDK_IS_X11_DISPLAY(gdk_display)) {
        xdisplay_ = GDK_DISPLAY_XDISPLAY(gdk_display);
        root_window_ = DefaultRootWindow(xdisplay_);
        active_window_atom_ = XInternAtom(xdisplay_, "_NET_ACTIVE_WINDOW", False);
        net_wm_state_atom_ = XInternAtom(xdisplay_, "_NET_WM_STATE", False);
        fullscreen_atom_ = XInternAtom(xdisplay_, "_NET_WM_STATE_FULLSCREEN", False);
        root_filter_window_ = gdk_get_default_root_window();
        if (root_filter_window_) {
            g_object_ref(root_filter_window_);
            XSelectInput(xdisplay_, root_window_, PropertyChangeMask);
            gdk_window_add_filter(root_filter_window_, on_root_event, this);
            root_filter_installed_ = true;
        }
    }

    active_source_id_ = g_timeout_add_seconds(ACTIVE_WINDOW_FALLBACK_POLL_SECONDS, on_active_timeout, this);
}

void PanelWindow::setup_registrar_signals() {
    if (!ensure_session_bus()) {
        return;
    }

    registrar_window_registered_signal_id_ = g_dbus_connection_signal_subscribe(
        session_bus_,
        "com.canonical.AppMenu.Registrar",
        "com.canonical.AppMenu.Registrar",
        "WindowRegistered",
        "/com/canonical/AppMenu/Registrar",
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_registrar_dbus_signal,
        this,
        nullptr);

    registrar_window_unregistered_signal_id_ = g_dbus_connection_signal_subscribe(
        session_bus_,
        "com.canonical.AppMenu.Registrar",
        "com.canonical.AppMenu.Registrar",
        "WindowUnregistered",
        "/com/canonical/AppMenu/Registrar",
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_registrar_dbus_signal,
        this,
        nullptr);
}

void PanelWindow::schedule_active_window_refresh() {
    if (!active_refresh_source_id_) {
        active_refresh_source_id_ = g_idle_add_full(
            G_PRIORITY_HIGH_IDLE,
            on_active_refresh_timeout,
            this,
            nullptr);
    }
}

void PanelWindow::schedule_tray_background_refresh() {
    if (!tray_background_refresh_source_id_) {
        tray_background_refresh_source_id_ = g_idle_add(on_tray_background_refresh_timeout, this);
    }
}

void PanelWindow::show() {
    reposition();
    gtk_widget_show_all(window_);
    reposition();
}

void PanelWindow::reload_settings() {
    settings_ = PanelSettings::load();
    load_css();
    tray_host_.refresh_background();
    schedule_tray_background_refresh();
    gtk_widget_set_size_request(bar_, -1, settings_.height);
    update_menu_logo();
    update_clock();
    update_battery();
    update_volume();
    reposition();
    reserve_screen_space();
}

void PanelWindow::reposition() {
    GdkRectangle geometry = primary_monitor_geometry();
    if (geometry.width <= 0) {
        return;
    }
    const int width = std::max(1, geometry.width);
    const int height = std::max(1, settings_.height);
    GdkWindow* gdk_window = gtk_widget_get_window(window_);

    if (gdk_window && geometry_applied_ &&
        last_x_ == geometry.x &&
        last_y_ == geometry.y &&
        last_width_ == width &&
        last_height_ == height) {
        return;
    }

    gtk_widget_set_size_request(window_, width, height);
    gtk_widget_set_size_request(bar_, width, height);
    gtk_window_set_default_size(GTK_WINDOW(window_), width, height);
    gtk_window_resize(GTK_WINDOW(window_), width, height);
    gtk_window_move(GTK_WINDOW(window_), geometry.x, geometry.y);

    if (gdk_window) {
        gdk_window_move_resize(gdk_window, geometry.x, geometry.y, width, height);
        geometry_applied_ = true;
        last_x_ = geometry.x;
        last_y_ = geometry.y;
        last_width_ = width;
        last_height_ = height;
    }
}

void PanelWindow::reserve_screen_space() {
    if (!settings_.reserve_space || !reserve_space_override_) {
        return;
    }

    GdkDisplay* gdk_display = gdk_display_get_default();
    if (!gdk_display || !GDK_IS_X11_DISPLAY(gdk_display) || !gtk_widget_get_window(window_)) {
        return;
    }

    GdkRectangle geometry = primary_monitor_geometry();
    if (struts_applied_ &&
        last_strut_x_ == geometry.x &&
        last_strut_y_ == geometry.y &&
        last_strut_width_ == geometry.width &&
        last_strut_height_ == settings_.height) {
        return;
    }

    Display* display = GDK_DISPLAY_XDISPLAY(gdk_display);
    Window xid = GDK_WINDOW_XID(gtk_widget_get_window(window_));
    Atom strut = XInternAtom(display, "_NET_WM_STRUT", False);
    Atom strut_partial = XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False);
    long values[12] = {0};
    values[2] = settings_.height;
    values[8] = geometry.x;
    values[9] = geometry.x + geometry.width - 1;
    XChangeProperty(display, xid, strut_partial, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(values), 12);
    XChangeProperty(display, xid, strut, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(values), 4);
    XFlush(display);

    struts_applied_ = true;
    last_strut_x_ = geometry.x;
    last_strut_y_ = geometry.y;
    last_strut_width_ = geometry.width;
    last_strut_height_ = settings_.height;
}

void PanelWindow::update_clock() {
    std::time_t now = std::time(nullptr);
    std::tm local_time{};
    localtime_r(&now, &local_time);

    std::array<char, 128> text{};
    std::array<char, 192> tooltip{};
    std::strftime(text.data(), text.size(), settings_.clock_format.c_str(), &local_time);
    std::strftime(tooltip.data(), tooltip.size(), settings_.clock_tooltip_format.c_str(), &local_time);
    if (last_clock_text_ != text.data()) {
        gtk_label_set_text(GTK_LABEL(clock_label_), text.data());
        last_clock_text_ = text.data();
    }
    if (last_clock_tooltip_ != tooltip.data()) {
        gtk_widget_set_tooltip_text(clock_label_, tooltip.data());
        last_clock_tooltip_ = tooltip.data();
    }
}

void PanelWindow::update_battery() {
    if (!battery_button_ || !battery_image_) {
        return;
    }

    const BatteryInfo info = current_battery_info();
    if (!info.present) {
        if (last_battery_visible_) {
            gtk_widget_hide(battery_button_);
            last_battery_visible_ = false;
        }
        last_battery_label_.clear();
        last_battery_icon_.clear();
        return;
    }

    const std::string icon = battery_icon_name(info.capacity, info.status);
    const std::string label =
        i18n::tr("battery") + ": " +
        std::to_string(info.capacity) + "% (" +
        localized_battery_status(info.status) + ")";

    if (last_battery_icon_ != icon) {
        gtk_image_set_from_icon_name(GTK_IMAGE(battery_image_), icon.c_str(), GTK_ICON_SIZE_MENU);
        gtk_image_set_pixel_size(GTK_IMAGE(battery_image_), settings_.icon_size);
        last_battery_icon_ = icon;
    }
    if (last_battery_label_ != label) {
        gtk_widget_set_tooltip_text(battery_button_, label.c_str());
        last_battery_label_ = label;
    }
    if (!last_battery_visible_) {
        gtk_widget_show(battery_image_);
        gtk_widget_show(battery_button_);
        last_battery_visible_ = true;
    }
}

std::string PanelWindow::command_output(const std::string& command) {
    std::array<char, 256> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return output;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        output += buffer.data();
    }
    pclose(pipe);
    return output;
}

int PanelWindow::current_volume_percent() {
    std::string output = command_output("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null");
    size_t percent = output.find('%');
    if (percent == std::string::npos) {
        return -1;
    }
    size_t start = percent;
    while (start > 0 && std::isdigit(static_cast<unsigned char>(output[start - 1]))) {
        --start;
    }
    try {
        return std::stoi(output.substr(start, percent - start));
    } catch (...) {
        return -1;
    }
}

bool PanelWindow::current_volume_muted() {
    std::string output = command_output("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null");
    return output.find("yes") != std::string::npos || output.find("si") != std::string::npos;
}

int PanelWindow::current_input_volume_percent() {
    std::string output = command_output("pactl get-source-volume @DEFAULT_SOURCE@ 2>/dev/null");
    size_t percent = output.find('%');
    if (percent == std::string::npos) {
        return -1;
    }
    size_t start = percent;
    while (start > 0 && std::isdigit(static_cast<unsigned char>(output[start - 1]))) {
        --start;
    }
    try {
        return std::stoi(output.substr(start, percent - start));
    } catch (...) {
        return -1;
    }
}

bool PanelWindow::current_input_muted() {
    std::string output = command_output("pactl get-source-mute @DEFAULT_SOURCE@ 2>/dev/null");
    return output.find("yes") != std::string::npos || output.find("si") != std::string::npos;
}

void PanelWindow::update_volume() {
    int volume = current_volume_percent();
    bool muted = current_volume_muted();
    int input_volume = current_input_volume_percent();
    bool input_muted = current_input_muted();

    if (volume < 0) {
        if (volume_label_) {
            gtk_label_set_text(GTK_LABEL(volume_label_), "--");
        }
        if (last_volume_icon_ != "audio-card-symbolic") {
            gtk_image_set_from_icon_name(GTK_IMAGE(volume_image_), "audio-card-symbolic", GTK_ICON_SIZE_MENU);
            last_volume_icon_ = "audio-card-symbolic";
        }
        return;
    }

    std::string label = std::to_string(volume) + "%";
    if (volume_label_) {
        gtk_label_set_text(GTK_LABEL(volume_label_), label.c_str());
    }
    const char* icon = "audio-volume-high-symbolic";
    if (muted || volume == 0) {
        icon = "audio-volume-muted-symbolic";
    } else if (volume < 34) {
        icon = "audio-volume-low-symbolic";
    } else if (volume < 67) {
        icon = "audio-volume-medium-symbolic";
    }
    if (last_volume_icon_ != icon) {
        gtk_image_set_from_icon_name(GTK_IMAGE(volume_image_), icon, GTK_ICON_SIZE_MENU);
        gtk_image_set_pixel_size(GTK_IMAGE(volume_image_), settings_.icon_size);
        last_volume_icon_ = icon;
    }
    if (last_volume_label_ != label) {
        gtk_widget_set_tooltip_text(volume_button_, ("PulseAudio: " + label).c_str());
        last_volume_label_ = label;
    }

    if (volume_popup_) {
        updating_vol_popup_ = true;
        gtk_range_set_value(GTK_RANGE(vol_popup_scale_), volume);
        gtk_label_set_text(GTK_LABEL(vol_popup_label_), label.c_str());
        GtkWidget* img = gtk_button_get_image(GTK_BUTTON(vol_popup_mute_btn_));
        if (img) {
            gtk_image_set_from_icon_name(GTK_IMAGE(img), icon, GTK_ICON_SIZE_MENU);
        }
        if (input_volume >= 0) {
            std::string input_label = std::to_string(input_volume) + "%";
            gtk_range_set_value(GTK_RANGE(input_popup_scale_), input_volume);
            gtk_label_set_text(GTK_LABEL(input_popup_label_), input_label.c_str());
            const char* input_icon = input_muted || input_volume == 0
                ? "microphone-sensitivity-muted-symbolic"
                : "microphone-sensitivity-high-symbolic";
            GtkWidget* input_img = gtk_button_get_image(GTK_BUTTON(input_popup_mute_btn_));
            if (input_img) {
                gtk_image_set_from_icon_name(GTK_IMAGE(input_img), input_icon, GTK_ICON_SIZE_MENU);
            }
        } else {
            gtk_label_set_text(GTK_LABEL(input_popup_label_), "--");
        }
        updating_vol_popup_ = false;
    }
}

void PanelWindow::launch_command(const std::string& command) {
    normalize_process_cwd();
    const char* current_modules = g_getenv("GTK_MODULES");
    const bool had_current_modules = current_modules != nullptr;
    const std::string current_modules_copy = current_modules ? current_modules : "";
    const char* child_modules = g_getenv("MILO_PANEL_CHILD_GTK_MODULES");
    if (child_modules) {
        g_setenv("GTK_MODULES", child_modules, TRUE);
    } else {
        g_unsetenv("GTK_MODULES");
    }

    GError* error = nullptr;
    if (!g_spawn_command_line_async(command.c_str(), &error) && error) {
        g_error_free(error);
    }

    if (had_current_modules) {
        g_setenv("GTK_MODULES", current_modules_copy.c_str(), TRUE);
    } else {
        g_unsetenv("GTK_MODULES");
    }
}

GtkWidget* PanelWindow::create_appmenu_widget() {
    // The external widget intentionally delays model replacement to avoid
    // flicker. Prefer miloPanel's direct importer so focus changes are visible
    // on the next GTK cycle, while retaining the widget as an opt-in fallback.
    if (!external_appmenu_enabled()) {
        return nullptr;
    }

    using AppmenuMenuWidgetNew = GtkWidget* (*)();
    const char* candidates[] = {
        "/usr/lib/x86_64-linux-gnu/vala-panel/applets/libappmenu.so",
        "/usr/lib/aarch64-linux-gnu/vala-panel/applets/libappmenu.so",
        "/usr/lib/arm-linux-gnueabihf/vala-panel/applets/libappmenu.so",
        "libappmenu.so",
        nullptr
    };

    if (!g_module_supported()) {
        g_printerr("miloPanel: GModule is not supported; AppMenu widget disabled\n");
        return nullptr;
    }

    for (const char** candidate = candidates; *candidate; ++candidate) {
        GModule* module = g_module_open(*candidate, G_MODULE_BIND_LAZY);
        if (!module) {
            continue;
        }

        gpointer symbol = nullptr;
        if (!g_module_symbol(module, "appmenu_menu_widget_new", &symbol) || !symbol) {
            g_module_close(module);
            continue;
        }

        auto create_widget = reinterpret_cast<AppmenuMenuWidgetNew>(symbol);
        GtkWidget* widget = create_widget();
        if (!widget || !GTK_IS_WIDGET(widget)) {
            g_module_close(module);
            continue;
        }

        g_object_set(
            G_OBJECT(widget),
            "compact-mode", FALSE,
            "bold-application-name", FALSE,
            nullptr);
        flatten_appmenu_widget(widget);
        prepare_appmenu_widget_tree(widget);
        appmenu_module_ = module;
        g_module_make_resident(appmenu_module_);
        return widget;
    }

    g_printerr("miloPanel: libappmenu.so not found; falling back to internal AppMenu importer\n");
    return nullptr;
}

void PanelWindow::adjust_volume(int delta_percent) {
    std::string sign = delta_percent > 0 ? "+" : "";
    launch_command("pactl set-sink-volume @DEFAULT_SINK@ " + sign + std::to_string(delta_percent) + "%");
    g_timeout_add(120, +[](gpointer data) -> gboolean {
        static_cast<PanelWindow*>(data)->update_volume();
        return FALSE;
    }, this);
}

std::string PanelWindow::active_window_title() {
    GdkDisplay* gdk_display = gdk_display_get_default();
    if (!gdk_display || !GDK_IS_X11_DISPLAY(gdk_display)) {
        current_active_window_id_ = 0;
        active_window_is_desktop_ = true;
        return i18n::tr("desktop");
    }

    Display* display = GDK_DISPLAY_XDISPLAY(gdk_display);
    Window root = DefaultRootWindow(display);
    Atom active_atom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    Atom name_atom = XInternAtom(display, "_NET_WM_NAME", False);
    Atom utf8_atom = XInternAtom(display, "UTF8_STRING", False);

    gdk_x11_display_error_trap_push(gdk_display);
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    Window active = 0;
    if (XGetWindowProperty(display, root, active_atom, 0, 1, False, XA_WINDOW, &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success && data && nitems > 0) {
        active = *reinterpret_cast<Window*>(data);
    }
    if (data) {
        XFree(data);
        data = nullptr;
    }
    current_active_window_id_ = active;
    gdk_x11_display_error_trap_pop_ignored(gdk_display);

    active_window_is_desktop_ = active == 0 || active == root;
    if (active_window_is_desktop_) {
        return i18n::tr("desktop");
    }

    std::string res_name;
    std::string res_class;
    gdk_x11_display_error_trap_push(gdk_display);
    active_window_is_desktop_ = window_has_type(display, active, "_NET_WM_WINDOW_TYPE_DESKTOP");
    XClassHint class_hint;
    class_hint.res_name = nullptr;
    class_hint.res_class = nullptr;
    if (XGetClassHint(display, active, &class_hint)) {
        if (class_hint.res_name) {
            res_name = class_hint.res_name;
        }
        if (class_hint.res_class) {
            res_class = class_hint.res_class;
        }
    }
    if (class_hint.res_class) {
        XFree(class_hint.res_class);
    }
    if (class_hint.res_name) {
        XFree(class_hint.res_name);
    }
    gdk_x11_display_error_trap_pop_ignored(gdk_display);

    if (string_identity_is_desktop(res_name) || string_identity_is_desktop(res_class)) {
        active_window_is_desktop_ = true;
    }
    if (active_window_is_desktop_) {
        return i18n::tr("desktop");
    }

    std::string desktop_file =
        x11_window_string_property(active, "_NET_WM_DESKTOP_FILE");
    if (desktop_file.empty()) {
        const long active_pid =
            x11_window_cardinal_property(display, active, "_NET_WM_PID");
        desktop_file = process_environment_value(
            active_pid, "GIO_LAUNCHED_DESKTOP_FILE");
    }
    const std::string gtk_app_id = x11_window_string_property(active, "_GTK_APPLICATION_ID");
    if (string_identity_is_desktop(desktop_file) || string_identity_is_desktop(gtk_app_id)) {
        active_window_is_desktop_ = true;
        return i18n::tr("desktop");
    }

    std::string title = resolve_app_display_name(desktop_file, gtk_app_id, res_class, res_name);
    if (!title.empty()) {
        return title;
    }

    if (!res_class.empty()) {
        return pretty_app_name(res_class);
    }
    if (!res_name.empty()) {
        return pretty_app_name(res_name);
    }

    title = x11_window_string_property(active, "_NET_WM_NAME");
    if (title.empty()) {
        gdk_x11_display_error_trap_push(gdk_display);
        if (XGetWindowProperty(display, active, name_atom, 0, 512, False, utf8_atom, &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success && data && actual_format == 8) {
            title.assign(reinterpret_cast<char*>(data), nitems);
        }
        if (data) {
            XFree(data);
            data = nullptr;
        }
        gdk_x11_display_error_trap_pop_ignored(gdk_display);
    }
    return title.empty() ? "miloOS" : title;
}

void PanelWindow::update_active_window() {
    startup_trace("update_active_window begin");
    std::string title = active_window_title();
    startup_trace("active title resolved");
    if (active_label_ && last_active_text_ != title) {
        gtk_label_set_text(GTK_LABEL(active_label_), title.c_str());
        last_active_text_ = title;
    }

    update_fullscreen_state();

    if (appmenu_module_) {
        if (active_window_is_desktop_) {
            if (native_appmenu_) {
                gtk_widget_hide(native_appmenu_);
            }
            if (active_label_) {
                gtk_widget_show(active_label_);
            }
            if (current_menu_kind_ != GlobalMenuKind::DesktopMenu) {
                clear_menu_bar();
                current_menu_kind_ = GlobalMenuKind::DesktopMenu;
                current_menu_service_.clear();
                current_menu_path_.clear();
                current_gmenu_bus_.clear();
                current_gmenu_path_.clear();
                current_gmenu_app_path_.clear();
                current_gmenu_window_path_.clear();
                update_menu_bar();
            } else if (menu_bar_) {
                gtk_widget_show_all(menu_bar_);
            }
        } else {
            if (active_label_) {
                gtk_widget_hide(active_label_);
            }
            if (menu_bar_) {
                clear_menu_bar();
                gtk_widget_hide(menu_bar_);
            }
            if (native_appmenu_) {
                gtk_widget_show_all(native_appmenu_);
            }
            current_menu_kind_ = GlobalMenuKind::NoMenu;
            current_menu_service_.clear();
            current_menu_path_.clear();
            current_gmenu_bus_.clear();
            current_gmenu_path_.clear();
            current_gmenu_app_path_.clear();
            current_gmenu_window_path_.clear();
        }
        last_active_window_id_ = current_active_window_id_;
        return;
    }

    bool window_changed = (current_active_window_id_ != last_active_window_id_);
    bool should_query = window_changed;
    if (window_changed) {
        menu_retry_window_id_ = current_active_window_id_;
        menu_retry_count_ = 0;
    }

    if (current_active_window_id_ != 0 &&
        !active_window_is_desktop_ &&
        current_menu_service_.empty() &&
        current_gmenu_bus_.empty() &&
        menu_retry_window_id_ == current_active_window_id_ &&
        menu_retry_count_ < MENU_MAX_RETRIES_PER_WINDOW) {
        should_query = true;
        ++menu_retry_count_;
    }

    if (!should_query) {
        return;
    }

    last_active_window_id_ = current_active_window_id_;
    std::string service;
    std::string path;
    std::string gmenu_bus;
    std::string gmenu_path;
    std::string gmenu_app_path;
    std::string gmenu_window_path;
    GlobalMenuKind next_kind = GlobalMenuKind::NoMenu;
    if (active_window_is_desktop_ || current_active_window_id_ == 0) {
        next_kind = GlobalMenuKind::DesktopMenu;
    } else if (query_gmenu_for_window(current_active_window_id_, &gmenu_bus, &gmenu_path, &gmenu_app_path, &gmenu_window_path)) {
        next_kind = GlobalMenuKind::GMenu;
    } else if (query_menu_for_window(current_active_window_id_, &service, &path)) {
        next_kind = GlobalMenuKind::DBusMenu;
    }

    if (next_kind != current_menu_kind_ ||
        service != current_menu_service_ ||
        path != current_menu_path_ ||
        gmenu_bus != current_gmenu_bus_ ||
        gmenu_path != current_gmenu_path_ ||
        gmenu_app_path != current_gmenu_app_path_ ||
        gmenu_window_path != current_gmenu_window_path_) {
        cleanup_menu_client();
        current_menu_kind_ = next_kind;
        current_menu_service_ = service;
        current_menu_path_ = path;
        current_gmenu_bus_ = gmenu_bus;
        current_gmenu_path_ = gmenu_path;
        current_gmenu_app_path_ = gmenu_app_path;
        current_gmenu_window_path_ = gmenu_window_path;
        if (current_menu_kind_ == GlobalMenuKind::DBusMenu) {
            subscribe_menu_signals();
        }
        startup_trace("menu state changed");
        update_menu_bar();
        startup_trace("menu bar updated");
    }

    if (xdisplay_ && current_active_window_id_ != 0 && !active_window_is_desktop_) {
        GdkDisplay* gdk_display = gdk_display_get_default();
        gdk_x11_display_error_trap_push(gdk_display);
        XWindowAttributes wattr;
        if (XGetWindowAttributes(xdisplay_, current_active_window_id_, &wattr)) {
            XSelectInput(xdisplay_, current_active_window_id_, wattr.your_event_mask | PropertyChangeMask);
        }
        gdk_x11_display_error_trap_pop_ignored(gdk_display);
    }
}

void PanelWindow::update_fullscreen_state() {
    if (!xdisplay_ || current_active_window_id_ == 0) {
        if (panel_hidden_for_fullscreen_) {
            panel_hidden_for_fullscreen_ = false;
            gtk_widget_show(window_);
            reserve_screen_space();
        }
        return;
    }

    gdk_x11_display_error_trap_push(gdk_display_get_default());
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    bool is_fullscreen = false;

    if (XGetWindowProperty(xdisplay_, current_active_window_id_, net_wm_state_atom_,
                           0, 64, False, XA_ATOM, &actual_type, &actual_format,
                           &nitems, &bytes_after, &data) == Success && data) {
        auto* atoms = reinterpret_cast<Atom*>(data);
        for (unsigned long i = 0; i < nitems; ++i) {
            if (atoms[i] == fullscreen_atom_) {
                is_fullscreen = true;
                break;
            }
        }
        XFree(data);
    }
    gdk_x11_display_error_trap_pop_ignored(gdk_display_get_default());

    if (is_fullscreen && !panel_hidden_for_fullscreen_) {
        panel_hidden_for_fullscreen_ = true;
        gtk_widget_hide(window_);
    } else if (!is_fullscreen && panel_hidden_for_fullscreen_) {
        panel_hidden_for_fullscreen_ = false;
        gtk_widget_show(window_);
        reserve_screen_space();
    }
}

bool PanelWindow::ensure_session_bus() {
    if (session_bus_) {
        return true;
    }
    GError* error = nullptr;
    session_bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (error) {
        g_error_free(error);
    }
    return session_bus_ != nullptr;
}

std::vector<Window> PanelWindow::menu_candidate_windows(Window window) {
    std::vector<Window> candidates;
    auto add_candidate = [&candidates](Window value) {
        if (value != 0 && std::find(candidates.begin(), candidates.end(), value) == candidates.end()) {
            candidates.push_back(value);
        }
    };
    add_candidate(window);

    GdkDisplay* gdk_display = gdk_display_get_default();
    if (!gdk_display || !GDK_IS_X11_DISPLAY(gdk_display) || window == 0) {
        return candidates;
    }

    Display* display = GDK_DISPLAY_XDISPLAY(gdk_display);
    gdk_x11_display_error_trap_push(gdk_display);

    Atom client_leader_atom = XInternAtom(display, "WM_CLIENT_LEADER", False);
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, window, client_leader_atom, 0, 1, False, XA_WINDOW,
                           &actual_type, &actual_format, &nitems, &bytes_after, &data) == Success &&
        data && nitems > 0) {
        add_candidate(*reinterpret_cast<Window*>(data));
    }
    if (data) {
        XFree(data);
        data = nullptr;
    }

    Window transient_for = 0;
    if (XGetTransientForHint(display, window, &transient_for)) {
        add_candidate(transient_for);
    }

    XWMHints* hints = XGetWMHints(display, window);
    if (hints) {
        if (hints->flags & WindowGroupHint) {
            add_candidate(hints->window_group);
        }
        XFree(hints);
    }

    gdk_x11_display_error_trap_pop_ignored(gdk_display);
    return candidates;
}

std::string PanelWindow::x11_window_string_property(Window window, const char* property_name) {
    GdkDisplay* gdk_display = gdk_display_get_default();
    if (!gdk_display || !GDK_IS_X11_DISPLAY(gdk_display) || window == 0) {
        return {};
    }

    Display* display = GDK_DISPLAY_XDISPLAY(gdk_display);
    Atom property_atom = XInternAtom(display, property_name, False);
    Atom utf8_atom = XInternAtom(display, "UTF8_STRING", False);
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;

    gdk_x11_display_error_trap_push(gdk_display);
    int status = XGetWindowProperty(
        display,
        window,
        property_atom,
        0,
        4096,
        False,
        utf8_atom,
        &actual_type,
        &actual_format,
        &nitems,
        &bytes_after,
        &data);
    if (status != Success || !data || actual_format != 8) {
        if (data) {
            XFree(data);
            data = nullptr;
        }
        status = XGetWindowProperty(
            display,
            window,
            property_atom,
            0,
            4096,
            False,
            XA_STRING,
            &actual_type,
            &actual_format,
            &nitems,
            &bytes_after,
            &data);
    }

    std::string value;
    if (status == Success && data && actual_format == 8 && nitems > 0) {
        value.assign(reinterpret_cast<const char*>(data), nitems);
    }
    if (data) {
        XFree(data);
    }
    gdk_x11_display_error_trap_pop_ignored(gdk_display);
    return value;
}

bool PanelWindow::query_menu_for_window(Window window, std::string* service, std::string* path) {
    if (!ensure_session_bus() || window == 0) {
        return false;
    }

    for (Window candidate : menu_candidate_windows(window)) {
        if (appmenu_registrar_.menu_for_window(
                static_cast<guint32>(candidate),
                service,
                path)) {
            return true;
        }
        if (appmenu_registrar_.owns_name()) {
            continue;
        }

        GError* error = nullptr;
        GVariant* result = g_dbus_connection_call_sync(
            session_bus_,
            "com.canonical.AppMenu.Registrar",
            "/com/canonical/AppMenu/Registrar",
            "com.canonical.AppMenu.Registrar",
            "GetMenuForWindow",
            g_variant_new("(u)", static_cast<guint32>(candidate)),
            G_VARIANT_TYPE("(so)"),
            G_DBUS_CALL_FLAGS_NO_AUTO_START,
            250,
            nullptr,
            &error);

        if (!result) {
            if (error) {
                g_error_free(error);
            }
            continue;
        }

        const gchar* service_value = nullptr;
        const gchar* path_value = nullptr;
        g_variant_get(result, "(&s&o)", &service_value, &path_value);
        service->clear();
        path->clear();
        if (service_value) {
            *service = service_value;
        }
        if (path_value) {
            *path = path_value;
        }
        g_variant_unref(result);
        if (!service->empty() && !path->empty()) {
            return true;
        }
    }
    service->clear();
    path->clear();
    return false;
}

bool PanelWindow::query_gmenu_for_window(
    Window window,
    std::string* bus,
    std::string* menu_path,
    std::string* app_path,
    std::string* window_path) {
    if (!ensure_session_bus() || window == 0) {
        return false;
    }

    for (Window candidate : menu_candidate_windows(window)) {
        std::string candidate_bus = x11_window_string_property(candidate, "_GTK_UNIQUE_BUS_NAME");
        std::string candidate_menu = x11_window_string_property(candidate, "_GTK_MENUBAR_OBJECT_PATH");
        if (candidate_menu.empty()) {
            candidate_menu = x11_window_string_property(candidate, "_GTK_APP_MENU_OBJECT_PATH");
        }
        if (candidate_bus.empty() || candidate_menu.empty()) {
            continue;
        }

        *bus = candidate_bus;
        *menu_path = candidate_menu;
        *app_path = x11_window_string_property(candidate, "_GTK_APPLICATION_OBJECT_PATH");
        *window_path = x11_window_string_property(candidate, "_GTK_WINDOW_OBJECT_PATH");
        return true;
    }
    return false;
}

void PanelWindow::cleanup_menu_client() {
    if (appmenu_module_) {
        return;
    }
    if (session_bus_ && menu_layout_signal_id_) {
        g_dbus_connection_signal_unsubscribe(session_bus_, menu_layout_signal_id_);
        menu_layout_signal_id_ = 0;
    }
    if (session_bus_ && menu_properties_signal_id_) {
        g_dbus_connection_signal_unsubscribe(session_bus_, menu_properties_signal_id_);
        menu_properties_signal_id_ = 0;
    }
    if (menu_refresh_source_id_) {
        g_source_remove(menu_refresh_source_id_);
        menu_refresh_source_id_ = 0;
    }
    if (session_bus_ && !current_gmenu_bus_.empty() && !current_gmenu_path_.empty()) {
        GVariantBuilder groups;
        g_variant_builder_init(&groups, G_VARIANT_TYPE("au"));
        for (guint group_id = 0; group_id <= 64; ++group_id) {
            g_variant_builder_add(&groups, "u", group_id);
        }
        g_dbus_connection_call(
            session_bus_,
            current_gmenu_bus_.c_str(),
            current_gmenu_path_.c_str(),
            "org.gtk.Menus",
            "End",
            g_variant_new("(@au)", g_variant_builder_end(&groups)),
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            300,
            nullptr,
            nullptr,
            nullptr);
    }
    if (session_bus_) {
        for (guint signal_id : gmenu_signal_ids_) {
            g_dbus_connection_signal_unsubscribe(session_bus_, signal_id);
        }
    }
    gmenu_signal_ids_.clear();
    detach_gmenu_model();
    clear_menu_bar();
    current_menu_widgets_.clear();
    if (menu_bar_) {
        gtk_widget_hide(menu_bar_);
    }
}

void PanelWindow::detach_gmenu_model() {
    if (menu_bar_) {
        gtk_widget_insert_action_group(menu_bar_, "unity", nullptr);
        gtk_widget_insert_action_group(menu_bar_, "app", nullptr);
        gtk_widget_insert_action_group(menu_bar_, "win", nullptr);
    }
    if (current_gmenu_model_) {
        g_object_unref(current_gmenu_model_);
        current_gmenu_model_ = nullptr;
    }
    if (unity_action_group_) {
        g_object_unref(unity_action_group_);
        unity_action_group_ = nullptr;
    }
    if (app_action_group_) {
        g_object_unref(app_action_group_);
        app_action_group_ = nullptr;
    }
    if (win_action_group_) {
        g_object_unref(win_action_group_);
        win_action_group_ = nullptr;
    }
}

void PanelWindow::subscribe_menu_signals() {
    if (!ensure_session_bus() || current_menu_service_.empty() || current_menu_path_.empty()) {
        return;
    }

    menu_layout_signal_id_ = g_dbus_connection_signal_subscribe(
        session_bus_,
        current_menu_service_.c_str(),
        "com.canonical.dbusmenu",
        "LayoutUpdated",
        current_menu_path_.c_str(),
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_menu_dbus_signal,
        this,
        nullptr);

    menu_properties_signal_id_ = g_dbus_connection_signal_subscribe(
        session_bus_,
        current_menu_service_.c_str(),
        "com.canonical.dbusmenu",
        "ItemsPropertiesUpdated",
        current_menu_path_.c_str(),
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        on_menu_dbus_signal,
        this,
        nullptr);
}

void PanelWindow::clear_menu_bar() {
    if (!menu_bar_) {
        return;
    }
    GList* children = gtk_container_get_children(GTK_CONTAINER(menu_bar_));
    for (GList* item = children; item != nullptr; item = item->next) {
        gtk_container_remove(GTK_CONTAINER(menu_bar_), GTK_WIDGET(item->data));
    }
    g_list_free(children);
    current_menu_widgets_.clear();
}

GtkWidget* PanelWindow::build_menu_item_from_layout(GVariant* node, bool top_level) {
    if (!node) {
        return nullptr;
    }
    if (menu_debug_enabled()) {
        gchar* printed = g_variant_print(node, FALSE);
        g_printerr("miloPanel menu parse top=%d node=%s\n", top_level ? 1 : 0, printed ? printed : "(null)");
        g_free(printed);
    }

    gint id = 0;
    GVariant* properties = nullptr;
    GVariant* children = nullptr;
    g_variant_get(node, "(i@a{sv}@av)", &id, &properties, &children);

    gboolean visible = TRUE;
    gboolean enabled = TRUE;
    const gchar* label = nullptr;
    const gchar* item_type = nullptr;
    g_variant_lookup(properties, "visible", "b", &visible);
    g_variant_lookup(properties, "enabled", "b", &enabled);
    g_variant_lookup(properties, "label", "&s", &label);
    g_variant_lookup(properties, "type", "&s", &item_type);

    const std::string label_text = label ? label : "";
    const bool separator = g_strcmp0(item_type, "separator") == 0 || label_text.empty();
    if (!visible || (top_level && separator)) {
        g_variant_unref(properties);
        g_variant_unref(children);
        return nullptr;
    }

    GtkWidget* item = nullptr;
    if (separator) {
        item = gtk_separator_menu_item_new();
    } else {
        gint toggle_state = -1;
        if (g_variant_lookup(properties, "toggle-state", "i", &toggle_state)) {
            item = gtk_check_menu_item_new_with_mnemonic(label_text.c_str());
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), toggle_state > 0);
        } else {
            item = gtk_menu_item_new_with_mnemonic(label_text.c_str());
        }
        gtk_widget_set_sensitive(item, enabled);
        g_object_set_data(G_OBJECT(item), "dbusmenu-id", GINT_TO_POINTER(id));
    }

    bool has_submenu = false;
    if (!separator && g_variant_n_children(children) > 0) {
        GtkWidget* submenu = gtk_menu_new();
        bool has_visible_children = false;
        for (gsize i = 0; i < g_variant_n_children(children); ++i) {
            GVariant* child_wrapper = g_variant_get_child_value(children, i);
            GVariant* child_node = g_variant_get_variant(child_wrapper);
            GtkWidget* child_item = build_menu_item_from_layout(child_node, false);
            if (child_item) {
                gtk_menu_shell_append(GTK_MENU_SHELL(submenu), child_item);
                has_visible_children = true;
            }
            g_variant_unref(child_node);
            g_variant_unref(child_wrapper);
        }
        if (has_visible_children) {
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
            has_submenu = true;
            g_signal_connect(item, "select", G_CALLBACK(on_dbus_submenu_select), this);
        } else {
            gtk_widget_destroy(submenu);
        }
    }
    if (!separator && !has_submenu) {
        g_signal_connect(item, "activate", G_CALLBACK(on_dbus_menu_item_activate), this);
    }

    g_variant_unref(properties);
    g_variant_unref(children);
    return item;
}

GtkWidget* PanelWindow::build_global_menu_button(const std::string& label, GtkWidget* menu) {
    GtkWidget* button = gtk_menu_item_new_with_mnemonic(label.c_str());
    gtk_widget_set_can_focus(button, FALSE);
    gtk_widget_set_size_request(button, -1, PANEL_REALIZED_HEIGHT);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "milopanel-menu-button");
    if (menu) {
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(button), menu);
    }
    return button;
}

void PanelWindow::update_menu_bar() {
    if (!menu_bar_) {
        return;
    }
    if (menu_debug_enabled()) {
        g_printerr(
            "miloPanel menu update kind=%d service=%s path=%s gbus=%s gpath=%s\n",
            static_cast<int>(current_menu_kind_),
            current_menu_service_.c_str(),
            current_menu_path_.c_str(),
            current_gmenu_bus_.c_str(),
            current_gmenu_path_.c_str());
    }

    clear_menu_bar();

    if (current_menu_kind_ == GlobalMenuKind::DesktopMenu) {
        build_desktop_menu_bar();
        return;
    }

    if (current_menu_kind_ == GlobalMenuKind::GMenu) {
        install_gmenu_model();
        return;
    }

    if (current_menu_kind_ != GlobalMenuKind::DBusMenu ||
        !ensure_session_bus() ||
        current_menu_service_.empty() ||
        current_menu_path_.empty()) {
        gtk_widget_hide(menu_bar_);
        return;
    }

    GVariantBuilder property_names;
    g_variant_builder_init(&property_names, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&property_names, "s", "label");
    g_variant_builder_add(&property_names, "s", "visible");
    g_variant_builder_add(&property_names, "s", "enabled");
    g_variant_builder_add(&property_names, "s", "children-display");
    g_variant_builder_add(&property_names, "s", "type");
    g_variant_builder_add(&property_names, "s", "toggle-type");
    g_variant_builder_add(&property_names, "s", "toggle-state");

    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        session_bus_,
        current_menu_service_.c_str(),
        current_menu_path_.c_str(),
        "com.canonical.dbusmenu",
        "GetLayout",
        g_variant_new("(ii@as)", 0, DBUSMENU_LAYOUT_DEPTH, g_variant_builder_end(&property_names)),
        G_VARIANT_TYPE("(u(ia{sv}av))"),
        G_DBUS_CALL_FLAGS_NONE,
        700,
        nullptr,
        &error);

    if (!result) {
        if (menu_debug_enabled()) {
            g_printerr("miloPanel menu GetLayout failed: %s\n", error ? error->message : "unknown");
        }
        if (error) {
            g_error_free(error);
        }
        gtk_widget_hide(menu_bar_);
        return;
    }

    GVariant* root = g_variant_get_child_value(result, 1);
    GVariant* root_children = g_variant_get_child_value(root, 2);
    if (menu_debug_enabled()) {
        g_printerr("miloPanel menu root children=%lu\n", static_cast<unsigned long>(g_variant_n_children(root_children)));
    }
    for (gsize i = 0; i < g_variant_n_children(root_children); ++i) {
        GVariant* child_wrapper = g_variant_get_child_value(root_children, i);
        GVariant* child_node = g_variant_get_variant(child_wrapper);
        GtkWidget* item = build_menu_item_from_layout(child_node, true);
        if (item) {
            GtkWidget* submenu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(item));
            std::string label = menu_item_label(item);
            if (submenu && !label.empty()) {
                g_object_ref(submenu);
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), nullptr);
                GtkWidget* button = build_global_menu_button(label, submenu);
                g_object_unref(submenu);
                current_menu_widgets_.push_back(button);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar_), button);
            }
            gtk_widget_destroy(item);
        }
        g_variant_unref(child_node);
        g_variant_unref(child_wrapper);
    }

    g_variant_unref(root_children);
    g_variant_unref(root);
    g_variant_unref(result);

    if (current_menu_widgets_.empty()) {
        gtk_widget_hide(menu_bar_);
        return;
    }

    gtk_widget_show_all(menu_bar_);
    gtk_widget_queue_resize(menu_bar_);
    gtk_widget_queue_resize(bar_);
}

void PanelWindow::build_desktop_menu_bar() {
    if (!menu_bar_) {
        return;
    }

    auto append_command = [this](GtkWidget* menu, const std::string& label, const std::string& command) {
        GtkWidget* item = gtk_menu_item_new_with_label(label.c_str());
        struct CommandAction {
            PanelWindow* panel;
            std::string command;
        };
        g_signal_connect_data(item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer raw) {
            auto* action = static_cast<CommandAction*>(raw);
            action->panel->launch_command(action->command);
        }), new CommandAction{this, command}, [](gpointer raw, GClosure*) {
            delete static_cast<CommandAction*>(raw);
        }, G_CONNECT_DEFAULT);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    };

    const std::string home = g_get_home_dir() ? g_get_home_dir() : "";
    const std::string desktop = user_special_dir(G_USER_DIRECTORY_DESKTOP, home + "/Desktop");
    const std::string documents = user_special_dir(G_USER_DIRECTORY_DOCUMENTS, home + "/Documents");
    const std::string downloads = user_special_dir(G_USER_DIRECTORY_DOWNLOAD, home + "/Downloads");

    GtkWidget* file_menu = gtk_menu_new();
    append_command(file_menu, i18n::tr("new_window"), "milofiles");
    append_command(file_menu, i18n::tr("open_terminal"), "xfce4-terminal");

    GtkWidget* view_menu = gtk_menu_new();
    append_command(view_menu, i18n::tr("reload_desktop"), "xfdesktop --reload");

    GtkWidget* go_menu = gtk_menu_new();
    append_command(go_menu, i18n::tr("home_folder"), "milofiles " + shell_quote(home));
    append_command(go_menu, i18n::tr("desktop_folder"), "milofiles " + shell_quote(desktop));
    append_command(go_menu, i18n::tr("documents_folder"), "milofiles " + shell_quote(documents));
    append_command(go_menu, i18n::tr("downloads_folder"), "milofiles " + shell_quote(downloads));

    std::vector<std::pair<std::string, GtkWidget*>> desktop_menus = {
        {i18n::tr("file_menu"), file_menu},
        {i18n::tr("view_menu"), view_menu},
        {i18n::tr("go_menu"), go_menu},
    };
    for (const auto& item : desktop_menus) {
        GtkWidget* button = build_global_menu_button(item.first, item.second);
        current_menu_widgets_.push_back(button);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar_), button);
    }
    gtk_widget_show_all(menu_bar_);
    gtk_widget_queue_resize(menu_bar_);
    gtk_widget_queue_resize(bar_);
}

void PanelWindow::install_gmenu_model() {
    if (!ensure_session_bus() || current_gmenu_bus_.empty() || current_gmenu_path_.empty()) {
        gtk_widget_hide(menu_bar_);
        return;
    }

    detach_gmenu_model();
    unity_action_group_ = G_ACTION_GROUP(g_dbus_action_group_get(session_bus_, current_gmenu_bus_.c_str(), current_gmenu_path_.c_str()));
    if (!current_gmenu_app_path_.empty()) {
        app_action_group_ = G_ACTION_GROUP(g_dbus_action_group_get(session_bus_, current_gmenu_bus_.c_str(), current_gmenu_app_path_.c_str()));
    }
    if (!current_gmenu_window_path_.empty()) {
        win_action_group_ = G_ACTION_GROUP(g_dbus_action_group_get(session_bus_, current_gmenu_bus_.c_str(), current_gmenu_window_path_.c_str()));
    }

    GVariantBuilder groups;
    g_variant_builder_init(&groups, G_VARIANT_TYPE("au"));
    for (guint group_id = 0; group_id <= 64; ++group_id) {
        g_variant_builder_add(&groups, "u", group_id);
    }

    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        session_bus_,
        current_gmenu_bus_.c_str(),
        current_gmenu_path_.c_str(),
        "org.gtk.Menus",
        "Start",
        g_variant_new("(@au)", g_variant_builder_end(&groups)),
        G_VARIANT_TYPE("(a(uuaa{sv}))"),
        G_DBUS_CALL_FLAGS_NONE,
        700,
        nullptr,
        &error);

    if (!result) {
        if (menu_debug_enabled()) {
            g_printerr("miloPanel GMenu Start failed: %s\n", error ? error->message : "unknown");
        }
        if (error) {
            g_error_free(error);
        }
        gtk_widget_hide(menu_bar_);
        return;
    }

    struct RemoteMenuItem {
        std::string label;
        std::string action;
        GVariant* target = nullptr;
        bool has_section = false;
        guint section_group = 0;
        guint section_id = 0;
        bool has_submenu = false;
        guint submenu_group = 0;
        guint submenu_id = 0;
    };

    using NodeKey = std::pair<guint, guint>;
    std::map<NodeKey, std::vector<RemoteMenuItem>> nodes;
    GVariant* content = nullptr;
    g_variant_get(result, "(@a(uuaa{sv}))", &content);
    for (gsize node_index = 0; content && node_index < g_variant_n_children(content); ++node_index) {
        guint group_id = 0;
        guint menu_id = 0;
        GVariant* items = nullptr;
        GVariant* node = g_variant_get_child_value(content, node_index);
        g_variant_get(node, "(uu@aa{sv})", &group_id, &menu_id, &items);

        std::vector<RemoteMenuItem> parsed_items;
        for (gsize item_index = 0; items && item_index < g_variant_n_children(items); ++item_index) {
            GVariant* attrs = g_variant_get_child_value(items, item_index);
            RemoteMenuItem item;
            gchar* label = nullptr;
            gchar* action = nullptr;
            if (g_variant_lookup(attrs, "label", "s", &label) && label) {
                item.label = label;
                g_free(label);
            }
            if (g_variant_lookup(attrs, "action", "s", &action) && action) {
                item.action = action;
                g_free(action);
            }
            GVariant* section = g_variant_lookup_value(attrs, ":section", G_VARIANT_TYPE("(uu)"));
            if (section) {
                item.has_section = true;
                g_variant_get(section, "(uu)", &item.section_group, &item.section_id);
                g_variant_unref(section);
            }
            GVariant* submenu = g_variant_lookup_value(attrs, ":submenu", G_VARIANT_TYPE("(uu)"));
            if (submenu) {
                item.has_submenu = true;
                g_variant_get(submenu, "(uu)", &item.submenu_group, &item.submenu_id);
                g_variant_unref(submenu);
            }
            item.target = g_variant_lookup_value(attrs, "target", nullptr);
            parsed_items.push_back(item);
            g_variant_unref(attrs);
        }
        nodes[{group_id, menu_id}] = parsed_items;
        if (items) {
            g_variant_unref(items);
        }
        g_variant_unref(node);
    }
    if (content) {
        g_variant_unref(content);
    }
    g_variant_unref(result);

    subscribe_gmenu_signals();

    std::function<void(guint, guint, GtkWidget*, bool, bool*)> append_node;
    append_node = [&](guint group_id, guint menu_id, GtkWidget* shell, bool top_level, bool* appended_any) {
        auto found = nodes.find({group_id, menu_id});
        if (found == nodes.end()) {
            return;
        }

        for (const RemoteMenuItem& remote_item : found->second) {
            if (remote_item.has_section) {
                bool section_has_items = false;
                if (!top_level && *appended_any) {
                    gtk_menu_shell_append(GTK_MENU_SHELL(shell), gtk_separator_menu_item_new());
                }
                append_node(remote_item.section_group, remote_item.section_id, shell, top_level, &section_has_items);
                if (section_has_items) {
                    *appended_any = true;
                }
                continue;
            }
            if (remote_item.label.empty() && remote_item.action.empty() && !remote_item.has_submenu) {
                if (!top_level) {
                    gtk_menu_shell_append(GTK_MENU_SHELL(shell), gtk_separator_menu_item_new());
                    *appended_any = true;
                }
                continue;
            }

            if (top_level) {
                if (!remote_item.has_submenu || remote_item.label.empty()) {
                    continue;
                }
                GtkWidget* submenu = gtk_menu_new();
                bool submenu_has_items = false;
                append_node(remote_item.submenu_group, remote_item.submenu_id, submenu, false, &submenu_has_items);
                if (!submenu_has_items) {
                    gtk_widget_destroy(submenu);
                    continue;
                }
                GtkWidget* button = build_global_menu_button(remote_item.label, submenu);
                current_menu_widgets_.push_back(button);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar_), button);
                *appended_any = true;
                continue;
            }

            GVariant* action_state = nullptr;
            GActionGroup* action_group = nullptr;
            std::string action_name;
            bool action_enabled = true;
            if (!remote_item.action.empty()) {
                const std::size_t separator = remote_item.action.find('.');
                if (separator != std::string::npos && separator + 1 < remote_item.action.size()) {
                    action_group = gmenu_action_group_for_namespace(remote_item.action.substr(0, separator));
                    action_name = remote_item.action.substr(separator + 1);
                    if (action_group && g_action_group_has_action(action_group, action_name.c_str())) {
                        action_enabled = g_action_group_get_action_enabled(action_group, action_name.c_str());
                        action_state = g_action_group_get_action_state(action_group, action_name.c_str());
                    }
                }
            }

            bool checkable = false;
            bool checked = false;
            if (action_state) {
                if (g_variant_is_of_type(action_state, G_VARIANT_TYPE_BOOLEAN)) {
                    checkable = true;
                    checked = g_variant_get_boolean(action_state);
                } else if (remote_item.target && g_variant_equal(action_state, remote_item.target)) {
                    checkable = true;
                    checked = true;
                }
            }

            GtkWidget* item = checkable
                ? gtk_check_menu_item_new_with_mnemonic(remote_item.label.c_str())
                : gtk_menu_item_new_with_mnemonic(remote_item.label.c_str());
            if (checkable) {
                gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), checked);
            }
            gtk_widget_set_sensitive(item, action_enabled);
            if (remote_item.has_submenu) {
                GtkWidget* submenu = gtk_menu_new();
                bool submenu_has_items = false;
                append_node(remote_item.submenu_group, remote_item.submenu_id, submenu, false, &submenu_has_items);
                if (submenu_has_items) {
                    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
                } else {
                    gtk_widget_destroy(submenu);
                }
            } else if (!remote_item.action.empty()) {
                auto* binding = new GMenuActionBinding;
                binding->detailed_action = remote_item.action;
                binding->target = remote_item.target ? g_variant_ref(remote_item.target) : nullptr;
                g_object_set_data_full(G_OBJECT(item), "milopanel-gmenu-action", binding, free_gmenu_action_binding);
                g_signal_connect(item, "activate", G_CALLBACK(on_gmenu_item_activate), this);
            }
            if (action_state) {
                g_variant_unref(action_state);
            }
            gtk_menu_shell_append(GTK_MENU_SHELL(shell), item);
            *appended_any = true;
        }
    };

    bool appended_any = false;
    append_node(0, 0, menu_bar_, true, &appended_any);
    for (auto& [key, items] : nodes) {
        (void)key;
        for (RemoteMenuItem& item : items) {
            if (item.target) {
                g_variant_unref(item.target);
                item.target = nullptr;
            }
        }
    }
    if (!appended_any) {
        gtk_widget_hide(menu_bar_);
        return;
    }
    gtk_widget_show_all(menu_bar_);
    gtk_widget_queue_resize(menu_bar_);
    gtk_widget_queue_resize(bar_);
}

void PanelWindow::schedule_menu_refresh() {
    if (!menu_refresh_source_id_) {
        menu_refresh_source_id_ = g_idle_add_full(
            G_PRIORITY_HIGH_IDLE,
            on_menu_refresh_timeout,
            this,
            nullptr);
    }
}

void PanelWindow::send_menu_event(int id, const char* event_name) {
    if (!ensure_session_bus() || current_menu_service_.empty() || current_menu_path_.empty()) {
        return;
    }
    g_dbus_connection_call(
        session_bus_,
        current_menu_service_.c_str(),
        current_menu_path_.c_str(),
        "com.canonical.dbusmenu",
        "Event",
        g_variant_new("(isvu)", id, event_name, g_variant_new_string(""), gtk_get_current_event_time()),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        700,
        nullptr,
        nullptr,
        nullptr);
}

void PanelWindow::request_menu_about_to_show(int id) {
    if (!ensure_session_bus() || current_menu_service_.empty() || current_menu_path_.empty()) {
        return;
    }

    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        session_bus_,
        current_menu_service_.c_str(),
        current_menu_path_.c_str(),
        "com.canonical.dbusmenu",
        "AboutToShow",
        g_variant_new("(i)", id),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        300,
        nullptr,
        &error);
    if (!result) {
        if (error) {
            g_error_free(error);
        }
        return;
    }

    gboolean needs_update = FALSE;
    g_variant_get(result, "(b)", &needs_update);
    g_variant_unref(result);
    if (needs_update) {
        schedule_menu_refresh();
    }
}

GActionGroup* PanelWindow::gmenu_action_group_for_namespace(const std::string& namespace_name) {
    if (namespace_name == "unity") {
        return unity_action_group_;
    }
    if (namespace_name == "app") {
        return app_action_group_;
    }
    if (namespace_name == "win") {
        return win_action_group_;
    }
    return nullptr;
}

void PanelWindow::activate_gmenu_action(const std::string& detailed_action, GVariant* target) {
    if (!ensure_session_bus() || current_gmenu_bus_.empty()) {
        return;
    }

    const std::size_t separator = detailed_action.find('.');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= detailed_action.size()) {
        return;
    }
    const std::string namespace_name = detailed_action.substr(0, separator);
    const std::string action_name = detailed_action.substr(separator + 1);
    std::string action_path;
    if (namespace_name == "unity") {
        action_path = current_gmenu_path_;
    } else if (namespace_name == "app") {
        action_path = current_gmenu_app_path_.empty() ? current_gmenu_path_ : current_gmenu_app_path_;
    } else if (namespace_name == "win") {
        action_path = current_gmenu_window_path_;
    }
    if (action_path.empty()) {
        return;
    }

    GVariantBuilder parameters;
    g_variant_builder_init(&parameters, G_VARIANT_TYPE("av"));
    if (target) {
        g_variant_builder_add(&parameters, "v", target);
    }

    GVariantBuilder platform_data;
    g_variant_builder_init(&platform_data, G_VARIANT_TYPE("a{sv}"));
    g_dbus_connection_call(
        session_bus_,
        current_gmenu_bus_.c_str(),
        action_path.c_str(),
        "org.gtk.Actions",
        "Activate",
        g_variant_new("(s@av@a{sv})",
            action_name.c_str(),
            g_variant_builder_end(&parameters),
            g_variant_builder_end(&platform_data)),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        700,
        nullptr,
        nullptr,
        nullptr);
}

void PanelWindow::subscribe_gmenu_signals() {
    if (!ensure_session_bus() || current_gmenu_bus_.empty() || current_gmenu_path_.empty() || !gmenu_signal_ids_.empty()) {
        return;
    }

    auto subscribe = [this](const std::string& path, const char* interface_name, const char* signal_name) {
        if (path.empty()) {
            return;
        }
        guint signal_id = g_dbus_connection_signal_subscribe(
            session_bus_,
            current_gmenu_bus_.c_str(),
            interface_name,
            signal_name,
            path.c_str(),
            nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_menu_dbus_signal,
            this,
            nullptr);
        if (signal_id) {
            gmenu_signal_ids_.push_back(signal_id);
        }
    };

    subscribe(current_gmenu_path_, "org.gtk.Menus", "Changed");

    std::vector<std::string> action_paths;
    auto add_action_path = [&action_paths](const std::string& path) {
        if (!path.empty() && std::find(action_paths.begin(), action_paths.end(), path) == action_paths.end()) {
            action_paths.push_back(path);
        }
    };
    add_action_path(current_gmenu_path_);
    add_action_path(current_gmenu_app_path_);
    add_action_path(current_gmenu_window_path_);
    for (const std::string& path : action_paths) {
        subscribe(path, "org.gtk.Actions", "Changed");
    }
}

void PanelWindow::append_gmenu_model_items(GMenuModel* model, GtkWidget* shell, bool top_level, bool* appended_any) {
    if (!model || !shell || !appended_any) {
        return;
    }

    const gint count = g_menu_model_get_n_items(model);
    for (gint index = 0; index < count; ++index) {
        GMenuModel* section = g_menu_model_get_item_link(model, index, G_MENU_LINK_SECTION);
        if (section) {
            append_gmenu_model_items(section, shell, top_level, appended_any);
            g_object_unref(section);
            continue;
        }

        GtkWidget* item = build_gmenu_item(model, index, top_level);
        if (!item) {
            continue;
        }
        if (top_level) {
            current_menu_widgets_.push_back(item);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(shell), item);
        *appended_any = true;
    }
}

GtkWidget* PanelWindow::build_gmenu_item(GMenuModel* model, gint index, bool top_level) {
    gchar* label = nullptr;
    gchar* detailed_action = nullptr;
    g_menu_model_get_item_attribute(model, index, G_MENU_ATTRIBUTE_LABEL, "s", &label);
    g_menu_model_get_item_attribute(model, index, G_MENU_ATTRIBUTE_ACTION, "s", &detailed_action);
    GVariant* target = g_menu_model_get_item_attribute_value(model, index, G_MENU_ATTRIBUTE_TARGET, nullptr);
    GMenuModel* submenu_model = g_menu_model_get_item_link(model, index, G_MENU_LINK_SUBMENU);

    const std::string label_text = label ? label : "";
    const std::string action_text = detailed_action ? detailed_action : "";
    if (label_text.empty() && action_text.empty() && !submenu_model) {
        g_free(label);
        g_free(detailed_action);
        if (target) {
            g_variant_unref(target);
        }
        return top_level ? nullptr : gtk_separator_menu_item_new();
    }

    GtkWidget* item = nullptr;
    GVariant* action_state = nullptr;
    GActionGroup* action_group = nullptr;
    std::string action_name;
    if (!action_text.empty()) {
        const std::size_t separator = action_text.find('.');
        if (separator != std::string::npos && separator + 1 < action_text.size()) {
            action_group = gmenu_action_group_for_namespace(action_text.substr(0, separator));
            action_name = action_text.substr(separator + 1);
            if (action_group && g_action_group_has_action(action_group, action_name.c_str())) {
                action_state = g_action_group_get_action_state(action_group, action_name.c_str());
            }
        }
    }

    if (!submenu_model && action_state && g_variant_is_of_type(action_state, G_VARIANT_TYPE_BOOLEAN)) {
        item = gtk_check_menu_item_new_with_mnemonic(label_text.c_str());
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), g_variant_get_boolean(action_state));
    } else {
        item = gtk_menu_item_new_with_mnemonic(label_text.c_str());
    }

    if (action_group && !action_name.empty() && g_action_group_has_action(action_group, action_name.c_str())) {
        gtk_widget_set_sensitive(item, g_action_group_get_action_enabled(action_group, action_name.c_str()));
    }

    if (submenu_model) {
        GtkWidget* submenu = gtk_menu_new();
        bool submenu_has_items = false;
        append_gmenu_model_items(submenu_model, submenu, false, &submenu_has_items);
        if (submenu_has_items) {
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
        } else {
            gtk_widget_destroy(submenu);
        }
    } else if (!action_text.empty()) {
        auto* binding = new GMenuActionBinding;
        binding->detailed_action = action_text;
        binding->target = target;
        target = nullptr;
        g_object_set_data_full(G_OBJECT(item), "milopanel-gmenu-action", binding, free_gmenu_action_binding);
        g_signal_connect(item, "activate", G_CALLBACK(on_gmenu_item_activate), this);
    }

    if (action_state) {
        g_variant_unref(action_state);
    }
    if (submenu_model) {
        g_object_unref(submenu_model);
    }
    if (target) {
        g_variant_unref(target);
    }
    g_free(label);
    g_free(detailed_action);
    return item;
}

void PanelWindow::on_menu_dbus_signal(
    GDBusConnection*,
    const gchar*,
    const gchar*,
    const gchar*,
    const gchar*,
    GVariant*,
    gpointer user_data) {
    static_cast<PanelWindow*>(user_data)->schedule_menu_refresh();
}

gboolean PanelWindow::on_menu_refresh_timeout(gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    panel->menu_refresh_source_id_ = 0;
    panel->update_menu_bar();
    return G_SOURCE_REMOVE;
}

void PanelWindow::on_registrar_dbus_signal(
    GDBusConnection*,
    const gchar*,
    const gchar*,
    const gchar*,
    const gchar*,
    GVariant*,
    gpointer user_data) {
    static_cast<PanelWindow*>(user_data)->schedule_active_window_refresh();
}

gboolean PanelWindow::on_active_refresh_timeout(gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    panel->active_refresh_source_id_ = 0;
    panel->update_active_window();
    return G_SOURCE_REMOVE;
}

gboolean PanelWindow::on_tray_background_refresh_timeout(gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    panel->tray_background_refresh_source_id_ = 0;
    panel->tray_host_.refresh_background();
    return G_SOURCE_REMOVE;
}

GdkFilterReturn PanelWindow::on_root_event(GdkXEvent* xevent, GdkEvent*, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    auto* event = static_cast<XEvent*>(xevent);
    if (!panel || event->type != PropertyNotify) {
        return GDK_FILTER_CONTINUE;
    }
    if (event->xproperty.window == panel->root_window_ &&
        event->xproperty.atom == panel->active_window_atom_) {
        panel->schedule_active_window_refresh();
    }
    if (event->xproperty.window == panel->current_active_window_id_ &&
        event->xproperty.atom == panel->net_wm_state_atom_) {
        panel->update_fullscreen_state();
    }
    return GDK_FILTER_CONTINUE;
}

void PanelWindow::on_dbus_menu_item_activate(GtkMenuItem* item, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "dbusmenu-id"));
    panel->send_menu_event(id, "clicked");
}

void PanelWindow::on_dbus_submenu_select(GtkMenuItem* item, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "dbusmenu-id"));
    panel->request_menu_about_to_show(id);
}

void PanelWindow::on_gmenu_item_activate(GtkMenuItem* item, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    auto* binding = static_cast<GMenuActionBinding*>(g_object_get_data(G_OBJECT(item), "milopanel-gmenu-action"));
    if (!panel || !binding) {
        return;
    }
    panel->activate_gmenu_action(binding->detailed_action, binding->target);
}

gboolean PanelWindow::on_global_menu_button_press(GtkWidget* widget, GdkEventButton* event, gpointer) {
    if (event->button != 1) {
        return FALSE;
    }
    GtkWidget* menu = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "milopanel-popup-menu"));
    if (!menu || !GTK_IS_MENU(menu)) {
        return FALSE;
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(
        GTK_MENU(menu),
        widget,
        GDK_GRAVITY_SOUTH_WEST,
        GDK_GRAVITY_NORTH_WEST,
        reinterpret_cast<GdkEvent*>(event));
    return TRUE;
}

gboolean PanelWindow::on_global_menu_button_enter(GtkWidget* widget, GdkEventCrossing* event, gpointer) {
    GtkWidget* menu = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "milopanel-popup-menu"));
    if (!menu || !GTK_IS_MENU(menu)) {
        return FALSE;
    }
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(
        GTK_MENU(menu),
        widget,
        GDK_GRAVITY_SOUTH_WEST,
        GDK_GRAVITY_NORTH_WEST,
        reinterpret_cast<GdkEvent*>(event));
    return FALSE;
}

gboolean PanelWindow::on_realize(GtkWidget*, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    panel->reposition();
    panel->reserve_screen_space();
    panel->tray_host_.start(GTK_WIDGET(panel->window_), panel->tray_box_, panel->settings_.icon_size);
    panel->sni_host_.start(panel->tray_box_, panel->settings_.icon_size);
    return FALSE;
}

gboolean PanelWindow::on_size_allocate(GtkWidget*, GdkRectangle*, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    if (!panel->geometry_applied_) {
        panel->reposition();
    }
    return FALSE;
}

gboolean PanelWindow::on_menu_button_press(GtkWidget*, GdkEventButton* event, gpointer user_data) {
    if (event->button != 1) {
        return FALSE;
    }
    auto* panel = static_cast<PanelWindow*>(user_data);
    GtkWidget* menu = build_app_menu(panel->menu_entries_, GTK_WINDOW(panel->window_));
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
    return TRUE;
}

gboolean PanelWindow::on_volume_button_press(GtkWidget*, GdkEventButton* event, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    if (event->button == 1) {
        if (gtk_widget_get_visible(panel->volume_popup_)) {
            gtk_widget_hide(panel->volume_popup_);
        } else {
            panel->update_volume();
            panel->show_popup_below_widget(panel->volume_popup_, panel->volume_button_);
        }
        return TRUE;
    }
    if (event->button == 2) {
        panel->launch_command("pactl set-sink-mute @DEFAULT_SINK@ toggle");
        g_timeout_add(120, +[](gpointer data) -> gboolean {
            static_cast<PanelWindow*>(data)->update_volume();
            return FALSE;
        }, panel);
        return TRUE;
    }
    if (event->button == 3) {
        panel->launch_command("audio-config");
        return TRUE;
    }
    return FALSE;
}

gboolean PanelWindow::on_volume_scroll(GtkWidget*, GdkEventScroll* event, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    if (event->direction == GDK_SCROLL_UP) {
        panel->adjust_volume(5);
        return TRUE;
    }
    if (event->direction == GDK_SCROLL_DOWN) {
        panel->adjust_volume(-5);
        return TRUE;
    }
    return FALSE;
}

gboolean PanelWindow::on_notification_button_press(GtkWidget*, GdkEventButton* event, gpointer user_data) {
    if (event->button != 1) {
        return FALSE;
    }
    static_cast<PanelWindow*>(user_data)->launch_command("xfce4-notifyd-config");
    return TRUE;
}

gboolean PanelWindow::on_clock_button_press(GtkWidget*, GdkEventButton* event, gpointer user_data) {
    if (event->button != 1) {
        return FALSE;
    }
    auto* panel = static_cast<PanelWindow*>(user_data);
    if (gtk_widget_get_visible(panel->clock_popup_)) {
        gtk_widget_hide(panel->clock_popup_);
    } else {
        panel->show_popup_below_widget(panel->clock_popup_, panel->clock_button_);
    }
    return TRUE;
}

void PanelWindow::on_popover_volume_changed(GtkRange* range, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    if (panel->updating_vol_popup_ || !gtk_widget_get_visible(panel->volume_popup_)) {
        return;
    }
    double value = gtk_range_get_value(range);
    panel->launch_command("pactl set-sink-volume @DEFAULT_SINK@ " + std::to_string(static_cast<int>(value)) + "%");
    
    std::string lbl = std::to_string(static_cast<int>(value)) + "%";
    gtk_label_set_text(GTK_LABEL(panel->vol_popup_label_), lbl.c_str());

    g_timeout_add(100, +[](gpointer data) -> gboolean {
        static_cast<PanelWindow*>(data)->update_volume();
        return FALSE;
    }, panel);
}

void PanelWindow::on_popover_mute_clicked(GtkButton*, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    panel->launch_command("pactl set-sink-mute @DEFAULT_SINK@ toggle");
    g_timeout_add(100, +[](gpointer data) -> gboolean {
        static_cast<PanelWindow*>(data)->update_volume();
        return FALSE;
    }, panel);
}

void PanelWindow::on_popover_input_volume_changed(GtkRange* range, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    if (panel->updating_vol_popup_ || !gtk_widget_get_visible(panel->volume_popup_)) {
        return;
    }
    double value = gtk_range_get_value(range);
    panel->launch_command("pactl set-source-volume @DEFAULT_SOURCE@ " + std::to_string(static_cast<int>(value)) + "%");

    std::string lbl = std::to_string(static_cast<int>(value)) + "%";
    gtk_label_set_text(GTK_LABEL(panel->input_popup_label_), lbl.c_str());

    g_timeout_add(100, +[](gpointer data) -> gboolean {
        static_cast<PanelWindow*>(data)->update_volume();
        return FALSE;
    }, panel);
}

void PanelWindow::on_popover_input_mute_clicked(GtkButton*, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    panel->launch_command("pactl set-source-mute @DEFAULT_SOURCE@ toggle");
    g_timeout_add(100, +[](gpointer data) -> gboolean {
        static_cast<PanelWindow*>(data)->update_volume();
        return FALSE;
    }, panel);
}

void PanelWindow::on_popover_pavucontrol_clicked(GtkButton*, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    gtk_widget_hide(panel->volume_popup_);
    panel->launch_command("pavucontrol");
}

gboolean PanelWindow::on_clock_timeout(gpointer user_data) {
    static_cast<PanelWindow*>(user_data)->update_clock();
    return TRUE;
}

gboolean PanelWindow::on_battery_timeout(gpointer user_data) {
    static_cast<PanelWindow*>(user_data)->update_battery();
    return TRUE;
}

gboolean PanelWindow::on_volume_timeout(gpointer user_data) {
    static_cast<PanelWindow*>(user_data)->update_volume();
    return TRUE;
}

gboolean PanelWindow::on_active_timeout(gpointer user_data) {
    static_cast<PanelWindow*>(user_data)->update_active_window();
    return TRUE;
}

void PanelWindow::on_screen_size_changed(GdkScreen*, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    panel->geometry_applied_ = false;
    panel->struts_applied_ = false;
    panel->reposition();
    panel->reserve_screen_space();
}

void PanelWindow::on_destroy(GtkWidget*, gpointer user_data) {
    auto* panel = static_cast<PanelWindow*>(user_data);
    g_application_quit(G_APPLICATION(panel->app_));
}

void PanelWindow::show_popup_below_widget(GtkWidget* popup, GtkWidget* button) {
    GdkWindow* gdk_win = gtk_widget_get_window(button);
    if (!gdk_win) return;

    int win_x = 0, win_y = 0;
    gdk_window_get_origin(gdk_win, &win_x, &win_y);

    GtkAllocation alloc;
    gtk_widget_get_allocation(button, &alloc);

    GtkRequisition req;
    gtk_widget_get_preferred_size(popup, nullptr, &req);

    // Position it below the panel (win_y + alloc.height)
    int x = win_x + alloc.x;
    int y = win_y + alloc.height;

    // Align right edges if popup would go off the screen
    GdkRectangle monitor = primary_monitor_geometry();
    if (x + req.width > monitor.x + monitor.width) {
        x = win_x + alloc.x + alloc.width - req.width;
    }
    if (x < monitor.x) {
        x = monitor.x;
    }

    gtk_window_move(GTK_WINDOW(popup), x, y);
    gtk_widget_show_all(popup);
}

gboolean PanelWindow::on_popup_map(GtkWidget* widget, gpointer user_data) {
    (void)user_data;
    GdkDisplay* display = gtk_widget_get_display(widget);
    GdkSeat* seat = gdk_display_get_default_seat(display);
    if (seat) {
        gdk_seat_grab(seat, gtk_widget_get_window(widget), GDK_SEAT_CAPABILITY_ALL, TRUE, nullptr, nullptr, nullptr, nullptr);
    }
    return FALSE;
}

gboolean PanelWindow::on_popup_unmap(GtkWidget* widget, gpointer user_data) {
    (void)user_data;
    GdkDisplay* display = gtk_widget_get_display(widget);
    GdkSeat* seat = gdk_display_get_default_seat(display);
    if (seat) {
        gdk_seat_ungrab(seat);
    }
    return FALSE;
}

gboolean PanelWindow::on_popup_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    (void)user_data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    // event coordinates are relative to the popup window itself.
    // If they are negative or exceed width/height, it's outside the popup.
    if (event->x < 0 || event->y < 0 || event->x > alloc.width || event->y > alloc.height) {
        gtk_widget_hide(widget);
        return TRUE; // Consume click event to dismiss
    }
    return FALSE;
}
