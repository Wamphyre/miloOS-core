#include "dock_window.hpp"

#include <glib/gstdio.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace {

constexpr guint TARGET_URI_LIST = 1;
constexpr guint TARGET_TEXT = 2;
constexpr guint AUTOHIDE_DELAY_MS = 700;
constexpr guint AUTOHIDE_TICK_MS = 180;
constexpr int REVEAL_EDGE_SIZE = 2;
constexpr int MAGNIFY_DELTA = 8;

bool rects_intersect(const GdkRectangle& a, const GdkRectangle& b) {
    return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
}

bool contains_token(const std::set<std::string>& a, const std::set<std::string>& b) {
    for (const auto& token : a) {
        if (b.count(token)) {
            return true;
        }
    }
    return false;
}

std::string css_for_settings(const DockSettings& settings) {
    const bool dark = settings.is_dark();
    const char* shell_top = dark ? "rgba(35, 35, 35, 0.90)" : "rgba(241, 241, 241, 0.86)";
    const char* shell_mid = dark ? "rgba(28, 28, 28, 0.87)" : "rgba(222, 222, 222, 0.84)";
    const char* shell_bottom = dark ? "rgba(20, 20, 20, 0.82)" : "rgba(141, 141, 141, 0.78)";
    const char* shell_border = dark ? "rgba(0, 0, 0, 0.47)" : "rgba(0, 0, 0, 0.39)";
    const char* shell_border_bottom = dark ? "rgba(0, 0, 0, 0.50)" : "rgba(0, 0, 0, 0.42)";
    const char* inner_highlight = dark ? "rgba(255, 255, 255, 0.12)" : "rgba(255, 255, 255, 0.50)";
    const char* inner_shadow = dark ? "rgba(0, 0, 0, 0.38)" : "rgba(0, 0, 0, 0.12)";
    const char* drop_color = dark ? "rgba(93, 180, 255, 0.96)" : "rgba(37, 121, 216, 0.92)";
    const char* empty_color = dark ? "alpha(#f2f2f2, 0.72)" : "alpha(#111111, 0.64)";

    const int icon_size = settings.icon_size;
    const int pad_x = std::max(9, static_cast<int>(icon_size * 0.30));
    const int pad_bottom = std::max(5, static_cast<int>(icon_size * 0.15));
    const int item_radius = std::max(8, static_cast<int>(icon_size * 0.24));

    std::ostringstream css;
    css
        << "window.milodock-window { background-color: transparent; }\n"
        << "window.milodock-window menu, window.milodock-window popover { border-radius: 8px; }\n"
        << ".milodock-shell {\n"
        << "  background-image: linear-gradient(to bottom, " << shell_top << ", " << shell_mid << " 52%, " << shell_bottom << ");\n"
        << "  border: 1px solid " << shell_border << ";\n"
        << "  border-bottom-color: " << shell_border_bottom << ";\n"
        << "  border-radius: 32px 32px 0 0;\n"
        << "  box-shadow: inset 0 1px " << inner_highlight << ", inset 0 -1px " << inner_shadow << ", 0 1px 4px alpha(#000000, " << (dark ? "0.28" : "0.22") << ");\n"
        << "  padding: 0 " << pad_x << "px " << pad_bottom << "px " << pad_x << "px;\n"
        << "}\n"
        << ".milodock-item {\n"
        << "  background-color: transparent;\n"
        << "  border-radius: " << item_radius << "px;\n"
        << "  padding: 2px 1px 0 1px;\n"
        << "  transition: 110ms ease-out;\n"
        << "}\n"
        << ".milodock-item:hover { background: rgba(255,255,255,0.00); box-shadow: none; }\n"
        << ".milodock-item-running { background: rgba(255,255,255,0.00); }\n"
        << ".milodock-drop-before { box-shadow: inset 2px 0 " << drop_color << "; }\n"
        << ".milodock-drop-after { box-shadow: inset -2px 0 " << drop_color << "; }\n"
        << ".milodock-empty { color: " << empty_color << "; padding: 5px 12px; }\n";
    return css.str();
}

std::vector<std::string> selection_uris(GtkSelectionData* data) {
    std::vector<std::string> uris;
    gchar** raw_uris = gtk_selection_data_get_uris(data);
    if (!raw_uris) {
        return uris;
    }
    for (gchar** uri = raw_uris; *uri; ++uri) {
        uris.emplace_back(*uri);
    }
    g_strfreev(raw_uris);
    return uris;
}

GtkTargetEntry* drag_targets(int* count) {
    static GtkTargetEntry targets[] = {
        {const_cast<gchar*>("text/uri-list"), 0, TARGET_URI_LIST},
        {const_cast<gchar*>("text/plain"), 0, TARGET_TEXT},
    };
    *count = static_cast<int>(G_N_ELEMENTS(targets));
    return targets;
}

struct MenuAction {
    DockWindow* dock = nullptr;
    Launcher launcher;
    bool force_new = false;
};

struct WindowMenuAction {
    DockWindow* dock = nullptr;
    Window xid = 0;
};

void menu_action_destroy(gpointer data, GClosure*) {
    delete static_cast<MenuAction*>(data);
}

void window_menu_action_destroy(gpointer data, GClosure*) {
    delete static_cast<WindowMenuAction*>(data);
}

std::string short_window_label(const TrackedWindow& window, int index) {
    std::string label = window.name.empty() ? "" : window.name;
    if (label.empty()) {
        label = window.wm_class.empty() ? "Ventana" : window.wm_class;
    }
    if (label.empty()) {
        label = "Ventana";
    }
    if (label.size() > 72) {
        label = label.substr(0, 69) + "...";
    }

    std::ostringstream stream;
    stream << index << ". " << label;
    if (window.minimized) {
        stream << " (minimizada)";
    }
    return stream.str();
}

std::string first_nonempty_window_value(const TrackedWindow& window) {
    const std::string values[] = {
        window.desktop_file,
        window.appimage_path,
        window.gtk_application_id,
        window.wm_class,
        window.wm_instance,
        window.name
    };
    for (const std::string& value : values) {
        if (!value.empty()) {
            return value;
        }
    }
    return "window-" + std::to_string(window.xid);
}

std::string sanitized_launcher_id(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (char& c : value) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') {
            c = '-';
        }
    }
    while (value.find("--") != std::string::npos) {
        value.replace(value.find("--"), 2, "-");
    }
    if (value.empty()) {
        value = "window";
    }
    return value;
}

Launcher fallback_launcher_for_window(const TrackedWindow& window) {
    Launcher launcher;
    launcher.desktop_id = "running-" + sanitized_launcher_id(first_nonempty_window_value(window));
    launcher.name = !window.wm_class.empty() ? window.wm_class : first_nonempty_window_value(window);
    launcher.match_tokens = !window.identity_tokens.empty() ? window.identity_tokens : window.tokens;
    if (launcher.match_tokens.empty()) {
        launcher.match_tokens.insert(sanitized_launcher_id(first_nonempty_window_value(window)));
    }
    return launcher;
}

} // namespace

DockWindow::DockWindow(GtkApplication* app) : app_(app), settings_(DockSettings::load()) {
    build_ui();
    load_css();
    monitor_settings_file();
    reload_launchers();
    running_source_id_ = g_timeout_add_seconds(1, on_running_timeout, this);
    autohide_source_id_ = g_timeout_add(AUTOHIDE_TICK_MS, on_autohide_timeout, this);
}

DockWindow::~DockWindow() {
    if (running_source_id_) {
        g_source_remove(running_source_id_);
    }
    if (autohide_source_id_) {
        g_source_remove(autohide_source_id_);
    }
    if (hide_source_id_) {
        g_source_remove(hide_source_id_);
    }
    if (reload_settings_source_id_) {
        g_source_remove(reload_settings_source_id_);
    }
    if (settings_monitor_) {
        g_object_unref(settings_monitor_);
    }
    if (css_provider_) {
        g_object_unref(css_provider_);
    }
}

void DockWindow::build_ui() {
    window_ = app_ ? gtk_application_window_new(app_) : gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "miloDock");
    gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window_), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(window_), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(window_), TRUE);
    gtk_window_stick(GTK_WINDOW(window_));
    gtk_window_set_type_hint(GTK_WINDOW(window_), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_widget_set_app_paintable(window_, TRUE);
    gtk_widget_set_name(window_, "milodock-window");
    gtk_style_context_add_class(gtk_widget_get_style_context(window_), "milodock-window");

    GdkScreen* screen = gtk_widget_get_screen(window_);
    GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
    if (visual) {
        gtk_widget_set_visual(window_, visual);
    }

    shell_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, settings_.launcher_spacing);
    gtk_style_context_add_class(gtk_widget_get_style_context(shell_), "milodock-shell");
    gtk_container_add(GTK_CONTAINER(window_), shell_);

    int target_count = 0;
    GtkTargetEntry* targets = drag_targets(&target_count);
    gtk_drag_dest_set(shell_, GTK_DEST_DEFAULT_ALL, targets, target_count, GDK_ACTION_COPY);
    g_signal_connect(shell_, "drag-data-received", G_CALLBACK(on_drag_data_received), this);

    gtk_widget_add_events(window_, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(window_, "destroy", G_CALLBACK(on_destroy), this);
    g_signal_connect(window_, "size-allocate", G_CALLBACK(on_size_allocate), this);
}

void DockWindow::load_css() {
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

void DockWindow::monitor_settings_file() {
    std::string directory = std::string(g_get_user_config_dir()) + "/miloDock";
    g_mkdir_with_parents(directory.c_str(), 0755);
    std::string path = directory + "/settings.ini";
    GFile* file = g_file_new_for_path(path.c_str());
    GError* error = nullptr;
    settings_monitor_ = g_file_monitor_file(file, G_FILE_MONITOR_NONE, nullptr, &error);
    if (settings_monitor_) {
        g_signal_connect(settings_monitor_, "changed", G_CALLBACK(on_settings_changed), this);
    }
    if (error) {
        g_error_free(error);
    }
    g_object_unref(file);
}

void DockWindow::reload_launchers() {
    desktop_cache_loaded_ = false;
    desktop_cache_.clear();
    unavailable_checks_.clear();
    pinned_launchers_ = ::load_launchers();
    render_launchers(launchers_with_running_apps());
}

void DockWindow::render_launchers(const std::vector<Launcher>& launchers) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(shell_));
    for (GList* node = children; node; node = node->next) {
        gtk_widget_destroy(GTK_WIDGET(node->data));
    }
    g_list_free(children);

    displayed_launchers_ = launchers;
    std::set<std::string> ids = pinned_ids();

    if (launchers.empty()) {
        GtkWidget* label = gtk_label_new("Sin lanzadores");
        gtk_style_context_add_class(gtk_widget_get_style_context(label), "milodock-empty");
        gtk_box_pack_start(GTK_BOX(shell_), label, FALSE, FALSE, 0);
        gtk_widget_show_all(shell_);
        reposition();
        return;
    }

    int target_count = 0;
    GtkTargetEntry* targets = drag_targets(&target_count);
    for (const Launcher& launcher : launchers) {
        GtkWidget* item = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(item), TRUE);
        gtk_event_box_set_above_child(GTK_EVENT_BOX(item), TRUE);
        gtk_widget_add_events(
            item,
            GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
                GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
        gtk_style_context_add_class(gtk_widget_get_style_context(item), "milodock-item");
        gtk_widget_set_tooltip_text(item, launcher.name.c_str());

        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
        gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
        gtk_container_add(GTK_CONTAINER(item), box);

        GtkWidget* image = nullptr;
        if (launcher.icon) {
            image = gtk_image_new_from_gicon(launcher.icon, GTK_ICON_SIZE_DIALOG);
        } else {
            image = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DIALOG);
        }
        gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

        GtkWidget* dot = gtk_drawing_area_new();
        gtk_widget_set_size_request(dot, 22, 6);
        gtk_box_pack_start(GTK_BOX(box), dot, FALSE, FALSE, 0);

        auto* data = new ItemData();
        data->dock = this;
        data->launcher = launcher;
        data->pinned = ids.count(launcher.desktop_id) > 0;
        data->item = item;
        data->image = image;
        data->dot = dot;
        g_object_set_data_full(G_OBJECT(item), "item-data", data, [](gpointer raw) {
            delete static_cast<ItemData*>(raw);
        });
        g_object_set_data(G_OBJECT(dot), "item-data", data);

        g_signal_connect(item, "button-press-event", G_CALLBACK(on_item_button_press), data);
        g_signal_connect(item, "button-release-event", G_CALLBACK(on_item_button_release), data);
        g_signal_connect(item, "motion-notify-event", G_CALLBACK(on_item_motion), data);
        g_signal_connect(item, "enter-notify-event", G_CALLBACK(on_item_enter), data);
        g_signal_connect(item, "leave-notify-event", G_CALLBACK(on_item_leave), data);
        g_signal_connect(dot, "draw", G_CALLBACK(on_dot_draw), data);
        gtk_drag_dest_set(item, GTK_DEST_DEFAULT_ALL, targets, target_count, GDK_ACTION_COPY);
        g_signal_connect(item, "drag-data-received", G_CALLBACK(on_drag_data_received), this);

        update_item_visual(data);
        gtk_box_pack_start(GTK_BOX(shell_), item, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(shell_);
    update_item_running_indicators();
    reposition();
}

void DockWindow::show() {
    hidden_ = false;
    gtk_widget_show_all(window_);
    reposition();
}

void DockWindow::present() {
    show_from_autohide();
    gtk_window_present(GTK_WINDOW(window_));
}

void DockWindow::reload_settings() {
    settings_ = DockSettings::load();
    load_css();
    gtk_box_set_spacing(GTK_BOX(shell_), settings_.launcher_spacing);
    apply_settings_to_items();
    apply_autohide_state();
    reposition();
}

void DockWindow::show_preferences_dialog() {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "miloDock",
        GTK_WINDOW(window_),
        GTK_DIALOG_MODAL,
        "Cancelar",
        GTK_RESPONSE_CANCEL,
        "Aplicar",
        GTK_RESPONSE_OK,
        nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 380, 260);

    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_start(grid, 16);
    gtk_widget_set_margin_end(grid, 16);
    gtk_widget_set_margin_top(grid, 16);
    gtk_widget_set_margin_bottom(grid, 16);
    gtk_container_add(GTK_CONTAINER(area), grid);

    GtkWidget* size_label = gtk_label_new("Tamano de iconos");
    gtk_label_set_xalign(GTK_LABEL(size_label), 0.0f);
    GtkWidget* size_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 28, 64, 1);
    gtk_range_set_value(GTK_RANGE(size_scale), settings_.icon_size);
    gtk_scale_set_digits(GTK_SCALE(size_scale), 0);
    gtk_widget_set_hexpand(size_scale, TRUE);
    gtk_grid_attach(GTK_GRID(grid), size_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), size_scale, 1, 0, 1, 1);

    GtkWidget* spacing_label = gtk_label_new("Separacion entre lanzadores");
    gtk_label_set_xalign(GTK_LABEL(spacing_label), 0.0f);
    GtkWidget* spacing_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 16, 1);
    gtk_range_set_value(GTK_RANGE(spacing_scale), settings_.launcher_spacing);
    gtk_scale_set_digits(GTK_SCALE(spacing_scale), 0);
    gtk_widget_set_hexpand(spacing_scale, TRUE);
    gtk_grid_attach(GTK_GRID(grid), spacing_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), spacing_scale, 1, 1, 1, 1);

    GtkWidget* autohide_label = gtk_label_new("Ocultar automaticamente");
    gtk_label_set_xalign(GTK_LABEL(autohide_label), 0.0f);
    GtkWidget* autohide_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(autohide_switch), settings_.auto_hide);
    gtk_grid_attach(GTK_GRID(grid), autohide_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), autohide_switch, 1, 2, 1, 1);

    GtkWidget* effect_label = gtk_label_new("Efecto");
    gtk_label_set_xalign(GTK_LABEL(effect_label), 0.0f);
    GtkWidget* effect_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(effect_combo), "magnify", "Aumentar");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(effect_combo), "none", "Ninguno");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(effect_combo), settings_.effect.c_str());
    gtk_grid_attach(GTK_GRID(grid), effect_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), effect_combo, 1, 3, 1, 1);

    GtkWidget* theme_label = gtk_label_new("Tema");
    gtk_label_set_xalign(GTK_LABEL(theme_label), 0.0f);
    GtkWidget* theme_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme_combo), "auto", "Seguir sistema");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme_combo), "light", "Claro");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(theme_combo), "dark", "Oscuro");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme_combo), settings_.theme.c_str());
    gtk_grid_attach(GTK_GRID(grid), theme_label, 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), theme_combo, 1, 4, 1, 1);

    gtk_widget_show_all(dialog);
    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        settings_.icon_size = static_cast<int>(gtk_range_get_value(GTK_RANGE(size_scale)));
        settings_.launcher_spacing = static_cast<int>(gtk_range_get_value(GTK_RANGE(spacing_scale)));
        settings_.auto_hide = gtk_switch_get_active(GTK_SWITCH(autohide_switch));
        const char* effect = gtk_combo_box_get_active_id(GTK_COMBO_BOX(effect_combo));
        const char* theme = gtk_combo_box_get_active_id(GTK_COMBO_BOX(theme_combo));
        settings_.effect = effect ? effect : "magnify";
        settings_.theme = theme ? theme : "auto";
        settings_.save();
        reload_settings();
    }
    gtk_widget_destroy(dialog);
}

void DockWindow::set_icon_size(int size) {
    settings_.icon_size = std::max(28, std::min(64, size));
    settings_.save();
    reload_settings();
}

void DockWindow::set_launcher_spacing(int spacing) {
    settings_.launcher_spacing = std::max(0, std::min(16, spacing));
    settings_.save();
    reload_settings();
}

void DockWindow::set_auto_hide(bool enabled) {
    settings_.auto_hide = enabled;
    settings_.save();
    reload_settings();
}

void DockWindow::set_effect(const std::string& effect) {
    if (effect == "magnify" || effect == "none") {
        settings_.effect = effect;
        settings_.save();
        reload_settings();
    }
}

void DockWindow::set_theme_mode(const std::string& mode) {
    if (mode == "auto" || mode == "light" || mode == "dark") {
        settings_.theme = mode;
        settings_.save();
        reload_settings();
    }
}

void DockWindow::apply_settings_to_items() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(shell_));
    for (GList* node = children; node; node = node->next) {
        auto* data = static_cast<ItemData*>(g_object_get_data(G_OBJECT(node->data), "item-data"));
        if (data) {
            update_item_visual(data);
        }
    }
    g_list_free(children);
}

void DockWindow::update_item_visual(ItemData* item) {
    if (!item || !item->image) {
        return;
    }
    int reserved = settings_.icon_size;
    if (settings_.effect == "magnify") {
        reserved = std::min(64, settings_.icon_size + MAGNIFY_DELTA);
    }

    int visible_size = settings_.icon_size;
    if (item->hovered && settings_.effect == "magnify") {
        visible_size = reserved;
    }
    gtk_widget_set_size_request(item->image, reserved, reserved);
    gtk_image_set_pixel_size(GTK_IMAGE(item->image), visible_size);

    gtk_widget_set_margin_top(item->item, 0);
    gtk_widget_set_margin_bottom(item->item, 0);
}

std::vector<TrackedWindow> DockWindow::tracked_windows() const {
    return tracker_.windows();
}

bool DockWindow::window_on_active_workspace(const TrackedWindow& window) const {
    return window_on_active_workspace(window, tracker_.current_desktop());
}

bool DockWindow::window_on_active_workspace(const TrackedWindow& window, int current_desktop) const {
    return window.desktop < 0 || window.desktop == current_desktop;
}

bool DockWindow::window_matches_launcher(const TrackedWindow& window, const Launcher& launcher) const {
    return contains_token(window.tokens, launcher.match_tokens);
}

std::vector<TrackedWindow> DockWindow::running_windows_for(const Launcher& launcher) const {
    const auto windows = tracked_windows();
    const int current_desktop = tracker_.current_desktop();
    return running_windows_for(launcher, windows, current_desktop);
}

std::vector<TrackedWindow> DockWindow::running_windows_for(
    const Launcher& launcher,
    const std::vector<TrackedWindow>& windows,
    int current_desktop) const {
    std::vector<TrackedWindow> matches;
    for (const auto& window : windows) {
        if (window.skip_tasklist || !window_on_active_workspace(window, current_desktop)) {
            continue;
        }
        if (window_matches_launcher(window, launcher)) {
            matches.push_back(window);
        }
    }
    return matches;
}

Launcher DockWindow::launcher_for_window(const TrackedWindow& window) {
    if (!window.appimage_path.empty()) {
        Launcher appimage_launcher = launcher_for_appimage(window.appimage_path);
        if (appimage_launcher.app_info) {
            desktop_cache_loaded_ = false;
            desktop_cache_.clear();
            return appimage_launcher;
        }
    }
    if (!desktop_cache_loaded_) {
        desktop_cache_ = all_desktop_launchers();
        desktop_cache_loaded_ = true;
    }
    for (const auto& launcher : desktop_cache_) {
        if (contains_token(window.identity_tokens, launcher.match_tokens)) {
            return launcher;
        }
    }
    return fallback_launcher_for_window(window);
}

std::vector<Launcher> DockWindow::launchers_with_running_apps() {
    const auto windows = tracked_windows();
    const int current_desktop = tracker_.current_desktop();
    return launchers_with_running_apps(windows, current_desktop);
}

std::vector<Launcher> DockWindow::launchers_with_running_apps(
    const std::vector<TrackedWindow>& windows,
    int current_desktop) {
    std::vector<Launcher> launchers = pinned_launchers_;
    std::set<std::string> seen;
    for (const auto& launcher : launchers) {
        seen.insert(launcher.desktop_id);
    }

    for (const auto& window : windows) {
        if (window.skip_tasklist || !window_on_active_workspace(window, current_desktop)) {
            continue;
        }
        bool already_present = false;
        for (const auto& launcher : launchers) {
            if (window_matches_launcher(window, launcher)) {
                already_present = true;
                break;
            }
        }
        if (already_present) {
            continue;
        }

        Launcher launcher = launcher_for_window(window);
        if (launcher.desktop_id.empty() || seen.count(launcher.desktop_id)) {
            continue;
        }
        seen.insert(launcher.desktop_id);
        launchers.push_back(std::move(launcher));
    }
    return launchers;
}

bool DockWindow::update_running_state() {
    const bool launchers_changed = prune_unavailable_launchers();
    const auto windows = tracked_windows();
    const int current_desktop = tracker_.current_desktop();
    std::vector<Launcher> desired = launchers_with_running_apps(windows, current_desktop);
    std::vector<std::string> desired_ids;
    std::vector<std::string> current_ids;
    for (const auto& launcher : desired) {
        desired_ids.push_back(launcher.desktop_id);
    }
    for (const auto& launcher : displayed_launchers_) {
        current_ids.push_back(launcher.desktop_id);
    }
    if (launchers_changed || desired_ids != current_ids) {
        render_launchers(desired);
        return true;
    }
    update_item_running_indicators(windows, current_desktop);
    return true;
}

bool DockWindow::prune_unavailable_launchers() {
    constexpr int UNAVAILABLE_CHECK_LIMIT = 2;
    std::vector<Launcher> available;
    std::set<std::string> current_keys;
    bool removed = false;

    for (const auto& launcher : pinned_launchers_) {
        const std::string key =
            launcher.desktop_id + "\n" + launcher.desktop_path;
        current_keys.insert(key);
        if (launcher_is_available(launcher)) {
            unavailable_checks_.erase(key);
            available.push_back(launcher);
            continue;
        }
        int& checks = unavailable_checks_[key];
        ++checks;
        if (checks < UNAVAILABLE_CHECK_LIMIT) {
            available.push_back(launcher);
            continue;
        }
        unavailable_checks_.erase(key);
        removed = true;
    }

    for (auto item = unavailable_checks_.begin();
         item != unavailable_checks_.end();) {
        if (!current_keys.count(item->first)) {
            item = unavailable_checks_.erase(item);
        } else {
            ++item;
        }
    }

    if (!removed) {
        return false;
    }
    save_launcher_order(available);
    pinned_launchers_ = std::move(available);
    desktop_cache_loaded_ = false;
    desktop_cache_.clear();
    return true;
}

void DockWindow::update_item_running_indicators() {
    const auto windows = tracked_windows();
    const int current_desktop = tracker_.current_desktop();
    update_item_running_indicators(windows, current_desktop);
}

void DockWindow::update_item_running_indicators(const std::vector<TrackedWindow>& windows, int current_desktop) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(shell_));
    for (GList* node = children; node; node = node->next) {
        auto* data = static_cast<ItemData*>(g_object_get_data(G_OBJECT(node->data), "item-data"));
        if (!data) {
            continue;
        }
        int running_count = static_cast<int>(running_windows_for(data->launcher, windows, current_desktop).size());
        if (running_count == data->running_count) {
            continue;
        }
        bool was_running = data->running_count > 0;
        bool running = running_count > 0;
        data->running_count = running_count;
        GtkStyleContext* context = gtk_widget_get_style_context(data->item);
        if (running && !was_running) {
            gtk_style_context_add_class(context, "milodock-item-running");
        } else if (!running && was_running) {
            gtk_style_context_remove_class(context, "milodock-item-running");
        }
        std::ostringstream tooltip;
        tooltip << data->launcher.name;
        if (running_count == 1) {
            tooltip << " (1 ventana)";
        } else if (running_count > 1) {
            tooltip << " (" << running_count << " ventanas)";
        }
        gtk_widget_set_tooltip_text(data->item, tooltip.str().c_str());
        gtk_widget_queue_draw(data->dot);
    }
    g_list_free(children);
}

std::vector<Launcher> DockWindow::current_launchers() const {
    return pinned_launchers_;
}

std::set<std::string> DockWindow::pinned_ids() const {
    std::set<std::string> ids;
    for (const auto& launcher : pinned_launchers_) {
        ids.insert(launcher.desktop_id);
    }
    return ids;
}

void DockWindow::persist_and_reload(const std::vector<Launcher>& launchers) {
    save_launcher_order(launchers);
    pinned_launchers_ = launchers;
    reload_launchers();
}

bool DockWindow::launcher_is_persisted(const Launcher& launcher) const {
    return pinned_ids().count(launcher.desktop_id) > 0;
}

void DockWindow::add_launcher_to_user_config(const Launcher& launcher) {
    std::vector<Launcher> launchers = current_launchers();
    if (!launcher_is_persisted(launcher)) {
        launchers.push_back(launcher);
    }
    persist_and_reload(launchers);
}

void DockWindow::remove_launcher_from_user_config(const Launcher& launcher) {
    std::vector<Launcher> launchers;
    for (const auto& item : current_launchers()) {
        if (item.desktop_id != launcher.desktop_id) {
            launchers.push_back(item);
        }
    }
    persist_and_reload(launchers);
}

void DockWindow::activate_or_launch(const Launcher& launcher, bool force_new) {
    auto windows = running_windows_for(launcher);
    if (!force_new && !windows.empty()) {
        Window target = windows.front().xid;
        if (windows.size() > 1) {
            Window active = tracker_.active_window();
            auto current = std::find_if(windows.begin(), windows.end(), [active](const TrackedWindow& window) {
                return window.xid == active;
            });
            if (current != windows.end()) {
                ++current;
                if (current == windows.end()) {
                    current = windows.begin();
                }
                target = current->xid;
            }
        }
        tracker_.activate(target);
        return;
    }
    launch_app(launcher);
}

void DockWindow::activate_window(Window xid) {
    tracker_.activate(xid);
}

void DockWindow::close_windows_for(const Launcher& launcher) {
    for (const auto& window : running_windows_for(launcher)) {
        tracker_.close(window.xid);
    }
}

void DockWindow::close_window(Window xid) {
    tracker_.close(xid);
}

void DockWindow::show_context_menu(ItemData* item, GdkEventButton* event) {
    if (!item) {
        return;
    }

    auto windows = running_windows_for(item->launcher);
    GtkWidget* menu = gtk_menu_new();
    GtkWidget* title = gtk_menu_item_new_with_label(item->launcher.name.c_str());
    gtk_widget_set_sensitive(title, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), title);

    GtkWidget* open = gtk_menu_item_new_with_label("Abrir");
    g_signal_connect_data(open, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer raw) {
        auto* action = static_cast<MenuAction*>(raw);
        action->dock->activate_or_launch(action->launcher, false);
    }), new MenuAction{this, item->launcher, false}, menu_action_destroy, G_CONNECT_DEFAULT);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), open);

    GtkWidget* new_window = gtk_menu_item_new_with_label("Abrir nueva ventana");
    gtk_widget_set_sensitive(new_window, item->launcher.app_info != nullptr);
    g_signal_connect_data(new_window, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer raw) {
        auto* action = static_cast<MenuAction*>(raw);
        action->dock->activate_or_launch(action->launcher, true);
    }), new MenuAction{this, item->launcher, true}, menu_action_destroy, G_CONNECT_DEFAULT);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), new_window);

    if (!windows.empty()) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

        std::ostringstream window_title_text;
        window_title_text << "Ventanas abiertas (" << windows.size() << ")";
        GtkWidget* windows_title = gtk_menu_item_new_with_label(window_title_text.str().c_str());
        gtk_widget_set_sensitive(windows_title, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), windows_title);

        int index = 1;
        for (const auto& window : windows) {
            GtkWidget* window_item = gtk_menu_item_new_with_label(short_window_label(window, index).c_str());
            g_signal_connect_data(window_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer raw) {
                auto* action = static_cast<WindowMenuAction*>(raw);
                action->dock->activate_window(action->xid);
            }), new WindowMenuAction{this, window.xid}, window_menu_action_destroy, G_CONNECT_DEFAULT);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), window_item);
            ++index;
        }

        if (windows.size() > 1) {
            GtkWidget* close_submenu_item = gtk_menu_item_new_with_label("Cerrar ventana");
            GtkWidget* close_submenu = gtk_menu_new();
            index = 1;
            for (const auto& window : windows) {
                GtkWidget* close_item = gtk_menu_item_new_with_label(short_window_label(window, index).c_str());
                g_signal_connect_data(close_item, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer raw) {
                    auto* action = static_cast<WindowMenuAction*>(raw);
                    action->dock->close_window(action->xid);
                }), new WindowMenuAction{this, window.xid}, window_menu_action_destroy, G_CONNECT_DEFAULT);
                gtk_menu_shell_append(GTK_MENU_SHELL(close_submenu), close_item);
                ++index;
            }
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(close_submenu_item), close_submenu);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), close_submenu_item);
        }

        GtkWidget* close = gtk_menu_item_new_with_label(windows.size() == 1 ? "Cerrar ventana" : "Cerrar todas las ventanas");
        g_signal_connect_data(close, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer raw) {
            auto* action = static_cast<MenuAction*>(raw);
            action->dock->close_windows_for(action->launcher);
        }), new MenuAction{this, item->launcher, false}, menu_action_destroy, G_CONNECT_DEFAULT);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), close);
    }

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    GtkWidget* add = gtk_menu_item_new_with_label("Anadir al lanzador");
    gtk_widget_set_sensitive(
        add,
        item->launcher.app_info != nullptr &&
            !item->launcher.desktop_path.empty() &&
            !launcher_is_persisted(item->launcher));
    g_signal_connect_data(add, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer raw) {
        auto* action = static_cast<MenuAction*>(raw);
        action->dock->add_launcher_to_user_config(action->launcher);
    }), new MenuAction{this, item->launcher, false}, menu_action_destroy, G_CONNECT_DEFAULT);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), add);

    GtkWidget* remove = gtk_menu_item_new_with_label("Quitar del lanzador");
    gtk_widget_set_sensitive(remove, item->pinned && current_launchers().size() > 1);
    g_signal_connect_data(remove, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer raw) {
        auto* action = static_cast<MenuAction*>(raw);
        action->dock->remove_launcher_from_user_config(action->launcher);
    }), new MenuAction{this, item->launcher, false}, menu_action_destroy, G_CONNECT_DEFAULT);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    GtkWidget* preferences = gtk_menu_item_new_with_label("Preferencias...");
    g_signal_connect(preferences, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer raw) {
        static_cast<DockWindow*>(raw)->show_preferences_dialog();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), preferences);

    GtkWidget* reload = gtk_menu_item_new_with_label("Recargar Dock");
    g_signal_connect(reload, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer raw) {
        static_cast<DockWindow*>(raw)->reload_launchers();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), reload);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
}

bool DockWindow::handle_drop(GtkSelectionData* data, guint info, int insert_index) {
    std::vector<Launcher> launchers = current_launchers();
    insert_index = std::max(0, std::min(insert_index, static_cast<int>(launchers.size())));

    std::vector<Launcher> new_launchers;
    if (info == TARGET_URI_LIST) {
        new_launchers = launchers_from_uri_list(selection_uris(data));
    } else if (info == TARGET_TEXT) {
        gchar* text = reinterpret_cast<gchar*>(gtk_selection_data_get_text(data));
        if (text) {
            Launcher launcher = launcher_from_text(text);
            if (launcher.app_info) {
                new_launchers.push_back(std::move(launcher));
            }
            g_free(text);
        }
    }

    bool added = false;
    std::set<std::string> seen;
    for (const auto& launcher : launchers) {
        seen.insert(launcher.desktop_id);
    }
    for (auto& launcher : new_launchers) {
        if (seen.count(launcher.desktop_id)) {
            continue;
        }
        seen.insert(launcher.desktop_id);
        launchers.insert(launchers.begin() + insert_index, std::move(launcher));
        ++insert_index;
        added = true;
    }
    if (added) {
        persist_and_reload(launchers);
    }
    return added;
}

int DockWindow::reorder_insert_index_for_x(int root_x) const {
    int index = 0;
    GList* children = gtk_container_get_children(GTK_CONTAINER(shell_));
    for (GList* node = children; node; node = node->next) {
        auto* data = static_cast<ItemData*>(g_object_get_data(G_OBJECT(node->data), "item-data"));
        if (!data || !data->pinned) {
            continue;
        }
        GdkWindow* window = gtk_widget_get_window(data->item);
        if (!window) {
            ++index;
            continue;
        }
        gint origin_x = 0;
        gint origin_y = 0;
        gdk_window_get_origin(window, &origin_x, &origin_y);
        int width = gtk_widget_get_allocated_width(data->item);
        if (root_x < origin_x + width / 2) {
            g_list_free(children);
            return index;
        }
        ++index;
    }
    g_list_free(children);
    return index;
}

void DockWindow::begin_reorder(ItemData* item, int root_x) {
    if (!item || !item->pinned) {
        return;
    }
    reorder_source_ = item;
    gtk_style_context_add_class(gtk_widget_get_style_context(item->item), "milodock-item-running");
    update_reorder_preview(root_x);
}

void DockWindow::update_reorder_preview(int root_x) {
    if (!reorder_source_) {
        return;
    }
    reorder_insert_index_ = reorder_insert_index_for_x(root_x);
    clear_reorder_preview();

    int index = 0;
    ItemData* last_pinned = nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(shell_));
    for (GList* node = children; node; node = node->next) {
        auto* data = static_cast<ItemData*>(g_object_get_data(G_OBJECT(node->data), "item-data"));
        if (!data || !data->pinned) {
            continue;
        }
        last_pinned = data;
        if (index == reorder_insert_index_) {
            gtk_style_context_add_class(gtk_widget_get_style_context(data->item), "milodock-drop-before");
            g_list_free(children);
            return;
        }
        ++index;
    }
    if (last_pinned) {
        gtk_style_context_add_class(gtk_widget_get_style_context(last_pinned->item), "milodock-drop-after");
    }
    g_list_free(children);
}

void DockWindow::finish_reorder(ItemData* item, int root_x) {
    if (reorder_source_ != item) {
        clear_reorder_preview();
        reorder_source_ = nullptr;
        return;
    }

    update_reorder_preview(root_x);
    std::vector<Launcher> launchers = current_launchers();
    auto current = std::find_if(launchers.begin(), launchers.end(), [&](const Launcher& launcher) {
        return launcher.desktop_id == item->launcher.desktop_id;
    });
    int current_index = current == launchers.end() ? -1 : static_cast<int>(std::distance(launchers.begin(), current));
    int insert_index = std::max(0, std::min(reorder_insert_index_, static_cast<int>(launchers.size())));

    clear_reorder_preview();
    gtk_style_context_remove_class(gtk_widget_get_style_context(item->item), "milodock-item-running");
    reorder_source_ = nullptr;

    if (current_index < 0) {
        return;
    }
    Launcher launcher = launchers[current_index];
    launchers.erase(launchers.begin() + current_index);
    if (current_index < insert_index) {
        --insert_index;
    }
    if (current_index == insert_index) {
        return;
    }
    launchers.insert(launchers.begin() + insert_index, std::move(launcher));
    persist_and_reload(launchers);
}

void DockWindow::clear_reorder_preview() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(shell_));
    for (GList* node = children; node; node = node->next) {
        GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(node->data));
        gtk_style_context_remove_class(context, "milodock-drop-before");
        gtk_style_context_remove_class(context, "milodock-drop-after");
    }
    g_list_free(children);
}

GdkRectangle DockWindow::monitor_geometry() const {
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

bool DockWindow::dock_rect(GdkRectangle* rect) const {
    if (!rect) {
        return false;
    }
    GdkRectangle geometry = monitor_geometry();
    if (geometry.width <= 0 || geometry.height <= 0) {
        return false;
    }

    GtkRequisition min_size;
    GtkRequisition natural_size;
    gtk_widget_get_preferred_size(window_, &min_size, &natural_size);
    const int width = std::max(gtk_widget_get_allocated_width(window_), natural_size.width);
    const int height = std::max(gtk_widget_get_allocated_height(window_), natural_size.height);
    rect->width = width;
    rect->height = height;
    rect->x = geometry.x + std::max(0, (geometry.width - width) / 2);
    rect->y = geometry.y + std::max(0, geometry.height - height);
    return true;
}

bool DockWindow::pointer_position(int* x, int* y) const {
    GdkDisplay* display = gdk_display_get_default();
    if (!display) {
        return false;
    }
    GdkSeat* seat = gdk_display_get_default_seat(display);
    GdkDevice* pointer = seat ? gdk_seat_get_pointer(seat) : nullptr;
    if (!pointer) {
        return false;
    }
    gdk_device_get_position(pointer, nullptr, x, y);
    return true;
}

bool DockWindow::pointer_over_dock() const {
    if (hidden_) {
        return false;
    }
    int x = 0;
    int y = 0;
    GdkRectangle rect;
    if (!pointer_position(&x, &y) || !dock_rect(&rect)) {
        return false;
    }
    return x >= rect.x && x <= rect.x + rect.width && y >= rect.y && y <= rect.y + rect.height;
}

bool DockWindow::pointer_near_reveal_edge() const {
    int x = 0;
    int y = 0;
    GdkRectangle geometry = monitor_geometry();
    GdkRectangle rect;
    if (!pointer_position(&x, &y) || !dock_rect(&rect) || geometry.width <= 0) {
        return false;
    }
    return x >= rect.x - 80 && x <= rect.x + rect.width + 80 && y >= geometry.y + geometry.height - REVEAL_EDGE_SIZE;
}

bool DockWindow::should_autohide_now() const {
    if (!settings_.auto_hide || !tracker_.available()) {
        return false;
    }

    const auto windows = tracker_.windows();
    const int current_desktop = tracker_.current_desktop();
    return should_autohide_now(windows, current_desktop);
}

bool DockWindow::should_autohide_now(const std::vector<TrackedWindow>& windows, int current_desktop) const {
    GdkRectangle dock;
    GdkRectangle monitor = monitor_geometry();
    if (!dock_rect(&dock) || monitor.width <= 0) {
        return false;
    }

    for (const auto& window : windows) {
        if (window.skip_tasklist || window.minimized || !window_on_active_workspace(window, current_desktop)) {
            continue;
        }
        GdkRectangle window_rect{window.x, window.y, window.width, window.height};
        if (!rects_intersect(window_rect, monitor)) {
            continue;
        }
        if (window.fullscreen || window.maximized || rects_intersect(window_rect, dock)) {
            return true;
        }
    }
    return false;
}

void DockWindow::schedule_autohide() {
    const bool should_hide = should_autohide_now();
    if (!should_hide) {
        show_from_autohide();
        return;
    }
    if (hide_source_id_) {
        return;
    }
    hide_source_id_ = g_timeout_add(AUTOHIDE_DELAY_MS, on_hide_timeout, this);
}

void DockWindow::hide_for_autohide() {
    hide_source_id_ = 0;
    if (!settings_.auto_hide || !should_autohide_now() || pointer_over_dock()) {
        return;
    }
    hidden_ = true;
    gtk_widget_hide(window_);
}

void DockWindow::show_from_autohide() {
    if (hide_source_id_) {
        g_source_remove(hide_source_id_);
        hide_source_id_ = 0;
    }
    if (hidden_ || !gtk_widget_get_visible(window_)) {
        hidden_ = false;
        gtk_widget_show_all(window_);
        reposition();
    }
}

void DockWindow::apply_autohide_state() {
    if (settings_.auto_hide && should_autohide_now()) {
        schedule_autohide();
    } else {
        show_from_autohide();
    }
}

void DockWindow::reposition() {
    GdkRectangle rect;
    if (!dock_rect(&rect)) {
        return;
    }
    gtk_window_move(GTK_WINDOW(window_), rect.x, rect.y);
}

gboolean DockWindow::on_size_allocate(GtkWidget*, GdkRectangle*, gpointer user_data) {
    static_cast<DockWindow*>(user_data)->reposition();
    return FALSE;
}

gboolean DockWindow::on_item_button_press(GtkWidget*, GdkEventButton* event, gpointer user_data) {
    auto* item = static_cast<ItemData*>(user_data);
    if (!item || !item->dock) {
        return FALSE;
    }
    if (event->button == 1) {
        item->press_valid = true;
        item->drag_started = false;
        item->press_x = event->x;
        item->press_y = event->y;
        return TRUE;
    }
    if (event->button == 2) {
        item->dock->activate_or_launch(item->launcher, true);
        return TRUE;
    }
    if (event->button == 3) {
        item->dock->show_context_menu(item, event);
        return TRUE;
    }
    return FALSE;
}

gboolean DockWindow::on_item_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    auto* item = static_cast<ItemData*>(user_data);
    if (!item || !item->dock || event->button != 1) {
        return FALSE;
    }

    const bool moved = item->press_valid && gtk_drag_check_threshold(
        widget,
        static_cast<gint>(item->press_x),
        static_cast<gint>(item->press_y),
        static_cast<gint>(event->x),
        static_cast<gint>(event->y));
    item->press_valid = false;

    if (item->drag_started) {
        item->dock->finish_reorder(item, static_cast<int>(event->x_root));
        item->drag_started = false;
        return TRUE;
    }
    if (moved) {
        return TRUE;
    }
    item->dock->activate_or_launch(item->launcher, false);
    return TRUE;
}

gboolean DockWindow::on_item_motion(GtkWidget* widget, GdkEventMotion* event, gpointer user_data) {
    auto* item = static_cast<ItemData*>(user_data);
    if (!item || !item->dock || !item->pinned || !item->press_valid) {
        return FALSE;
    }
    if (!(event->state & GDK_BUTTON1_MASK)) {
        return FALSE;
    }
    if (item->drag_started) {
        item->dock->update_reorder_preview(static_cast<int>(event->x_root));
        return TRUE;
    }
    if (!gtk_drag_check_threshold(
            widget,
            static_cast<gint>(item->press_x),
            static_cast<gint>(item->press_y),
            static_cast<gint>(event->x),
            static_cast<gint>(event->y))) {
        return FALSE;
    }
    item->drag_started = true;
    item->dock->begin_reorder(item, static_cast<int>(event->x_root));
    return TRUE;
}

gboolean DockWindow::on_item_enter(GtkWidget*, GdkEventCrossing*, gpointer user_data) {
    auto* item = static_cast<ItemData*>(user_data);
    if (!item || !item->dock) {
        return FALSE;
    }
    item->hovered = true;
    item->dock->update_item_visual(item);
    item->dock->show_from_autohide();
    return FALSE;
}

gboolean DockWindow::on_item_leave(GtkWidget*, GdkEventCrossing*, gpointer user_data) {
    auto* item = static_cast<ItemData*>(user_data);
    if (!item || !item->dock) {
        return FALSE;
    }
    item->hovered = false;
    item->dock->update_item_visual(item);
    return FALSE;
}

gboolean DockWindow::on_dot_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    auto* item = static_cast<ItemData*>(user_data);
    if (!item || item->running_count <= 0) {
        return FALSE;
    }
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    if (item->dock->settings_.is_dark()) {
        cairo_set_source_rgba(cr, 0.88, 0.93, 1.0, 0.86);
    } else {
        cairo_set_source_rgba(cr, 0.12, 0.16, 0.20, 0.78);
    }
    const int visible_dots = std::min(item->running_count, 3);
    const double radius = 2.0;
    const double spacing = 6.0;
    const double total_width = (visible_dots - 1) * spacing;
    const double start_x = allocation.width / 2.0 - total_width / 2.0;
    const double y = allocation.height / 2.0;
    for (int i = 0; i < visible_dots; ++i) {
        cairo_arc(cr, start_x + i * spacing, y, radius, 0.0, 6.283185307179586);
        cairo_fill(cr);
    }
    return FALSE;
}

void DockWindow::on_drag_data_received(GtkWidget* widget, GdkDragContext* context, gint x, gint, GtkSelectionData* data, guint info, guint time, gpointer user_data) {
    auto* dock = static_cast<DockWindow*>(user_data);
    int insert_index = static_cast<int>(dock->current_launchers().size());
    auto* item = static_cast<ItemData*>(g_object_get_data(G_OBJECT(widget), "item-data"));
    if (item && item->pinned) {
        auto launchers = dock->current_launchers();
        auto current = std::find_if(launchers.begin(), launchers.end(), [&](const Launcher& launcher) {
            return launcher.desktop_id == item->launcher.desktop_id;
        });
        if (current != launchers.end()) {
            insert_index = static_cast<int>(std::distance(launchers.begin(), current));
            if (x >= gtk_widget_get_allocated_width(widget) / 2) {
                ++insert_index;
            }
        }
    }
    bool handled = dock->handle_drop(data, info, insert_index);
    gtk_drag_finish(context, handled, FALSE, time);
}

void DockWindow::on_settings_changed(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer user_data) {
    auto* dock = static_cast<DockWindow*>(user_data);
    if (dock->reload_settings_source_id_) {
        g_source_remove(dock->reload_settings_source_id_);
    }
    dock->reload_settings_source_id_ = g_timeout_add(120, on_reload_settings_timeout, dock);
}

gboolean DockWindow::on_reload_settings_timeout(gpointer user_data) {
    auto* dock = static_cast<DockWindow*>(user_data);
    dock->reload_settings_source_id_ = 0;
    dock->reload_settings();
    return FALSE;
}

gboolean DockWindow::on_running_timeout(gpointer user_data) {
    return static_cast<DockWindow*>(user_data)->update_running_state() ? TRUE : FALSE;
}

gboolean DockWindow::on_autohide_timeout(gpointer user_data) {
    auto* dock = static_cast<DockWindow*>(user_data);
    if (!dock->settings_.auto_hide) {
        if (dock->hidden_) {
            dock->show_from_autohide();
        }
        return TRUE;
    }
    if (!dock->should_autohide_now()) {
        dock->show_from_autohide();
        return TRUE;
    }
    if (dock->hidden_) {
        if (dock->pointer_near_reveal_edge()) {
            dock->show_from_autohide();
        }
        return TRUE;
    }
    if (!dock->pointer_over_dock()) {
        dock->schedule_autohide();
    }
    return TRUE;
}

gboolean DockWindow::on_hide_timeout(gpointer user_data) {
    static_cast<DockWindow*>(user_data)->hide_for_autohide();
    return FALSE;
}

void DockWindow::on_destroy(GtkWidget*, gpointer user_data) {
    auto* dock = static_cast<DockWindow*>(user_data);
    if (dock->app_) {
        g_application_quit(G_APPLICATION(dock->app_));
    } else {
        gtk_main_quit();
    }
}
