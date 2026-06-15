#pragma once

#include "launcher.hpp"
#include "settings.hpp"
#include "window_tracker.hpp"

#include <gtk/gtk.h>

#include <set>
#include <vector>

class DockWindow {
public:
    explicit DockWindow(GtkApplication* app = nullptr);
    ~DockWindow();

    void show();
    void present();
    void reload_launchers();
    void reload_settings();
    void show_preferences_dialog();
    void set_icon_size(int size);
    void set_launcher_spacing(int spacing);
    void set_auto_hide(bool enabled);
    void set_effect(const std::string& effect);
    void set_theme_mode(const std::string& mode);
    const DockSettings& settings() const { return settings_; }

private:
    struct ItemData {
        DockWindow* dock = nullptr;
        Launcher launcher;
        bool pinned = false;
        bool running = false;
        bool hovered = false;
        bool press_valid = false;
        bool drag_started = false;
        double press_x = 0.0;
        double press_y = 0.0;
        GtkWidget* item = nullptr;
        GtkWidget* image = nullptr;
        GtkWidget* dot = nullptr;
    };

    GtkApplication* app_ = nullptr;
    DockSettings settings_;
    WindowTracker tracker_;
    GtkWidget* window_ = nullptr;
    GtkWidget* shell_ = nullptr;
    GtkCssProvider* css_provider_ = nullptr;
    GFileMonitor* settings_monitor_ = nullptr;
    guint reload_settings_source_id_ = 0;
    guint running_source_id_ = 0;
    guint autohide_source_id_ = 0;
    guint hide_source_id_ = 0;
    bool hidden_ = false;
    bool desktop_cache_loaded_ = false;
    ItemData* reorder_source_ = nullptr;
    int reorder_insert_index_ = 0;
    std::vector<Launcher> pinned_launchers_;
    std::vector<Launcher> displayed_launchers_;
    std::vector<Launcher> desktop_cache_;

    void build_ui();
    void load_css();
    void render_launchers(const std::vector<Launcher>& launchers);
    void apply_settings_to_items();
    void update_item_visual(ItemData* item);
    void update_item_running_indicators();
    bool update_running_state();
    std::vector<TrackedWindow> tracked_windows() const;
    std::vector<TrackedWindow> running_windows_for(const Launcher& launcher) const;
    bool window_matches_launcher(const TrackedWindow& window, const Launcher& launcher) const;
    bool window_on_active_workspace(const TrackedWindow& window) const;
    Launcher launcher_for_window(const TrackedWindow& window);
    std::vector<Launcher> launchers_with_running_apps();
    std::vector<Launcher> current_launchers() const;
    std::set<std::string> pinned_ids() const;
    void persist_and_reload(const std::vector<Launcher>& launchers);
    void add_launcher_to_user_config(const Launcher& launcher);
    void remove_launcher_from_user_config(const Launcher& launcher);
    bool launcher_is_persisted(const Launcher& launcher) const;
    void activate_or_launch(const Launcher& launcher, bool force_new);
    void close_windows_for(const Launcher& launcher);
    void show_context_menu(ItemData* item, GdkEventButton* event);
    bool handle_drop(GtkSelectionData* data, guint info, int insert_index);
    int reorder_insert_index_for_x(int root_x) const;
    void begin_reorder(ItemData* item, int root_x);
    void update_reorder_preview(int root_x);
    void finish_reorder(ItemData* item, int root_x);
    void clear_reorder_preview();
    GdkRectangle monitor_geometry() const;
    bool dock_rect(GdkRectangle* rect) const;
    bool pointer_position(int* x, int* y) const;
    bool pointer_over_dock() const;
    bool pointer_near_reveal_edge() const;
    bool should_autohide_now() const;
    void schedule_autohide();
    void hide_for_autohide();
    void show_from_autohide();
    void apply_autohide_state();
    void reposition();
    void monitor_settings_file();

    static gboolean on_size_allocate(GtkWidget* widget, GdkRectangle* allocation, gpointer user_data);
    static gboolean on_item_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static gboolean on_item_button_release(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static gboolean on_item_motion(GtkWidget* widget, GdkEventMotion* event, gpointer user_data);
    static gboolean on_item_enter(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);
    static gboolean on_item_leave(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);
    static gboolean on_dot_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data);
    static void on_drag_data_received(GtkWidget* widget, GdkDragContext* context, gint x, gint y, GtkSelectionData* data, guint info, guint time, gpointer user_data);
    static void on_settings_changed(GFileMonitor* monitor, GFile* file, GFile* other_file, GFileMonitorEvent event_type, gpointer user_data);
    static gboolean on_reload_settings_timeout(gpointer user_data);
    static gboolean on_running_timeout(gpointer user_data);
    static gboolean on_autohide_timeout(gpointer user_data);
    static gboolean on_hide_timeout(gpointer user_data);
    static void on_destroy(GtkWidget* widget, gpointer user_data);
};
