#pragma once

#include "menu_model.hpp"
#include "panel_settings.hpp"
#include "system_tray_host.hpp"
#include "status_notifier_host.hpp"

#include <gtk/gtk.h>
#include <gmodule.h>
#include <X11/Xlib.h>

#include <string>
#include <vector>

class PanelWindow {
public:
    explicit PanelWindow(GtkApplication* app, bool reserve_space_override);
    ~PanelWindow();

    void show();
    void reload_settings();

private:
    enum class GlobalMenuKind {
        NoMenu,
        DesktopMenu,
        DBusMenu,
        GMenu
    };

    GtkApplication* app_ = nullptr;
    PanelSettings settings_;
    bool reserve_space_override_ = true;
    GtkWidget* window_ = nullptr;
    GtkWidget* bar_ = nullptr;
    GtkWidget* menu_button_ = nullptr;
    GtkWidget* menu_image_ = nullptr;
    GtkWidget* active_label_ = nullptr;
    GtkWidget* tray_box_ = nullptr;
    GtkWidget* volume_button_ = nullptr;
    GtkWidget* volume_image_ = nullptr;
    GtkWidget* volume_label_ = nullptr;
    GtkWidget* clock_button_ = nullptr;
    GtkWidget* clock_label_ = nullptr;
    GtkWidget* clock_popup_ = nullptr;
    GtkWidget* volume_popup_ = nullptr;
    GtkWidget* vol_popup_mute_btn_ = nullptr;
    GtkWidget* vol_popup_scale_ = nullptr;
    GtkWidget* vol_popup_label_ = nullptr;
    GtkWidget* input_popup_mute_btn_ = nullptr;
    GtkWidget* input_popup_scale_ = nullptr;
    GtkWidget* input_popup_label_ = nullptr;
    bool updating_vol_popup_ = false;
    GtkWidget* notification_button_ = nullptr;
    GtkCssProvider* css_provider_ = nullptr;
    guint clock_source_id_ = 0;
    guint volume_source_id_ = 0;
    guint active_source_id_ = 0;
    guint active_refresh_source_id_ = 0;
    bool root_filter_installed_ = false;
    GdkWindow* root_filter_window_ = nullptr;
    Display* xdisplay_ = nullptr;
    Window root_window_ = 0;
    Atom active_window_atom_ = 0;
    Atom net_wm_state_atom_ = 0;
    Atom fullscreen_atom_ = 0;
    bool panel_hidden_for_fullscreen_ = false;
    bool geometry_applied_ = false;
    bool struts_applied_ = false;
    int last_x_ = 0;
    int last_y_ = 0;
    int last_width_ = 0;
    int last_height_ = 0;
    int last_strut_x_ = 0;
    int last_strut_y_ = 0;
    int last_strut_width_ = 0;
    int last_strut_height_ = 0;
    std::string last_clock_text_;
    std::string last_clock_tooltip_;
    std::string last_volume_label_;
    std::string last_volume_icon_;
    std::string last_active_text_;
    SystemTrayHost tray_host_;
    StatusNotifierHost sni_host_;
    std::vector<MenuEntry> menu_entries_;

    // Global menu
    GlobalMenuKind current_menu_kind_ = GlobalMenuKind::NoMenu;
    GtkWidget* menu_bar_ = nullptr;
    GtkWidget* native_appmenu_ = nullptr;
    GModule* appmenu_module_ = nullptr;
    GDBusConnection* session_bus_ = nullptr;
    guint registrar_window_registered_signal_id_ = 0;
    guint registrar_window_unregistered_signal_id_ = 0;
    guint menu_layout_signal_id_ = 0;
    guint menu_properties_signal_id_ = 0;
    guint menu_refresh_source_id_ = 0;
    std::string current_menu_service_;
    std::string current_menu_path_;
    std::string current_gmenu_bus_;
    std::string current_gmenu_path_;
    std::string current_gmenu_app_path_;
    std::string current_gmenu_window_path_;
    GMenuModel* current_gmenu_model_ = nullptr;
    GActionGroup* unity_action_group_ = nullptr;
    GActionGroup* app_action_group_ = nullptr;
    GActionGroup* win_action_group_ = nullptr;
    std::vector<guint> gmenu_signal_ids_;
    Window current_active_window_id_ = 0;
    Window last_active_window_id_ = 0;
    Window menu_retry_window_id_ = 0;
    int menu_retry_count_ = 0;
    bool active_window_is_desktop_ = true;
    std::vector<GtkWidget*> current_menu_widgets_;

    void build_ui();
    void load_css();
    void setup_active_window_tracking();
    void setup_registrar_signals();
    void schedule_active_window_refresh();
    void reposition();
    void reserve_screen_space();
    void build_menu_button();
    void update_menu_logo();
    void update_clock();
    void update_volume();
    void update_active_window();
    void update_fullscreen_state();
    void launch_command(const std::string& command);
    std::string command_output(const std::string& command);
    int current_volume_percent();
    bool current_volume_muted();
    int current_input_volume_percent();
    bool current_input_muted();
    void adjust_volume(int delta_percent);
    std::string active_window_title();
    GtkWidget* create_appmenu_widget();
    std::vector<Window> menu_candidate_windows(Window window);
    std::string x11_window_string_property(Window window, const char* property_name);
    bool ensure_session_bus();
    bool session_bus_name_has_owner(const char* name);
    bool query_menu_for_window(Window window, std::string* service, std::string* path);
    bool query_gmenu_for_window(Window window, std::string* bus, std::string* menu_path, std::string* app_path, std::string* window_path);
    GtkWidget* build_menu_item_from_layout(GVariant* node, bool top_level);
    GtkWidget* build_global_menu_button(const std::string& label, GtkWidget* menu);
    void subscribe_menu_signals();
    void schedule_menu_refresh();
    void send_menu_event(int id, const char* event_name);
    void request_menu_about_to_show(int id);
    GActionGroup* gmenu_action_group_for_namespace(const std::string& namespace_name);
    void activate_gmenu_action(const std::string& detailed_action, GVariant* target);
    void subscribe_gmenu_signals();
    void append_gmenu_model_items(GMenuModel* model, GtkWidget* shell, bool top_level, bool* appended_any);
    GtkWidget* build_gmenu_item(GMenuModel* model, gint index, bool top_level);
    void install_gmenu_model();
    void build_desktop_menu_bar();
    void detach_gmenu_model();
    void update_menu_bar();
    void clear_menu_bar();
    void cleanup_menu_client();
    void show_popup_below_widget(GtkWidget* popup, GtkWidget* button);

    static void on_menu_dbus_signal(
        GDBusConnection* connection,
        const gchar* sender_name,
        const gchar* object_path,
        const gchar* interface_name,
        const gchar* signal_name,
        GVariant* parameters,
        gpointer user_data);
    static gboolean on_menu_refresh_timeout(gpointer user_data);
    static void on_registrar_dbus_signal(
        GDBusConnection* connection,
        const gchar* sender_name,
        const gchar* object_path,
        const gchar* interface_name,
        const gchar* signal_name,
        GVariant* parameters,
        gpointer user_data);
    static gboolean on_active_refresh_timeout(gpointer user_data);
    static GdkFilterReturn on_root_event(GdkXEvent* xevent, GdkEvent* event, gpointer user_data);
    static void on_dbus_menu_item_activate(GtkMenuItem* item, gpointer user_data);
    static void on_dbus_submenu_select(GtkMenuItem* item, gpointer user_data);
    static void on_gmenu_item_activate(GtkMenuItem* item, gpointer user_data);
    static gboolean on_global_menu_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static gboolean on_global_menu_button_enter(GtkWidget* widget, GdkEventCrossing* event, gpointer user_data);
    static gboolean on_realize(GtkWidget* widget, gpointer user_data);
    static gboolean on_size_allocate(GtkWidget* widget, GdkRectangle* allocation, gpointer user_data);
    static gboolean on_menu_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static gboolean on_volume_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static gboolean on_volume_scroll(GtkWidget* widget, GdkEventScroll* event, gpointer user_data);
    static gboolean on_notification_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static gboolean on_clock_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static void on_popover_volume_changed(GtkRange* range, gpointer user_data);
    static void on_popover_mute_clicked(GtkButton* button, gpointer user_data);
    static void on_popover_input_volume_changed(GtkRange* range, gpointer user_data);
    static void on_popover_input_mute_clicked(GtkButton* button, gpointer user_data);
    static void on_popover_pavucontrol_clicked(GtkButton* button, gpointer user_data);
    static gboolean on_popup_map(GtkWidget* widget, gpointer user_data);
    static gboolean on_popup_unmap(GtkWidget* widget, gpointer user_data);
    static gboolean on_popup_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static gboolean on_clock_timeout(gpointer user_data);
    static gboolean on_volume_timeout(gpointer user_data);
    static gboolean on_active_timeout(gpointer user_data);
    static void on_screen_size_changed(GdkScreen* screen, gpointer user_data);
    static void on_destroy(GtkWidget* widget, gpointer user_data);
};
