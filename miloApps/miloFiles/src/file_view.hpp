#ifndef FILE_VIEW_HPP
#define FILE_VIEW_HPP

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <memory>

class AppWindow;

class FileView {
public:
    FileView(AppWindow* parent_window, 
             std::function<void(const std::string&)> on_dir_activated_cb,
             std::function<void(const std::string&)> on_status_update_cb);
    ~FileView();

    GtkWidget* get_widget();
    void load_directory(const std::string& path, bool show_hidden, const std::string& search_query = "");
    std::string get_current_dir() const;
    std::vector<std::string> get_selected_paths() const;
    std::string get_drop_destination(GtkWidget* source_widget, gint x, gint y) const;
    void set_view_mode(const std::string& mode); // "icon" or "list"
    void select_all();
    void unselect_all();

    // Icon loading helper
    static GdkPixbuf* get_file_icon(const std::string& path, int size, bool is_dir);

    void handle_cut(const std::vector<std::string>& paths);
    void handle_copy(const std::vector<std::string>& paths);
    void handle_paste();
    void handle_rename(const std::vector<std::string>& paths);
    void handle_trash(const std::vector<std::string>& paths);
    void handle_delete(const std::vector<std::string>& paths);
    void handle_compress(const std::vector<std::string>& paths);
    void handle_extract(const std::vector<std::string>& paths);
    void handle_new_folder();
    void handle_new_file();

private:
    GtkWidget* stack;
    GtkWidget* icon_scrolled;
    GtkWidget* list_scrolled;
    GtkWidget* icon_view;
    GtkWidget* tree_view;

    GtkListStore* icon_store;
    GtkListStore* list_store;

    AppWindow* parent;
    std::string current_dir;
    std::function<void(const std::string&)> on_dir_activated;
    std::function<void(const std::string&)> on_status_update;

    std::string view_mode;
    std::vector<std::string> drag_source_paths;
    bool drag_source_snapshot_valid;
    bool drag_in_progress;

    // Thumbnail Loading & Cache
    struct ThumbnailState;
    std::shared_ptr<ThumbnailState> thumbnail_state;

    GFileMonitor* directory_monitor;
    guint directory_reload_timeout_id;

    void setup_icon_view();
    void setup_tree_view();
    void update_directory_monitor();
    void clear_directory_monitor();
    void schedule_directory_reload();
    void set_selected_paths(const std::vector<std::string>& paths);
    void update_drop_highlight(GtkWidget* widget, gint x, gint y);
    void clear_drop_highlight(GtkWidget* widget);

    void start_thumbnail_loading(const std::string& dir_path, const std::vector<std::string>& files_to_process);
    static void update_item_thumbnail(FileView* self,
                                      const std::shared_ptr<ThumbnailState>& state,
                                      const std::string& file_path,
                                      GdkPixbuf* icon_pb,
                                      GdkPixbuf* list_icon_pb,
                                      int load_id);

    // Callbacks
    static void on_item_activated_icon(GtkIconView* icon_view, GtkTreePath* path, gpointer user_data);
    static void on_row_activated_list(GtkTreeView* tree_view, GtkTreePath* path, GtkTreeViewColumn* column, gpointer user_data);
    static gboolean on_button_press_icon(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static gboolean on_button_press_list(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static void on_directory_changed(GFileMonitor* monitor, GFile* file, GFile* other_file,
                                     GFileMonitorEvent event_type, gpointer user_data);

    void show_context_menu(GdkEventButton* event, const std::vector<std::string>& selected_paths);

    // Context Menu Handlers
    void handle_open(const std::vector<std::string>& paths);
    void handle_open_with_app(const std::vector<std::string>& paths, GAppInfo* app_info);
    void handle_other_app(const std::vector<std::string>& paths);
    void handle_properties(const std::vector<std::string>& paths);
    void handle_open_terminal();
    void handle_add_favorites(const std::vector<std::string>& paths);
};

#endif // FILE_VIEW_HPP
