#ifndef APP_WINDOW_HPP
#define APP_WINDOW_HPP

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "sidebar.hpp"
#include "file_view.hpp"
#include "progress_dialog.hpp"

class AppWindow {
public:
    AppWindow(const std::string& initial_dir = "");
    ~AppWindow();

    void show();
    GtkWindow* get_window();
    void show_error_dialog(const std::string& title, const std::string& msg);
    
    // Clipboard & Operations
    std::vector<std::string> clipboard_files;
    std::string clipboard_action;
    
    void start_paste_operation(const std::vector<std::string>& src_paths, const std::string& dest_dir, const std::string& action);
    void start_compress_operation(const std::vector<std::string>& src_paths, const std::string& archive_path, const std::string& format);
    void start_extract_operation(const std::string& archive_path, const std::string& dest_dir);
    
    void select_sidebar_path(const std::string& path);
    void update_breadcrumbs();
    void reload_sidebar();
    void load_directory(const std::string& path, bool add_to_history = true);

    // DND drops
    void handle_drag_data_received(GtkWidget* widget, GdkDragContext* context, gint x, gint y,
                                  GtkSelectionData* data, guint info, guint time);

    bool get_show_hidden() const { return show_hidden; }
    void set_show_hidden(bool show) { show_hidden = show; }

    std::string get_current_search_query() const { return current_search_query; }
    void set_current_search_query(const std::string& query) { current_search_query = query; }

    FileView* get_file_view() { return file_view.get(); }

private:
    GtkWidget* window;
    GtkWidget* main_box;
    GtkWidget* toolbar;
    GtkWidget* path_box; // Breadcrumbs container
    GtkWidget* search_entry;
    GtkWidget* statusbar;
    guint statusbar_context_id;

    // Split pane & Child Views
    GtkWidget* body_paned;
    std::unique_ptr<Sidebar> sidebar;
    std::unique_ptr<FileView> file_view;

    // Navigation Buttons
    GtkWidget* back_btn;
    GtkWidget* forward_btn;
    GtkWidget* up_btn;

    // View Mode Toggle
    GtkWidget* icon_view_btn;
    GtkWidget* list_view_btn;

    // Navigation History
    std::vector<std::string> history;
    int history_index;

    // Search query
    std::string current_search_query;
    bool show_hidden;

    // Operation tracking
    std::atomic<bool> operation_cancelled;
    std::shared_ptr<ProgressDialog> current_progress_dialog;

    void setup_menu_bar();
    void setup_toolbar();
    void setup_statusbar();
    void update_statusbar(const std::string& custom_text = "");
    
    // Event handlers
    static void on_back_clicked(GtkWidget* widget, gpointer data);
    static void on_forward_clicked(GtkWidget* widget, gpointer data);
    static void on_up_clicked(GtkWidget* widget, gpointer data);
    static void on_search_changed(GtkSearchEntry* entry, gpointer data);
    static void on_view_mode_changed(GtkToggleButton* btn, gpointer data);
    static void on_window_destroy(GtkWidget* widget, gpointer data);
    static gboolean on_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data);

    void apply_theme_styling();
    void update_path_recommendations();
    void show_path_entry();
    void show_path_breadcrumbs();
    void navigate_from_location_text(const std::string& text);
    void mount_network_share(const std::string& uri);
    void toggle_hidden_files();
    void show_connect_server_dialog();
    void show_go_location_dialog();

    GtkWidget* path_stack;
    GtkWidget* path_entry;
    GtkListStore* path_completion_model;
    GtkWidget* hidden_mitem;
};

#endif // APP_WINDOW_HPP
