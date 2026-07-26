#include "file_view.hpp"
#include "app_window.hpp"
#include "utils.hpp"
#include "i18n.hpp"
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstring>
#include <cctype>
#include <ctime>
#include <unordered_set>

static std::unordered_map<std::string, GdkPixbuf*> _icon_pixbuf_cache;
static std::unordered_map<std::string, GdkPixbuf*> _icon_cache;
static std::recursive_mutex _icon_cache_mutex;

struct FileView::ThumbnailState {
    std::unordered_map<std::string, std::pair<GdkPixbuf*, GdkPixbuf*>> cache;
    std::atomic<int> load_id{0};
    std::atomic<bool> alive{true};
    std::mutex mutex;

    ~ThumbnailState() {
        for (auto& pair : cache) {
            if (pair.second.first) g_object_unref(pair.second.first);
            if (pair.second.second) g_object_unref(pair.second.second);
        }
    }
};

static std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return std::tolower(ch);
    });
    return value;
}

static bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string strip_archive_extension(const std::string& filename) {
    std::string lower = to_lower_copy(filename);
    const std::vector<std::string> extensions = {".tar.gz", ".tar.xz", ".tar.bz2", ".zip", ".7z"};
    for (const auto& ext : extensions) {
        if (ends_with(lower, ext)) {
            return filename.substr(0, filename.size() - ext.size());
        }
    }
    return filename;
}

static void set_uri_selection_data(const std::vector<std::string>& paths, GtkSelectionData* data) {
    gchar** uris = g_new0(gchar*, paths.size() + 1);
    for (size_t i = 0; i < paths.size(); ++i) {
        uris[i] = g_strdup(utils::location_to_uri(paths[i]).c_str());
    }
    gtk_selection_data_set_uris(data, uris);
    g_strfreev(uris);
}

static bool app_list_contains(GList* apps, GAppInfo* candidate) {
    for (GList* item = apps; item != nullptr; item = item->next) {
        if (g_app_info_equal(G_APP_INFO(item->data), candidate)) return true;
    }
    return false;
}

static std::vector<GAppInfo*> get_common_applications(const std::vector<std::string>& paths) {
    std::vector<GAppInfo*> common;
    if (paths.empty()) return common;

    GList* apps = g_app_info_get_all_for_type(utils::get_mime_type(paths.front()).c_str());
    for (GList* item = apps; item != nullptr; item = item->next) {
        GAppInfo* app = G_APP_INFO(item->data);
        if (!g_app_info_supports_files(app) && !g_app_info_supports_uris(app)) continue;

        const bool duplicate = std::any_of(
            common.begin(), common.end(),
            [app](GAppInfo* existing) { return g_app_info_equal(existing, app); });
        if (!duplicate) common.push_back(G_APP_INFO(g_object_ref(app)));
    }
    g_list_free_full(apps, g_object_unref);

    for (size_t i = 1; i < paths.size() && !common.empty(); ++i) {
        apps = g_app_info_get_all_for_type(utils::get_mime_type(paths[i]).c_str());
        auto next = std::remove_if(
            common.begin(), common.end(),
            [apps](GAppInfo* app) {
                if (app_list_contains(apps, app)) return false;
                g_object_unref(app);
                return true;
            });
        common.erase(next, common.end());
        g_list_free_full(apps, g_object_unref);
    }

    return common;
}

static GdkDragAction choose_drop_action(GdkDragContext* context) {
    const GdkDragAction allowed = gdk_drag_context_get_actions(context);
    GdkModifierType state = static_cast<GdkModifierType>(0);
    gtk_get_current_event_state(&state);

    if ((state & GDK_CONTROL_MASK) && (allowed & GDK_ACTION_COPY)) {
        return GDK_ACTION_COPY;
    }
    if ((state & GDK_SHIFT_MASK) && (allowed & GDK_ACTION_MOVE)) {
        return GDK_ACTION_MOVE;
    }

    GtkWidget* source = gtk_drag_get_source_widget(context);
    const bool is_milofiles_source =
        source &&
        gtk_style_context_has_class(
            gtk_widget_get_style_context(source), "file-view");
    if (is_milofiles_source && (allowed & GDK_ACTION_MOVE)) {
        return GDK_ACTION_MOVE;
    }

    const GdkDragAction suggested = gdk_drag_context_get_suggested_action(context);
    if (allowed & suggested) return suggested;
    if (allowed & GDK_ACTION_COPY) return GDK_ACTION_COPY;
    return GDK_ACTION_MOVE;
}

static GdkPixbuf* get_icon_pixbuf(const std::string& icon_name, int size) {
    std::lock_guard<std::recursive_mutex> lock(_icon_cache_mutex);
    std::string key = icon_name + "_" + std::to_string(size);
    auto it = _icon_pixbuf_cache.find(key);
    if (it != _icon_pixbuf_cache.end()) {
        return it->second;
    }
    
    GtkIconTheme* theme = gtk_icon_theme_get_default();
    GError* error = NULL;
    GdkPixbuf* pb = gtk_icon_theme_load_icon(theme, icon_name.c_str(), size, GTK_ICON_LOOKUP_FORCE_SIZE, &error);
    if (error) {
        g_error_free(error);
        error = NULL;
        pb = gtk_icon_theme_load_icon(theme, icon_name == "folder" ? "folder" : "text-x-generic", size, GTK_ICON_LOOKUP_FORCE_SIZE, &error);
        if (error) {
            g_error_free(error);
            pb = NULL;
        }
    }
    
    _icon_pixbuf_cache[key] = pb;
    return pb;
}

GdkPixbuf* FileView::get_file_icon(const std::string& path, int size, bool is_dir) {
    std::lock_guard<std::recursive_mutex> lock(_icon_cache_mutex);
    std::string key = path + "_" + std::to_string(size) + "_" + (is_dir ? "d" : "f");
    auto it = _icon_cache.find(key);
    if (it != _icon_cache.end()) {
        return it->second;
    }
    
    GdkPixbuf* pb = NULL;
    if (is_dir) {
        std::string name = utils::get_filename(path);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        
        if (path == g_get_home_dir()) {
            pb = get_icon_pixbuf("user-home", size);
        } else if (name == "downloads") {
            pb = get_icon_pixbuf("folder-download", size);
        } else if (name == "documents") {
            pb = get_icon_pixbuf("folder-documents", size);
        } else if (name == "desktop") {
            pb = get_icon_pixbuf("folder-desktop", size);
        } else if (name == "music") {
            pb = get_icon_pixbuf("folder-music", size);
        } else if (name == "pictures") {
            pb = get_icon_pixbuf("folder-pictures", size);
        } else if (name == "videos") {
            pb = get_icon_pixbuf("folder-videos", size);
        } else {
            pb = get_icon_pixbuf("folder", size);
        }
    } else {
        std::string mime = utils::get_mime_type(path);
        if (!mime.empty()) {
            std::string icon_name = mime;
            std::replace(icon_name.begin(), icon_name.end(), '/', '-');
            pb = get_icon_pixbuf(icon_name, size);
            if (!pb) {
                size_t slash = mime.find('/');
                if (slash != std::string::npos) {
                    std::string prefix = mime.substr(0, slash);
                    pb = get_icon_pixbuf(prefix + "-x-generic", size);
                }
            }
        }
        if (!pb) {
            GFile* gfile = utils::new_gfile_for_location(path);
            GFileInfo* info = g_file_query_info(gfile, "standard::icon", G_FILE_QUERY_INFO_NONE, NULL, NULL);
            if (info) {
                GIcon* icon = g_file_info_get_icon(info);
                if (icon) {
                    GtkIconTheme* theme = gtk_icon_theme_get_default();
                    GtkIconInfo* icon_info = gtk_icon_theme_lookup_by_gicon(theme, icon, size, GTK_ICON_LOOKUP_FORCE_SIZE);
                    if (icon_info) {
                        pb = gtk_icon_info_load_icon(icon_info, NULL);
                        g_object_unref(icon_info);
                    }
                }
                g_object_unref(info);
            }
            g_object_unref(gfile);
        }
        if (!pb) {
            pb = get_icon_pixbuf("text-x-generic", size);
        }
    }
    
    _icon_cache[key] = pb;
    return pb;
}

FileView::FileView(AppWindow* parent_window, 
                   std::function<void(const std::string&)> on_dir_activated_cb,
                   std::function<void(const std::string&)> on_status_update_cb)
    : parent(parent_window), on_dir_activated(on_dir_activated_cb), on_status_update(on_status_update_cb), 
      view_mode("icon"), drag_source_snapshot_valid(false), drag_in_progress(false),
      thumbnail_state(std::make_shared<ThumbnailState>()),
      directory_monitor(nullptr), directory_reload_timeout_id(0) {
    
    stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 150);
    
    icon_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(icon_scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    list_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(list_scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    setup_icon_view();
    setup_tree_view();
    
    gtk_container_add(GTK_CONTAINER(icon_scrolled), icon_view);
    gtk_container_add(GTK_CONTAINER(list_scrolled), tree_view);
    
    gtk_stack_add_named(GTK_STACK(stack), icon_scrolled, "icon");
    gtk_stack_add_named(GTK_STACK(stack), list_scrolled, "list");
    
    gtk_widget_show_all(stack);
}

FileView::~FileView() {
    thumbnail_state->alive.store(false);
    ++thumbnail_state->load_id;
    clear_directory_monitor();
    if (directory_reload_timeout_id != 0) {
        g_source_remove(directory_reload_timeout_id);
        directory_reload_timeout_id = 0;
    }
}

GtkWidget* FileView::get_widget() {
    return stack;
}

void FileView::clear_directory_monitor() {
    if (!directory_monitor) {
        monitored_directory.clear();
        return;
    }
    g_signal_handlers_disconnect_by_data(directory_monitor, this);
    g_file_monitor_cancel(directory_monitor);
    g_object_unref(directory_monitor);
    directory_monitor = nullptr;
    monitored_directory.clear();
}

void FileView::update_directory_monitor() {
    if (directory_monitor &&
        !monitored_directory.empty() &&
        utils::same_location(monitored_directory, current_dir)) {
        return;
    }

    clear_directory_monitor();
    if (current_dir.empty()) return;

    GFile* dir_file = utils::new_gfile_for_location(current_dir);
    GError* error = NULL;
    directory_monitor = g_file_monitor_directory(dir_file, G_FILE_MONITOR_NONE, NULL, &error);
    if (directory_monitor) {
        monitored_directory = current_dir;
        g_file_monitor_set_rate_limit(directory_monitor, 100);
        g_signal_connect(directory_monitor, "changed", G_CALLBACK(on_directory_changed), this);
    }
    if (error) {
        g_error_free(error);
    }
    g_object_unref(dir_file);
}

void FileView::schedule_directory_reload() {
    if (directory_reload_timeout_id != 0) return;

    directory_reload_timeout_id = g_timeout_add(180, [](gpointer user_data) -> gboolean {
        FileView* self = static_cast<FileView*>(user_data);
        self->directory_reload_timeout_id = 0;
        if (!self->current_dir.empty()) {
            self->parent->load_directory(self->current_dir, false);
        }
        return FALSE;
    }, this);
}

void FileView::on_directory_changed(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent event_type, gpointer user_data) {
    if (event_type == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED) return;

    FileView* self = static_cast<FileView*>(user_data);
    if (!self) return;
    self->schedule_directory_reload();
}

void FileView::setup_icon_view() {
    icon_store = gtk_list_store_new(4, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    icon_view = gtk_icon_view_new_with_model(GTK_TREE_MODEL(icon_store));
    g_object_unref(icon_store);
    
    gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(icon_view), 0);
    gtk_icon_view_set_text_column(GTK_ICON_VIEW(icon_view), 1);
    gtk_icon_view_set_item_width(GTK_ICON_VIEW(icon_view), 85);
    gtk_icon_view_set_column_spacing(GTK_ICON_VIEW(icon_view), 10);
    gtk_icon_view_set_row_spacing(GTK_ICON_VIEW(icon_view), 10);
    gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(icon_view), GTK_SELECTION_MULTIPLE);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_view), "file-view");
    
    g_signal_connect(icon_view, "item-activated", G_CALLBACK(on_item_activated_icon), this);
    g_signal_connect(icon_view, "button-press-event", G_CALLBACK(on_button_press_icon), this);
    
    // Setup Drag and Drop Destination
    gtk_drag_dest_set(icon_view, GTK_DEST_DEFAULT_ALL, NULL, 0, (GdkDragAction)(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    gtk_drag_dest_add_uri_targets(icon_view);
    g_signal_connect(icon_view, "drag-data-received", G_CALLBACK(+[](GtkWidget* w, GdkDragContext* context, gint x, gint y,
                                                                      GtkSelectionData* data, guint info, guint time, gpointer user_data) {
        FileView* self = static_cast<FileView*>(user_data);
        self->clear_drop_highlight(w);
        self->parent->handle_drag_data_received(w, context, x, y, data, info, time);
    }), this);
    g_signal_connect(icon_view, "drag-motion", G_CALLBACK(+[](GtkWidget* w, GdkDragContext* context, gint x, gint y,
                                                               guint time, gpointer user_data) -> gboolean {
        FileView* self = static_cast<FileView*>(user_data);
        self->update_drop_highlight(w, x, y);

        const GdkDragAction action = choose_drop_action(context);
        gdk_drag_status(context, action, time);
        return TRUE;
    }), this);
    g_signal_connect(icon_view, "drag-leave", G_CALLBACK(+[](GtkWidget* w, GdkDragContext*, guint, gpointer user_data) {
        static_cast<FileView*>(user_data)->clear_drop_highlight(w);
    }), this);

    GtkTargetEntry source_targets[] = {
        {const_cast<gchar*>("text/uri-list"), 0, 0},
    };
    gtk_icon_view_enable_model_drag_source(GTK_ICON_VIEW(icon_view),
                                           GDK_BUTTON1_MASK,
                                           source_targets,
                                           G_N_ELEMENTS(source_targets),
                                           (GdkDragAction)(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    g_signal_connect(icon_view, "drag-data-get", G_CALLBACK(+[](GtkWidget*, GdkDragContext*, GtkSelectionData* data,
                                                                  guint, guint, gpointer user_data) {
        FileView* self = static_cast<FileView*>(user_data);
        const auto paths = self->drag_source_snapshot_valid
            ? self->drag_source_paths
            : self->get_selected_paths();
        set_uri_selection_data(paths, data);
    }), this);
    g_signal_connect(icon_view, "drag-begin", G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer user_data) {
        FileView* self = static_cast<FileView*>(user_data);
        self->drag_in_progress = true;
        if (!self->drag_source_snapshot_valid) {
            self->drag_source_paths = self->get_selected_paths();
            self->drag_source_snapshot_valid = true;
        }
    }), this);
    g_signal_connect(icon_view, "drag-end", G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer user_data) {
        FileView* self = static_cast<FileView*>(user_data);
        self->drag_in_progress = false;
        self->drag_source_snapshot_valid = false;
        self->drag_source_paths.clear();
    }), this);
    g_signal_connect(icon_view, "button-release-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* event,
                                                                        gpointer user_data) -> gboolean {
        FileView* self = static_cast<FileView*>(user_data);
        if (event->button == 1 && !self->drag_in_progress) {
            self->drag_source_snapshot_valid = false;
            self->drag_source_paths.clear();
        }
        return FALSE;
    }), this);
}

void FileView::setup_tree_view() {
    list_store = gtk_list_store_new(9, 
                                    GDK_TYPE_PIXBUF,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_BOOLEAN,
                                    G_TYPE_INT64,
                                    G_TYPE_INT64
                                    );
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list_store));
    g_object_unref(list_store);
    
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(tree_view), "file-view");
    GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(select, GTK_SELECTION_MULTIPLE);
    gtk_tree_view_set_rubber_banding(GTK_TREE_VIEW(tree_view), TRUE);
    
    // Column Name
    GtkTreeViewColumn* col_name = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col_name, i18n::_("name").c_str());
    gtk_tree_view_column_set_resizable(col_name, TRUE);
    
    GtkCellRenderer* ren_pix = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col_name, ren_pix, FALSE);
    gtk_tree_view_column_add_attribute(col_name, ren_pix, "pixbuf", 0);
    
    GtkCellRenderer* ren_txt = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col_name, ren_txt, TRUE);
    gtk_tree_view_column_add_attribute(col_name, ren_txt, "text", 1);
    
    gtk_tree_view_column_set_sort_column_id(col_name, 1);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_name);
    
    // Column Size
    GtkTreeViewColumn* col_size = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col_size, i18n::_("size").c_str());
    gtk_tree_view_column_set_resizable(col_size, TRUE);
    GtkCellRenderer* ren_size = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col_size, ren_size, TRUE);
    gtk_tree_view_column_add_attribute(col_size, ren_size, "text", 2);
    gtk_tree_view_column_set_sort_column_id(col_size, 7);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_size);
    
    // Column Type
    GtkTreeViewColumn* col_type = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col_type, i18n::_("type").c_str());
    gtk_tree_view_column_set_resizable(col_type, TRUE);
    GtkCellRenderer* ren_type = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col_type, ren_type, TRUE);
    gtk_tree_view_column_add_attribute(col_type, ren_type, "text", 3);
    gtk_tree_view_column_set_sort_column_id(col_type, 3);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_type);
    
    // Column Modified
    GtkTreeViewColumn* col_mod = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col_mod, i18n::_("modified").c_str());
    gtk_tree_view_column_set_resizable(col_mod, TRUE);
    GtkCellRenderer* ren_mod = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col_mod, ren_mod, TRUE);
    gtk_tree_view_column_add_attribute(col_mod, ren_mod, "text", 4);
    gtk_tree_view_column_set_sort_column_id(col_mod, 8);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_mod);
    
    g_signal_connect(tree_view, "row-activated", G_CALLBACK(on_row_activated_list), this);
    g_signal_connect(tree_view, "button-press-event", G_CALLBACK(on_button_press_list), this);
    
    // Setup Drag and Drop Destination
    gtk_drag_dest_set(tree_view, GTK_DEST_DEFAULT_ALL, NULL, 0, (GdkDragAction)(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    gtk_drag_dest_add_uri_targets(tree_view);
    g_signal_connect(tree_view, "drag-data-received", G_CALLBACK(+[](GtkWidget* w, GdkDragContext* context, gint x, gint y,
                                                                      GtkSelectionData* data, guint info, guint time, gpointer user_data) {
        FileView* self = static_cast<FileView*>(user_data);
        self->clear_drop_highlight(w);
        self->parent->handle_drag_data_received(w, context, x, y, data, info, time);
    }), this);
    g_signal_connect(tree_view, "drag-motion", G_CALLBACK(+[](GtkWidget* w, GdkDragContext* context, gint x, gint y,
                                                               guint time, gpointer user_data) -> gboolean {
        FileView* self = static_cast<FileView*>(user_data);
        self->update_drop_highlight(w, x, y);

        const GdkDragAction action = choose_drop_action(context);
        gdk_drag_status(context, action, time);
        return TRUE;
    }), this);
    g_signal_connect(tree_view, "drag-leave", G_CALLBACK(+[](GtkWidget* w, GdkDragContext*, guint, gpointer user_data) {
        static_cast<FileView*>(user_data)->clear_drop_highlight(w);
    }), this);

    GtkTargetEntry source_targets[] = {
        {const_cast<gchar*>("text/uri-list"), 0, 0},
    };
    gtk_tree_view_enable_model_drag_source(GTK_TREE_VIEW(tree_view),
                                           GDK_BUTTON1_MASK,
                                           source_targets,
                                           G_N_ELEMENTS(source_targets),
                                           (GdkDragAction)(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    g_signal_connect(tree_view, "drag-data-get", G_CALLBACK(+[](GtkWidget*, GdkDragContext*, GtkSelectionData* data,
                                                                  guint, guint, gpointer user_data) {
        FileView* self = static_cast<FileView*>(user_data);
        const auto paths = self->drag_source_snapshot_valid
            ? self->drag_source_paths
            : self->get_selected_paths();
        set_uri_selection_data(paths, data);
    }), this);
    g_signal_connect(tree_view, "drag-begin", G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer user_data) {
        FileView* self = static_cast<FileView*>(user_data);
        self->drag_in_progress = true;
        if (!self->drag_source_snapshot_valid) {
            self->drag_source_paths = self->get_selected_paths();
            self->drag_source_snapshot_valid = true;
        }
    }), this);
    g_signal_connect(tree_view, "drag-end", G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer user_data) {
        FileView* self = static_cast<FileView*>(user_data);
        self->drag_in_progress = false;
        self->drag_source_snapshot_valid = false;
        self->drag_source_paths.clear();
    }), this);
    g_signal_connect(tree_view, "button-release-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton* event,
                                                                        gpointer user_data) -> gboolean {
        FileView* self = static_cast<FileView*>(user_data);
        if (event->button == 1 && !self->drag_in_progress) {
            self->drag_source_snapshot_valid = false;
            self->drag_source_paths.clear();
        }
        return FALSE;
    }), this);
}

void FileView::load_directory(const std::string& path, bool show_hidden, const std::string& search_query) {
    std::string next_dir = utils::normalize_path(path);
    if (next_dir.empty()) next_dir = g_get_home_dir();

    const bool preserve_selection =
        !current_dir.empty() && utils::same_location(current_dir, next_dir);
    const std::vector<std::string> selected_before =
        preserve_selection ? get_selected_paths() : std::vector<std::string>{};

    if (!preserve_selection && directory_reload_timeout_id != 0) {
        g_source_remove(directory_reload_timeout_id);
        directory_reload_timeout_id = 0;
    }

    current_dir = next_dir;
    drag_source_paths.clear();
    drag_source_snapshot_valid = false;
    drag_in_progress = false;
    update_directory_monitor();

    GFile* dir_file = utils::new_gfile_for_location(current_dir);
    GError* error = NULL;
    GFileEnumerator* enumerator = g_file_enumerate_children(
        dir_file,
        G_FILE_ATTRIBUTE_STANDARD_NAME ","
        G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
        G_FILE_ATTRIBUTE_STANDARD_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_SIZE ","
        G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
        G_FILE_ATTRIBUTE_TIME_MODIFIED,
        G_FILE_QUERY_INFO_NONE,
        NULL,
        &error);
    if (!enumerator) {
        std::string message = error ? error->message : "Could not open directory " + current_dir;
        if (error) {
            g_error_free(error);
        }
        g_object_unref(dir_file);
        parent->show_error_dialog(i18n::_("cannot_read_dir"), message);
        return;
    }

    struct DirectoryItem {
        std::string name;
        std::string display_name;
        std::string location;
        bool is_dir = false;
        int64_t size = 0;
        int64_t mtime = 0;
    };

    std::vector<DirectoryItem> dir_list;
    std::vector<DirectoryItem> file_list;
    
    std::string search_lower = search_query;
    std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
    
    GFileInfo* info = NULL;
    while ((info = g_file_enumerator_next_file(enumerator, NULL, &error)) != NULL) {
        const char* raw_name = g_file_info_get_name(info);
        if (!raw_name || std::strcmp(raw_name, ".") == 0 || std::strcmp(raw_name, "..") == 0) {
            g_object_unref(info);
            continue;
        }
        if (!show_hidden && g_file_info_get_is_hidden(info)) {
            g_object_unref(info);
            continue;
        }

        const char* display = g_file_info_get_display_name(info);
        std::string name = raw_name;
        std::string display_name = display ? display : name;
        
        if (!search_lower.empty()) {
            std::string name_lower = display_name;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
            if (name_lower.find(search_lower) == std::string::npos) {
                g_object_unref(info);
                continue;
            }
        }
        
        GFile* child = g_file_get_child(dir_file, name.c_str());
        DirectoryItem item;
        item.name = name;
        item.display_name = display_name;
        item.location = utils::location_from_gfile(child);
        item.is_dir = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
        item.size = g_file_info_get_size(info);
        item.mtime = static_cast<int64_t>(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED));

        if (item.is_dir) {
            dir_list.push_back(std::move(item));
        } else {
            file_list.push_back(std::move(item));
        }

        g_object_unref(child);
        g_object_unref(info);
    }
    if (error) {
        g_error_free(error);
    }
    g_object_unref(enumerator);
    g_object_unref(dir_file);

    auto cmp = [](const DirectoryItem& a, const DirectoryItem& b) {
        std::string a_l = a.display_name;
        std::string b_l = b.display_name;
        std::transform(a_l.begin(), a_l.end(), a_l.begin(), ::tolower);
        std::transform(b_l.begin(), b_l.end(), b_l.begin(), ::tolower);
        return a_l < b_l;
    };
    std::sort(dir_list.begin(), dir_list.end(), cmp);
    std::sort(file_list.begin(), file_list.end(), cmp);
    
    gtk_list_store_clear(icon_store);
    gtk_list_store_clear(list_store);
    
    std::vector<std::string> files_to_process;
    
    auto process_item = [&](const DirectoryItem& item) {
        std::string modified = "";
        if (item.mtime > 0) {
            time_t mtime = static_cast<time_t>(item.mtime);
            struct tm* timeinfo = localtime(&mtime);
            if (timeinfo) {
                char buffer[80];
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
                modified = buffer;
            }
        }

        std::string size_str = item.is_dir ? "" : utils::format_size(item.size);
        std::string type_str = utils::get_file_type_description(item.location, item.is_dir);
        
        GdkPixbuf* icon_pb = get_file_icon(item.location, 48, item.is_dir);
        GdkPixbuf* list_icon_pb = get_file_icon(item.location, 20, item.is_dir);
        
        GtkTreeIter iter;
        gtk_list_store_append(icon_store, &iter);
        gtk_list_store_set(icon_store, &iter,
                           0, icon_pb,
                           1, item.display_name.c_str(),
                           2, item.location.c_str(),
                           3, item.is_dir,
                           -1);
                           
        gtk_list_store_append(list_store, &iter);
        gtk_list_store_set(list_store, &iter,
                           0, list_icon_pb,
                           1, item.display_name.c_str(),
                           2, size_str.c_str(),
                           3, type_str.c_str(),
                           4, modified.c_str(),
                           5, item.location.c_str(),
                           6, item.is_dir,
                           7, item.size,
                           8, item.mtime,
                           -1);
                           
        if (!item.is_dir) {
            files_to_process.push_back(item.location);
        }
    };
    
    for (const auto& d : dir_list) process_item(d);
    for (const auto& f : file_list) process_item(f);

    if (preserve_selection) set_selected_paths(selected_before);
    
    start_thumbnail_loading(current_dir, files_to_process);
    
    int total_items = dir_list.size() + file_list.size();
    std::string status_msg = std::to_string(total_items) + " " + i18n::_("items");
    on_status_update(status_msg);
}

std::string FileView::get_current_dir() const {
    return current_dir;
}

std::vector<std::string> FileView::get_selected_paths() const {
    std::vector<std::string> paths;
    if (view_mode == "icon") {
        GList* list = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(icon_view));
        for (GList* l = list; l != NULL; l = l->next) {
            GtkTreePath* tp = static_cast<GtkTreePath*>(l->data);
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(icon_store), &iter, tp)) {
                char* p;
                gtk_tree_model_get(GTK_TREE_MODEL(icon_store), &iter, 2, &p, -1);
                if (p) {
                    paths.push_back(p);
                    g_free(p);
                }
            }
        }
        g_list_free_full(list, (GDestroyNotify)gtk_tree_path_free);
    } else {
        GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
        GList* list = gtk_tree_selection_get_selected_rows(select, NULL);
        for (GList* l = list; l != NULL; l = l->next) {
            GtkTreePath* tp = static_cast<GtkTreePath*>(l->data);
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(list_store), &iter, tp)) {
                char* p;
                gtk_tree_model_get(GTK_TREE_MODEL(list_store), &iter, 5, &p, -1);
                if (p) {
                    paths.push_back(p);
                    g_free(p);
                }
            }
        }
        g_list_free_full(list, (GDestroyNotify)gtk_tree_path_free);
    }
    return paths;
}

void FileView::set_selected_paths(const std::vector<std::string>& paths) {
    const std::unordered_set<std::string> wanted(paths.begin(), paths.end());

    gtk_icon_view_unselect_all(GTK_ICON_VIEW(icon_view));
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(icon_store), &iter);
    while (valid) {
        char* location = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(icon_store), &iter, 2, &location, -1);
        if (location && wanted.find(location) != wanted.end()) {
            GtkTreePath* tree_path = gtk_tree_model_get_path(GTK_TREE_MODEL(icon_store), &iter);
            gtk_icon_view_select_path(GTK_ICON_VIEW(icon_view), tree_path);
            gtk_tree_path_free(tree_path);
        }
        g_free(location);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(icon_store), &iter);
    }

    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_unselect_all(selection);
    valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(list_store), &iter);
    while (valid) {
        char* location = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(list_store), &iter, 5, &location, -1);
        if (location && wanted.find(location) != wanted.end()) {
            GtkTreePath* tree_path = gtk_tree_model_get_path(GTK_TREE_MODEL(list_store), &iter);
            gtk_tree_selection_select_path(selection, tree_path);
            gtk_tree_path_free(tree_path);
        }
        g_free(location);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(list_store), &iter);
    }
}

void FileView::update_drop_highlight(GtkWidget* widget, gint x, gint y) {
    if (widget == icon_view) {
        GtkTreePath* tree_path =
            gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(icon_view), x, y);
        gboolean is_directory = FALSE;
        if (tree_path) {
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(icon_store), &iter, tree_path)) {
                gtk_tree_model_get(GTK_TREE_MODEL(icon_store), &iter, 3, &is_directory, -1);
            }
        }
        gtk_icon_view_set_drag_dest_item(
            GTK_ICON_VIEW(icon_view),
            is_directory ? tree_path : nullptr,
            GTK_ICON_VIEW_DROP_INTO);
        if (tree_path) gtk_tree_path_free(tree_path);
        return;
    }

    if (widget == tree_view) {
        GtkTreePath* tree_path = nullptr;
        gboolean is_directory = FALSE;
        if (gtk_tree_view_get_path_at_pos(
                GTK_TREE_VIEW(tree_view), x, y,
                &tree_path, nullptr, nullptr, nullptr)) {
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(list_store), &iter, tree_path)) {
                gtk_tree_model_get(GTK_TREE_MODEL(list_store), &iter, 6, &is_directory, -1);
            }
        }
        gtk_tree_view_set_drag_dest_row(
            GTK_TREE_VIEW(tree_view),
            is_directory ? tree_path : nullptr,
            GTK_TREE_VIEW_DROP_INTO_OR_AFTER);
        if (tree_path) gtk_tree_path_free(tree_path);
    }
}

void FileView::clear_drop_highlight(GtkWidget* widget) {
    if (widget == icon_view) {
        gtk_icon_view_set_drag_dest_item(
            GTK_ICON_VIEW(icon_view), nullptr, GTK_ICON_VIEW_DROP_INTO);
    } else if (widget == tree_view) {
        gtk_tree_view_set_drag_dest_row(
            GTK_TREE_VIEW(tree_view), nullptr, GTK_TREE_VIEW_DROP_BEFORE);
    }
}

std::string FileView::get_drop_destination(GtkWidget* source_widget, gint x, gint y) const {
    GtkTreePath* tree_path = nullptr;
    GtkTreeModel* model = nullptr;
    int location_column = -1;
    int directory_column = -1;

    if (source_widget == icon_view) {
        tree_path = gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(icon_view), x, y);
        model = GTK_TREE_MODEL(icon_store);
        location_column = 2;
        directory_column = 3;
    } else if (source_widget == tree_view) {
        if (!gtk_tree_view_get_path_at_pos(
                GTK_TREE_VIEW(tree_view), x, y, &tree_path, nullptr, nullptr, nullptr)) {
            tree_path = nullptr;
        }
        model = GTK_TREE_MODEL(list_store);
        location_column = 5;
        directory_column = 6;
    }

    std::string destination = current_dir;
    if (tree_path && model) {
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter(model, &iter, tree_path)) {
            char* location = nullptr;
            gboolean is_directory = FALSE;
            gtk_tree_model_get(
                model, &iter,
                location_column, &location,
                directory_column, &is_directory,
                -1);
            if (location && is_directory) destination = location;
            g_free(location);
        }
        gtk_tree_path_free(tree_path);
    }
    return destination;
}

void FileView::set_view_mode(const std::string& mode) {
    if (mode != "icon" && mode != "list") return;
    if (mode == view_mode) return;

    const std::vector<std::string> selected = get_selected_paths();
    view_mode = mode;
    gtk_stack_set_visible_child_name(GTK_STACK(stack), mode.c_str());
    set_selected_paths(selected);
}

void FileView::select_all() {
    gtk_icon_view_select_all(GTK_ICON_VIEW(icon_view));
    GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_select_all(select);
}

void FileView::unselect_all() {
    gtk_icon_view_unselect_all(GTK_ICON_VIEW(icon_view));
    GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_unselect_all(select);
}

void FileView::on_item_activated_icon(GtkIconView* icon_view, GtkTreePath* path, gpointer user_data) {
    FileView* self = static_cast<FileView*>(user_data);
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(self->icon_store), &iter, path)) {
        char* p;
        gboolean is_dir;
        gtk_tree_model_get(GTK_TREE_MODEL(self->icon_store), &iter, 2, &p, 3, &is_dir, -1);
        if (p) {
            std::string path_s(p);
            g_free(p);
            if (is_dir) {
                self->on_dir_activated(path_s);
            } else {
                utils::open_file(path_s);
            }
        }
    }
}

void FileView::on_row_activated_list(GtkTreeView* tree_view, GtkTreePath* path, GtkTreeViewColumn* column, gpointer user_data) {
    FileView* self = static_cast<FileView*>(user_data);
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(self->list_store), &iter, path)) {
        char* p;
        gboolean is_dir;
        gtk_tree_model_get(GTK_TREE_MODEL(self->list_store), &iter, 5, &p, 6, &is_dir, -1);
        if (p) {
            std::string path_s(p);
            g_free(p);
            if (is_dir) {
                self->on_dir_activated(path_s);
            } else {
                utils::open_file(path_s);
            }
        }
    }
}

gboolean FileView::on_button_press_icon(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    FileView* self = static_cast<FileView*>(user_data);
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        self->drag_source_paths.clear();
        self->drag_source_snapshot_valid = false;
        GtkTreePath* path = gtk_icon_view_get_path_at_pos(
            GTK_ICON_VIEW(self->icon_view), event->x, event->y);
        if (path) {
            if (gtk_icon_view_path_is_selected(GTK_ICON_VIEW(self->icon_view), path)) {
                self->drag_source_paths = self->get_selected_paths();
                self->drag_source_snapshot_valid = true;
            } else if (!(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
                GtkTreeIter iter;
                char* location = nullptr;
                if (gtk_tree_model_get_iter(GTK_TREE_MODEL(self->icon_store), &iter, path)) {
                    gtk_tree_model_get(GTK_TREE_MODEL(self->icon_store), &iter, 2, &location, -1);
                }
                if (location) {
                    self->drag_source_paths = {location};
                    self->drag_source_snapshot_valid = true;
                }
                g_free(location);
            }
        } else if (!(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
            self->unselect_all();
        }
        if (path) gtk_tree_path_free(path);
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkTreePath* path = gtk_icon_view_get_path_at_pos(GTK_ICON_VIEW(self->icon_view), event->x, event->y);
        if (path) {
            if (!gtk_icon_view_path_is_selected(GTK_ICON_VIEW(self->icon_view), path)) {
                gtk_icon_view_unselect_all(GTK_ICON_VIEW(self->icon_view));
                gtk_icon_view_select_path(GTK_ICON_VIEW(self->icon_view), path);
            }
            gtk_tree_path_free(path);
        } else {
            gtk_icon_view_unselect_all(GTK_ICON_VIEW(self->icon_view));
        }
        
        self->show_context_menu(event, self->get_selected_paths());
        return TRUE;
    }
    return FALSE;
}

gboolean FileView::on_button_press_list(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    FileView* self = static_cast<FileView*>(user_data);
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        self->drag_source_paths.clear();
        self->drag_source_snapshot_valid = false;
        GtkTreePath* path = nullptr;
        if (gtk_tree_view_get_path_at_pos(
                GTK_TREE_VIEW(self->tree_view), event->x, event->y,
                &path, nullptr, nullptr, nullptr)) {
            GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->tree_view));
            if (gtk_tree_selection_path_is_selected(selection, path)) {
                self->drag_source_paths = self->get_selected_paths();
                self->drag_source_snapshot_valid = true;
            } else if (!(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
                GtkTreeIter iter;
                char* location = nullptr;
                if (gtk_tree_model_get_iter(GTK_TREE_MODEL(self->list_store), &iter, path)) {
                    gtk_tree_model_get(GTK_TREE_MODEL(self->list_store), &iter, 5, &location, -1);
                }
                if (location) {
                    self->drag_source_paths = {location};
                    self->drag_source_snapshot_valid = true;
                }
                g_free(location);
            }
            gtk_tree_path_free(path);
        } else if (!(event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
            self->unselect_all();
        }
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkTreePath* path;
        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(self->tree_view), event->x, event->y, &path, NULL, NULL, NULL)) {
            GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->tree_view));
            if (!gtk_tree_selection_path_is_selected(select, path)) {
                gtk_tree_selection_unselect_all(select);
                gtk_tree_selection_select_path(select, path);
            }
            gtk_tree_path_free(path);
        } else {
            GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(self->tree_view));
            gtk_tree_selection_unselect_all(select);
        }
        
        self->show_context_menu(event, self->get_selected_paths());
        return TRUE;
    }
    return FALSE;
}

void FileView::start_thumbnail_loading(const std::string& dir_path, const std::vector<std::string>& files_to_process) {
    auto state = thumbnail_state;
    int load_id = ++state->load_id;
    
    std::thread([this, state, dir_path, files_to_process, load_id]() {
        for (const auto& file_path : files_to_process) {
            if (!state->alive.load() || state->load_id.load() != load_id) return;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            std::string filename = utils::get_filename(file_path);
            std::string ext = "";
            size_t dot = filename.find_last_of('.');
            if (dot != std::string::npos) {
                ext = filename.substr(dot);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            }
            
            const std::vector<std::string> img_exts = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".svg"};
            if (std::find(img_exts.begin(), img_exts.end(), ext) == img_exts.end()) {
                std::string mime = utils::get_mime_type(file_path);
                if (mime.rfind("image/", 0) != 0) continue;
            }
            
            GdkPixbuf* icon_pb = NULL;
            GdkPixbuf* list_icon_pb = NULL;
            bool owns_loaded_pixbufs = false;
            
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                auto it = state->cache.find(file_path);
                if (it != state->cache.end()) {
                    icon_pb = it->second.first;
                    list_icon_pb = it->second.second;
                }
            }
            
            if (!icon_pb) {
                owns_loaded_pixbufs = true;
                GFile* gfile = utils::new_gfile_for_location(file_path);
                GFileInfo* info = g_file_query_info(gfile, "thumbnail::path", G_FILE_QUERY_INFO_NONE, NULL, NULL);
                if (info) {
                    const char* thumb_path = g_file_info_get_attribute_string(info, "thumbnail::path");
                    if (thumb_path && access(thumb_path, F_OK) == 0) {
                        icon_pb = gdk_pixbuf_new_from_file_at_scale(thumb_path, 48, 48, TRUE, NULL);
                        list_icon_pb = gdk_pixbuf_new_from_file_at_scale(thumb_path, 20, 20, TRUE, NULL);
                    }
                    g_object_unref(info);
                }
                g_object_unref(gfile);
                
                if (!icon_pb) {
                    std::string local_path = utils::location_to_path(file_path);
                    if (!local_path.empty()) {
                        icon_pb = gdk_pixbuf_new_from_file_at_scale(local_path.c_str(), 48, 48, TRUE, NULL);
                        list_icon_pb = gdk_pixbuf_new_from_file_at_scale(local_path.c_str(), 20, 20, TRUE, NULL);
                    }
                }
                
                if (icon_pb && list_icon_pb) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (state->cache.size() >= 1000) {
                        for (auto& pair : state->cache) {
                            if (pair.second.first) g_object_unref(pair.second.first);
                            if (pair.second.second) g_object_unref(pair.second.second);
                        }
                        state->cache.clear();
                    }
                    g_object_ref(icon_pb);
                    g_object_ref(list_icon_pb);
                    state->cache[file_path] = {icon_pb, list_icon_pb};
                }
            }
            
            if (icon_pb && list_icon_pb && state->alive.load() && state->load_id.load() == load_id) {
                FileView::update_item_thumbnail(this, state, file_path, icon_pb, list_icon_pb, load_id);
            }
            if (owns_loaded_pixbufs) {
                if (icon_pb) g_object_unref(icon_pb);
                if (list_icon_pb) g_object_unref(list_icon_pb);
            }
        }
    }).detach();
}

void FileView::update_item_thumbnail(FileView* self,
                                     const std::shared_ptr<ThumbnailState>& state,
                                     const std::string& file_path,
                                     GdkPixbuf* icon_pb,
                                     GdkPixbuf* list_icon_pb,
                                     int load_id) {
    struct ThumbUpdateData {
        FileView* self;
        std::shared_ptr<ThumbnailState> state;
        std::string file_path;
        GdkPixbuf* icon_pb;
        GdkPixbuf* list_icon_pb;
        int load_id;
    };
    g_object_ref(icon_pb);
    g_object_ref(list_icon_pb);
    ThumbUpdateData* data = new ThumbUpdateData{self, state, file_path, icon_pb, list_icon_pb, load_id};
    g_idle_add([](gpointer user_data) -> gboolean {
        ThumbUpdateData* d = static_cast<ThumbUpdateData*>(user_data);
        if (d->state->alive.load() && d->state->load_id.load() == d->load_id) {
            GtkTreeIter iter;
            gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(d->self->icon_store), &iter);
            while (valid) {
                char* p;
                gtk_tree_model_get(GTK_TREE_MODEL(d->self->icon_store), &iter, 2, &p, -1);
                if (p) {
                    std::string path_s(p);
                    g_free(p);
                    if (path_s == d->file_path) {
                        gtk_list_store_set(d->self->icon_store, &iter, 0, d->icon_pb, -1);
                        break;
                    }
                }
                valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(d->self->icon_store), &iter);
            }
            
            valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(d->self->list_store), &iter);
            while (valid) {
                char* p;
                gtk_tree_model_get(GTK_TREE_MODEL(d->self->list_store), &iter, 5, &p, -1);
                if (p) {
                    std::string path_s(p);
                    g_free(p);
                    if (path_s == d->file_path) {
                        gtk_list_store_set(d->self->list_store, &iter, 0, d->list_icon_pb, -1);
                        break;
                    }
                }
                valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(d->self->list_store), &iter);
            }
        }
        g_object_unref(d->icon_pb);
        g_object_unref(d->list_icon_pb);
        delete d;
        return FALSE;
    }, data);
}

void FileView::show_context_menu(GdkEventButton* event, const std::vector<std::string>& selected_paths) {
    GtkWidget* menu = gtk_menu_new();
    bool is_item = !selected_paths.empty();
    
    if (is_item) {
        GtkWidget* item_open = gtk_menu_item_new_with_label(i18n::_("open").c_str());
        g_signal_connect_swapped(item_open, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_open(self->get_selected_paths());
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_open);
        
        const bool only_files = std::all_of(
            selected_paths.begin(), selected_paths.end(),
            [](const std::string& path) { return !utils::is_directory(path); });
        if (only_files) {
            GtkWidget* item_open_with = gtk_menu_item_new_with_label(i18n::_("open_with").c_str());
            GtkWidget* open_with_menu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_open_with), open_with_menu);

            std::vector<GAppInfo*> apps = get_common_applications(selected_paths);
            for (GAppInfo* app : apps) {
                const char* name = g_app_info_get_display_name(app);
                if (!name) name = g_app_info_get_name(app);
                if (!name) {
                    g_object_unref(app);
                    continue;
                }

                GtkWidget* app_item = gtk_menu_item_new_with_label(name);
                struct LaunchData {
                    FileView* self;
                    GAppInfo* app;
                    std::vector<std::string> paths;
                };
                LaunchData* data = new LaunchData{this, app, selected_paths};
                g_signal_connect_data(app_item, "activate", G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
                    LaunchData* launch = static_cast<LaunchData*>(user_data);
                    launch->self->handle_open_with_app(launch->paths, launch->app);
                }), data, [](gpointer user_data, GClosure*) {
                    LaunchData* launch = static_cast<LaunchData*>(user_data);
                    g_object_unref(launch->app);
                    delete launch;
                }, G_CONNECT_AFTER);
                gtk_menu_shell_append(GTK_MENU_SHELL(open_with_menu), app_item);
            }

            if (!apps.empty()) {
                gtk_menu_shell_append(
                    GTK_MENU_SHELL(open_with_menu),
                    gtk_separator_menu_item_new());
            }

            GtkWidget* other_app_item =
                gtk_menu_item_new_with_label(i18n::_("other_application").c_str());
            struct OtherAppData {
                FileView* self;
                std::vector<std::string> paths;
            };
            OtherAppData* data = new OtherAppData{this, selected_paths};
            g_signal_connect_data(other_app_item, "activate", G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
                OtherAppData* other = static_cast<OtherAppData*>(user_data);
                other->self->handle_other_app(other->paths);
            }), data, [](gpointer user_data, GClosure*) {
                delete static_cast<OtherAppData*>(user_data);
            }, G_CONNECT_AFTER);

            gtk_menu_shell_append(GTK_MENU_SHELL(open_with_menu), other_app_item);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_open_with);
        }
        
        GtkWidget* sep1 = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep1);
        
        GtkWidget* item_cut = gtk_menu_item_new_with_label(i18n::_("cut").c_str());
        g_signal_connect_swapped(item_cut, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_cut(self->get_selected_paths());
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_cut);
        
        GtkWidget* item_copy = gtk_menu_item_new_with_label(i18n::_("copy").c_str());
        g_signal_connect_swapped(item_copy, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_copy(self->get_selected_paths());
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_copy);
    }
    
    GtkWidget* item_paste = gtk_menu_item_new_with_label(i18n::_("paste").c_str());
    gtk_widget_set_sensitive(item_paste, parent->has_paste_data());
    g_signal_connect_swapped(item_paste, "activate", G_CALLBACK(+[](FileView* self) {
        self->handle_paste();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_paste);
    
    if (is_item) {
        GtkWidget* sep2 = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep2);
        
        GtkWidget* item_rename = gtk_menu_item_new_with_label(i18n::_("rename").c_str());
        gtk_widget_set_sensitive(item_rename, selected_paths.size() == 1);
        g_signal_connect_swapped(item_rename, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_rename(self->get_selected_paths());
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_rename);
        
        GtkWidget* item_trash = gtk_menu_item_new_with_label(i18n::_("trash_action").c_str());
        g_signal_connect_swapped(item_trash, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_trash(self->get_selected_paths());
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_trash);
        
        GtkWidget* item_delete = gtk_menu_item_new_with_label(i18n::_("delete").c_str());
        g_signal_connect_swapped(item_delete, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_delete(self->get_selected_paths());
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_delete);
        
        GtkWidget* sep3 = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep3);
        
        GtkWidget* item_compress = gtk_menu_item_new_with_label(i18n::_("compress").c_str());
        g_signal_connect_swapped(item_compress, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_compress(self->get_selected_paths());
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_compress);
        
        if (selected_paths.size() == 1) {
            std::string path_lower = selected_paths[0];
            std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);
            const std::vector<std::string> arch_exts = {".zip", ".tar", ".gz", ".tgz", ".bz2", ".tbz2", ".xz", ".txz", ".7z", ".rar"};
            bool is_archive = false;
            for (const auto& ext : arch_exts) {
                if (path_lower.size() >= ext.size() && path_lower.compare(path_lower.size() - ext.size(), ext.size(), ext) == 0) {
                    is_archive = true;
                    break;
                }
            }
            if (is_archive) {
                GtkWidget* item_extract = gtk_menu_item_new_with_label(i18n::_("extract_here").c_str());
                g_signal_connect_swapped(item_extract, "activate", G_CALLBACK(+[](FileView* self) {
                    self->handle_extract(self->get_selected_paths());
                }), this);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_extract);
            }
            
            if (utils::is_directory(selected_paths[0])) {
                GtkWidget* item_fav = gtk_menu_item_new_with_label(i18n::_("add_to_favorites").c_str());
                g_signal_connect_swapped(item_fav, "activate", G_CALLBACK(+[](FileView* self) {
                    self->handle_add_favorites(self->get_selected_paths());
                }), this);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_fav);
            }
        }
        
        GtkWidget* sep4 = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep4);
        
        GtkWidget* item_props = gtk_menu_item_new_with_label(i18n::_("properties").c_str());
        gtk_widget_set_sensitive(item_props, selected_paths.size() == 1);
        g_signal_connect_swapped(item_props, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_properties(self->get_selected_paths());
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_props);
    } else {
        GtkWidget* sep1 = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep1);
        
        GtkWidget* item_new_folder = gtk_menu_item_new_with_label(i18n::_("new_folder").c_str());
        g_signal_connect_swapped(item_new_folder, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_new_folder();
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_new_folder);
        
        GtkWidget* item_new_file = gtk_menu_item_new_with_label(i18n::_("new_file").c_str());
        g_signal_connect_swapped(item_new_file, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_new_file();
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_new_file);
        
        GtkWidget* sep2 = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep2);
        
        GtkWidget* item_terminal = gtk_menu_item_new_with_label(i18n::_("open_terminal").c_str());
        g_signal_connect_swapped(item_terminal, "activate", G_CALLBACK(+[](FileView* self) {
            self->handle_open_terminal();
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_terminal);

        if (utils::is_directory(current_dir)) {
            GtkWidget* item_fav = gtk_menu_item_new_with_label(i18n::_("add_to_favorites").c_str());
            g_signal_connect_swapped(item_fav, "activate", G_CALLBACK(+[](FileView* self) {
                self->handle_add_favorites({self->current_dir});
            }), this);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_fav);
        }
    }
    
    gtk_widget_show_all(menu);
    g_signal_connect(menu, "selection-done", G_CALLBACK(+[](GtkMenu* finished_menu, gpointer) {
        gtk_widget_destroy(GTK_WIDGET(finished_menu));
    }), nullptr);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
}

void FileView::handle_open(const std::vector<std::string>& paths) {
    std::vector<std::string> files;
    for (const auto& path : paths) {
        if (utils::is_directory(path)) {
            on_dir_activated(path);
        } else if (utils::location_exists(path)) {
            files.push_back(path);
        }
    }
    if (!files.empty() && !utils::open_files(files)) {
        parent->show_error_dialog(i18n::_("open_error"), i18n::_("open_error"));
    }
}

void FileView::handle_open_with_app(const std::vector<std::string>& paths, GAppInfo* app_info) {
    if (paths.empty() || !app_info) return;

    const char* executable = g_app_info_get_executable(app_info);
    char* executable_name = executable ? g_path_get_basename(executable) : nullptr;
    const bool is_vlc = executable_name && g_ascii_strcasecmp(executable_name, "vlc") == 0;
    g_free(executable_name);
    if (is_vlc) {
        std::vector<std::string> argv = {
            executable,
            "--one-instance",
            "--started-from-file"
        };
        for (const auto& path : paths) {
            const std::string local_path = utils::location_to_path(path);
            argv.push_back(local_path.empty() ? utils::location_to_uri(path) : local_path);
        }
        if (!utils::run_command_async(argv)) {
            parent->show_error_dialog(i18n::_("open_error"), i18n::_("open_error"));
        }
        return;
    }

    GList* files = nullptr;
    for (auto path = paths.rbegin(); path != paths.rend(); ++path) {
        files = g_list_prepend(files, utils::new_gfile_for_location(*path));
    }

    GError* error = nullptr;
    if (!g_app_info_launch(app_info, files, nullptr, &error)) {
        parent->show_error_dialog(
            i18n::_("open_error"),
            error ? error->message : i18n::_("open_error"));
    }
    if (error) g_error_free(error);
    g_list_free_full(files, g_object_unref);
}

void FileView::handle_other_app(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    std::string path = paths[0];
    
    GFile* gfile = utils::new_gfile_for_location(path);
    GtkWidget* dialog = gtk_app_chooser_dialog_new(GTK_WINDOW(parent->get_window()), GTK_DIALOG_MODAL, gfile);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        GAppInfo* app_info = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(dialog));
        if (app_info) {
            handle_open_with_app(paths, app_info);
            g_object_unref(app_info);
        }
    }
    gtk_widget_destroy(dialog);
    g_object_unref(gfile);
}

void FileView::handle_properties(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    std::string path = paths[0];
    
    std::string name = utils::get_filename(path);
    bool is_dir = utils::is_directory(path);
    std::string size_str = "Calculating...";
    std::string modified = "N/A";
    std::string owner = "unknown";
    std::string group = "unknown";
    std::string permissions = "";

    GFile* prop_file = utils::new_gfile_for_location(path);
    GError* prop_error = NULL;
    GFileInfo* prop_info = g_file_query_info(
        prop_file,
        G_FILE_ATTRIBUTE_STANDARD_SIZE ","
        G_FILE_ATTRIBUTE_TIME_MODIFIED,
        G_FILE_QUERY_INFO_NONE,
        NULL,
        &prop_error);
    if (prop_info) {
        if (!is_dir) {
            const gint64 size = g_file_info_get_size(prop_info);
            size_str = utils::format_size(size) + " (" + std::to_string(size) + " bytes)";
        }
        const guint64 mtime_attr = g_file_info_get_attribute_uint64(prop_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
        if (mtime_attr > 0) {
            time_t mtime = static_cast<time_t>(mtime_attr);
            struct tm* timeinfo = localtime(&mtime);
            if (timeinfo) {
                char buffer[80];
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
                modified = buffer;
            }
        }
        g_object_unref(prop_info);
    }
    if (prop_error) {
        g_error_free(prop_error);
    }
    g_object_unref(prop_file);

    std::string local_path = utils::location_to_path(path);
    if (is_dir && local_path.empty()) {
        size_str = "N/A";
    }
    if (!local_path.empty()) {
        struct stat st;
        if (stat(local_path.c_str(), &st) == 0) {
        struct passwd* pw = getpwuid(st.st_uid);
        if (pw) owner = pw->pw_name;
        
        struct group* gr = getgrgid(st.st_gid);
        if (gr) group = gr->gr_name;
        
        char perm_buf[32];
        std::snprintf(perm_buf, sizeof(perm_buf), "%o", st.st_mode & 0777);
        permissions = perm_buf;
        }
    }
    
    std::string type_str = utils::get_file_type_description(path, is_dir);
    
    GtkWidget* dialog = gtk_dialog_new_with_buttons((i18n::_("properties_title") + " - " + name).c_str(),
                                                    GTK_WINDOW(parent->get_window()),
                                                    GTK_DIALOG_MODAL,
                                                    i18n::_("close").c_str(), GTK_RESPONSE_CLOSE,
                                                    NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 360, 480);
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(content), 12);
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    
    GtkWidget* notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(content), notebook, TRUE, TRUE, 0);
    
    // General Tab
    GtkWidget* vbox_gen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_gen), 12);
    
    GdkPixbuf* big_pb = get_file_icon(path, 64, is_dir);
    GtkWidget* img = gtk_image_new_from_pixbuf(big_pb);
    gtk_box_pack_start(GTK_BOX(vbox_gen), img, FALSE, FALSE, 10);
    
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_box_pack_start(GTK_BOX(vbox_gen), grid, TRUE, TRUE, 0);
    
    std::vector<std::pair<std::string, std::string>> items_list = {
        {i18n::_("name") + ":", name},
        {i18n::_("type") + ":", type_str},
        {i18n::_("size") + ":", size_str},
        {i18n::_("location") + ":", utils::get_parent_directory(path)},
        {i18n::_("modified") + ":", modified}
    };
    
    GtkWidget* lbl_size_value = NULL;
    for (size_t i = 0; i < items_list.size(); ++i) {
        GtkWidget* lbl_k = gtk_label_new(items_list[i].first.c_str());
        gtk_widget_set_halign(lbl_k, GTK_ALIGN_END);
        GtkStyleContext* k_context = gtk_widget_get_style_context(lbl_k);
        gtk_style_context_add_class(k_context, "dim-label");
        gtk_grid_attach(GTK_GRID(grid), lbl_k, 0, i, 1, 1);
        
        GtkWidget* lbl_v = gtk_label_new(items_list[i].second.c_str());
        gtk_widget_set_halign(lbl_v, GTK_ALIGN_START);
        gtk_label_set_line_wrap(GTK_LABEL(lbl_v), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(lbl_v), 25);
        gtk_grid_attach(GTK_GRID(grid), lbl_v, 1, i, 1, 1);
        
        if (items_list[i].first == i18n::_("size") + ":") {
            lbl_size_value = lbl_v;
        }
    }
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_gen, gtk_label_new(i18n::_("general").c_str()));
    
    // Permissions Tab
    GtkWidget* vbox_perm = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox_perm), 12);
    
    GtkWidget* grid_p = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid_p), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid_p), 10);
    gtk_box_pack_start(GTK_BOX(vbox_perm), grid_p, TRUE, TRUE, 0);
    
    std::vector<std::pair<std::string, std::string>> p_list = {
        {i18n::_("owner") + ":", owner},
        {i18n::_("group") + ":", group},
        {i18n::_("octal") + ":", permissions}
    };
    
    for (size_t i = 0; i < p_list.size(); ++i) {
        GtkWidget* lbl_k = gtk_label_new(p_list[i].first.c_str());
        gtk_widget_set_halign(lbl_k, GTK_ALIGN_END);
        gtk_grid_attach(GTK_GRID(grid_p), lbl_k, 0, i, 1, 1);
        
        GtkWidget* lbl_v = gtk_label_new(p_list[i].second.c_str());
        gtk_widget_set_halign(lbl_v, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(grid_p), lbl_v, 1, i, 1, 1);
    }
    
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_perm, gtk_label_new(i18n::_("permissions").c_str()));
    
    // Async size calculation for directories
    if (is_dir && lbl_size_value && !local_path.empty()) {
        // Increment reference count of label so it stays alive during async thread
        g_object_ref(lbl_size_value);
        std::thread([local_path, lbl_size_value]() {
            int64_t size_bytes = 0;
            int64_t file_count = 0;
            
            // Recursive directory size scan
            std::function<void(const std::string&)> scan = [&](const std::string& dir_path) {
                DIR* dir = opendir(dir_path.c_str());
                if (!dir) return;
                struct dirent* entry;
                while ((entry = readdir(dir)) != NULL) {
                    std::string name = entry->d_name;
                    if (name == "." || name == "..") continue;
                    std::string full = dir_path + "/" + name;
                    struct stat st;
                    if (lstat(full.c_str(), &st) == 0) {
                        if (S_ISDIR(st.st_mode)) {
                            scan(full);
                        } else {
                            size_bytes += st.st_size;
                            file_count++;
                        }
                    }
                    
                    if (file_count % 500 == 0) {
                        struct UpdateData {
                            GtkWidget* lbl;
                            std::string txt;
                        };
                        std::string txt = utils::format_size(size_bytes) + " (" + std::to_string(size_bytes) + " bytes) - " + std::to_string(file_count) + " items";
                        UpdateData* d = new UpdateData{lbl_size_value, txt};
                        g_idle_add([](gpointer ud) -> gboolean {
                            UpdateData* u = static_cast<UpdateData*>(ud);
                            gtk_label_set_text(GTK_LABEL(u->lbl), u->txt.c_str());
                            delete u;
                            return FALSE;
                        }, d);
                    }
                }
                closedir(dir);
            };
            
            scan(local_path);
            
            struct UpdateData {
                GtkWidget* lbl;
                std::string txt;
            };
            std::string txt = utils::format_size(size_bytes) + " (" + std::to_string(size_bytes) + " bytes) - " + std::to_string(file_count) + " items";
            UpdateData* d = new UpdateData{lbl_size_value, txt};
            g_idle_add([](gpointer ud) -> gboolean {
                UpdateData* u = static_cast<UpdateData*>(ud);
                gtk_label_set_text(GTK_LABEL(u->lbl), u->txt.c_str());
                g_object_unref(u->lbl);
                delete u;
                return FALSE;
            }, d);
        }).detach();
    }
    
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void FileView::handle_cut(const std::vector<std::string>& paths) {
    parent->set_clipboard_files(paths, "cut");
}

void FileView::handle_copy(const std::vector<std::string>& paths) {
    parent->set_clipboard_files(paths, "copy");
}

void FileView::handle_paste() {
    if (!parent->load_clipboard_files()) return;
    std::string action = parent->clipboard_action == "cut" ? "cut" : "copy";
    parent->start_paste_operation(parent->clipboard_files, current_dir, action);
}

void FileView::handle_rename(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    std::string old_path = paths[0];
    std::string old_name = utils::get_filename(old_path);
    
    GtkWidget* dialog = gtk_dialog_new_with_buttons(i18n::_("rename").c_str(),
                                                    GTK_WINDOW(parent->get_window()),
                                                    GTK_DIALOG_MODAL,
                                                    i18n::_("cancel").c_str(), GTK_RESPONSE_CANCEL,
                                                    i18n::_("apply").c_str(), GTK_RESPONSE_OK,
                                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(box), 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    
    GtkWidget* lbl = gtk_label_new(i18n::_("enter_new_name").c_str());
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), old_name.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_OK) {
        std::string new_name = gtk_entry_get_text(GTK_ENTRY(entry));
        new_name.erase(new_name.begin(), std::find_if(new_name.begin(), new_name.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        new_name.erase(std::find_if(new_name.rbegin(), new_name.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), new_name.end());
        
        if (!new_name.empty() && new_name != old_name) {
            std::string new_path = utils::child_location(utils::get_parent_directory(old_path), new_name);
            if (utils::move_path(old_path, new_path)) {
                parent->load_directory(current_dir, false);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void FileView::handle_trash(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    parent->start_trash_operation(paths, current_dir);
}

void FileView::handle_delete(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(parent->get_window()),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "%s", i18n::_("confirm_delete").c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), i18n::_("delete_title").c_str());
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        parent->start_delete_operation(paths, current_dir);
    }
}

void FileView::handle_compress(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    
    std::string default_archive = utils::get_filename(paths[0]) + ".zip";
    
    GtkWidget* dialog = gtk_dialog_new_with_buttons(i18n::_("compress").c_str(),
                                                    GTK_WINDOW(parent->get_window()),
                                                    GTK_DIALOG_MODAL,
                                                    i18n::_("cancel").c_str(), GTK_RESPONSE_CANCEL,
                                                    i18n::_("apply").c_str(), GTK_RESPONSE_OK,
                                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(box), 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    
    GtkWidget* lbl = gtk_label_new(i18n::_("archive_name").c_str());
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), default_archive.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    
    GtkWidget* lbl_fmt = gtk_label_new(i18n::_("format").c_str());
    gtk_box_pack_start(GTK_BOX(box), lbl_fmt, FALSE, FALSE, 0);
    
    GtkWidget* combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "zip", "ZIP (.zip)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "7z", "7-Zip (.7z)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "tar.gz", "Tarball GZIP (.tar.gz)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "tar.xz", "Tarball XZ (.tar.xz)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "tar.bz2", "Tarball BZIP2 (.tar.bz2)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 0);
    
    // Sync combo when entry text changes
    struct EntryComboLink { GtkWidget* entry; GtkWidget* combo; };
    EntryComboLink* ec_link = new EntryComboLink{entry, combo};
    g_signal_connect_data(entry, "changed", G_CALLBACK(+[](GtkWidget*, gpointer d) {
        EntryComboLink* link = static_cast<EntryComboLink*>(d);
        std::string txt = gtk_entry_get_text(GTK_ENTRY(link->entry));
        std::string lower = to_lower_copy(txt);
        if (ends_with(lower, ".zip")) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(link->combo), 0);
        } else if (ends_with(lower, ".7z")) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(link->combo), 1);
        } else if (ends_with(lower, ".tar.gz")) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(link->combo), 2);
        } else if (ends_with(lower, ".tar.xz")) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(link->combo), 3);
        } else if (ends_with(lower, ".tar.bz2")) {
            gtk_combo_box_set_active(GTK_COMBO_BOX(link->combo), 4);
        }
    }), ec_link, [](gpointer d, GClosure*) { delete static_cast<EntryComboLink*>(d); }, (GConnectFlags)0);
    
    // Sync entry extension when combo format changes
    EntryComboLink* ce_link = new EntryComboLink{entry, combo};
    g_signal_connect_data(combo, "changed", G_CALLBACK(+[](GtkWidget*, gpointer d) {
        EntryComboLink* link = static_cast<EntryComboLink*>(d);
        std::string txt = gtk_entry_get_text(GTK_ENTRY(link->entry));
        const gchar* fmt = gtk_combo_box_get_active_id(GTK_COMBO_BOX(link->combo));
        if (fmt) {
            std::string format(fmt);

            std::string base = strip_archive_extension(txt);
            gtk_entry_set_text(GTK_ENTRY(link->entry), (base + "." + format).c_str());
        }
    }), ce_link, [](gpointer d, GClosure*) { delete static_cast<EntryComboLink*>(d); }, (GConnectFlags)0);
    
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_OK) {
        std::string archive_name = gtk_entry_get_text(GTK_ENTRY(entry));
        const gchar* format_sel = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
        std::string format = format_sel ? format_sel : "zip";
        
        if (!archive_name.empty()) {
            parent->start_compress_operation(
                paths, utils::child_location(current_dir, archive_name), format);
        }
    }
    gtk_widget_destroy(dialog);
}

void FileView::handle_extract(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    parent->start_extract_operation(paths[0], current_dir);
}

void FileView::handle_new_folder() {
    std::string base_name = i18n::_("new_folder_name");
    std::string name = base_name;
    int counter = 1;
    while (utils::location_exists(utils::child_location(current_dir, name))) {
        counter++;
        name = base_name + " " + std::to_string(counter);
    }

    std::string folder_path = utils::child_location(current_dir, name);
    if (utils::make_directory(folder_path)) {
        parent->load_directory(current_dir, false);
    } else {
        parent->show_error_dialog(i18n::_("cannot_create_folder"), folder_path);
    }
}

void FileView::handle_new_file() {
    std::string base_name = i18n::_("new_file_name");
    std::string name = base_name;
    int counter = 1;
    while (utils::location_exists(utils::child_location(current_dir, name))) {
        counter++;
        name = base_name + " " + std::to_string(counter);
    }

    std::string file_path = utils::child_location(current_dir, name);
    if (utils::create_empty_file(file_path)) {
        parent->load_directory(current_dir, false);
    } else {
        parent->show_error_dialog(i18n::_("new_file"), file_path);
    }
}

void FileView::handle_open_terminal() {
    std::vector<std::string> args = {"xfce4-terminal", "--working-directory=" + current_dir};
    gchar** spawn_argv = g_new0(gchar*, args.size() + 1);
    for (size_t i = 0; i < args.size(); ++i) {
        spawn_argv[i] = g_strdup(args[i].c_str());
    }
    GError* error = NULL;
    g_spawn_async(NULL, spawn_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
    if (error) {
        g_error_free(error);
        error = NULL;
        // fallback to x-terminal-emulator
        std::vector<std::string> fallback_args = {"x-terminal-emulator"};
        gchar** fallback_argv = g_new0(gchar*, fallback_args.size() + 1);
        fallback_argv[0] = g_strdup("x-terminal-emulator");
        g_spawn_async(NULL, fallback_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
        g_free(fallback_argv[0]);
        g_free(fallback_argv);
    }
    for (size_t i = 0; i < args.size(); ++i) {
        g_free(spawn_argv[i]);
    }
    g_free(spawn_argv);
}

void FileView::handle_add_favorites(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    std::string path = paths[0];

    std::string name = utils::get_filename(path);
    if (utils::add_favorite(path, name)) {
        parent->reload_sidebar();
    }
}
