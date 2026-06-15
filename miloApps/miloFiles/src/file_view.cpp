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

static std::unordered_map<std::string, GdkPixbuf*> _icon_pixbuf_cache;
static std::unordered_map<std::string, GdkPixbuf*> _icon_cache;
static std::recursive_mutex _icon_cache_mutex;

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
            GFile* gfile = g_file_new_for_path(path.c_str());
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
      view_mode("icon"), current_thumbnail_load_id(0) {
    
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
    std::lock_guard<std::mutex> lock(cache_mutex);
    for (auto& pair : thumbnail_cache) {
        if (pair.second.first) g_object_unref(pair.second.first);
        if (pair.second.second) g_object_unref(pair.second.second);
    }
}

GtkWidget* FileView::get_widget() {
    return stack;
}

void FileView::setup_icon_view() {
    icon_store = gtk_list_store_new(4, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    icon_view = gtk_icon_view_new_with_model(GTK_TREE_MODEL(icon_store));
    g_object_unref(icon_store);
    
    gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(icon_view), 0);
    gtk_icon_view_set_text_column(GTK_ICON_VIEW(icon_view), 1);
    gtk_icon_view_set_item_width(GTK_ICON_VIEW(icon_view), 85);
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
        self->parent->handle_drag_data_received(w, context, x, y, data, info, time);
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
        self->parent->handle_drag_data_received(w, context, x, y, data, info, time);
    }), this);
}

void FileView::load_directory(const std::string& path, bool show_hidden, const std::string& search_query) {
    current_dir = path;
    
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        parent->show_error_dialog(i18n::_("cannot_read_dir"), "Could not open directory " + path);
        return;
    }
    
    std::vector<std::string> dir_list;
    std::vector<std::string> file_list;
    
    std::string search_lower = search_query;
    std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (!show_hidden && name[0] == '.') continue;
        
        if (!search_lower.empty()) {
            std::string name_lower = name;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
            if (name_lower.find(search_lower) == std::string::npos) continue;
        }
        
        std::string full_path = path + "/" + name;
        struct stat st;
        bool is_dir = false;
        if (stat(full_path.c_str(), &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
        }
        
        if (is_dir) {
            dir_list.push_back(name);
        } else {
            file_list.push_back(name);
        }
    }
    closedir(dir);
    
    auto cmp = [](const std::string& a, const std::string& b) {
        std::string a_l = a;
        std::string b_l = b;
        std::transform(a_l.begin(), a_l.end(), a_l.begin(), ::tolower);
        std::transform(b_l.begin(), b_l.end(), b_l.begin(), ::tolower);
        return a_l < b_l;
    };
    std::sort(dir_list.begin(), dir_list.end(), cmp);
    std::sort(file_list.begin(), file_list.end(), cmp);
    
    gtk_list_store_clear(icon_store);
    gtk_list_store_clear(list_store);
    
    std::vector<std::string> files_to_process;
    
    auto process_item = [&](const std::string& name, bool is_dir) {
        std::string full_path = path + "/" + name;
        int64_t raw_size = 0;
        int64_t raw_mtime = 0;
        std::string modified = "";
        
        struct stat st;
        if (stat(full_path.c_str(), &st) == 0) {
            raw_size = st.st_size;
            raw_mtime = st.st_mtime;
            
            struct tm* timeinfo = localtime(&st.st_mtime);
            char buffer[80];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", timeinfo);
            modified = buffer;
        }
        
        std::string size_str = is_dir ? "" : utils::format_size(raw_size);
        std::string type_str = utils::get_file_type_description(full_path, is_dir);
        
        GdkPixbuf* icon_pb = get_file_icon(full_path, 48, is_dir);
        GdkPixbuf* list_icon_pb = get_file_icon(full_path, 20, is_dir);
        
        GtkTreeIter iter;
        gtk_list_store_append(icon_store, &iter);
        gtk_list_store_set(icon_store, &iter,
                           0, icon_pb,
                           1, name.c_str(),
                           2, full_path.c_str(),
                           3, is_dir,
                           -1);
                           
        gtk_list_store_append(list_store, &iter);
        gtk_list_store_set(list_store, &iter,
                           0, list_icon_pb,
                           1, name.c_str(),
                           2, size_str.c_str(),
                           3, type_str.c_str(),
                           4, modified.c_str(),
                           5, full_path.c_str(),
                           6, is_dir,
                           7, raw_size,
                           8, raw_mtime,
                           -1);
                           
        if (!is_dir) {
            files_to_process.push_back(full_path);
        }
    };
    
    for (const auto& d : dir_list) process_item(d, true);
    for (const auto& f : file_list) process_item(f, false);
    
    start_thumbnail_loading(path, files_to_process);
    
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

void FileView::set_view_mode(const std::string& mode) {
    view_mode = mode;
    gtk_stack_set_visible_child_name(GTK_STACK(stack), mode.c_str());
}

void FileView::select_all() {
    if (view_mode == "icon") {
        gtk_icon_view_select_all(GTK_ICON_VIEW(icon_view));
    } else {
        GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
        gtk_tree_selection_select_all(select);
    }
}

void FileView::unselect_all() {
    if (view_mode == "icon") {
        gtk_icon_view_unselect_all(GTK_ICON_VIEW(icon_view));
    } else {
        GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
        gtk_tree_selection_unselect_all(select);
    }
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
    int load_id = ++current_thumbnail_load_id;
    
    std::thread([this, dir_path, files_to_process, load_id]() {
        for (const auto& file_path : files_to_process) {
            if (this->current_thumbnail_load_id.load() != load_id) return;
            
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
            
            {
                std::lock_guard<std::mutex> lock(cache_mutex);
                auto it = thumbnail_cache.find(file_path);
                if (it != thumbnail_cache.end()) {
                    icon_pb = it->second.first;
                    list_icon_pb = it->second.second;
                }
            }
            
            if (!icon_pb) {
                GFile* gfile = g_file_new_for_path(file_path.c_str());
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
                    icon_pb = gdk_pixbuf_new_from_file_at_scale(file_path.c_str(), 48, 48, TRUE, NULL);
                    list_icon_pb = gdk_pixbuf_new_from_file_at_scale(file_path.c_str(), 20, 20, TRUE, NULL);
                }
                
                if (icon_pb && list_icon_pb) {
                    std::lock_guard<std::mutex> lock(cache_mutex);
                    if (thumbnail_cache.size() >= 1000) {
                        for (auto& pair : thumbnail_cache) {
                            if (pair.second.first) g_object_unref(pair.second.first);
                            if (pair.second.second) g_object_unref(pair.second.second);
                        }
                        thumbnail_cache.clear();
                    }
                    g_object_ref(icon_pb);
                    g_object_ref(list_icon_pb);
                    thumbnail_cache[file_path] = {icon_pb, list_icon_pb};
                }
            }
            
            if (icon_pb && list_icon_pb && this->current_thumbnail_load_id.load() == load_id) {
                this->update_item_thumbnail(file_path, icon_pb, list_icon_pb, load_id);
            }
        }
    }).detach();
}

struct ThumbUpdateData {
    FileView* self;
    std::string file_path;
    GdkPixbuf* icon_pb;
    GdkPixbuf* list_icon_pb;
    int load_id;
};

void FileView::update_item_thumbnail(const std::string& file_path, GdkPixbuf* icon_pb, GdkPixbuf* list_icon_pb, int load_id) {
    ThumbUpdateData* data = new ThumbUpdateData{this, file_path, icon_pb, list_icon_pb, load_id};
    g_idle_add([](gpointer user_data) -> gboolean {
        ThumbUpdateData* d = static_cast<ThumbUpdateData*>(user_data);
        if (d->self->current_thumbnail_load_id.load() == d->load_id) {
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
        
        if (selected_paths.size() == 1) {
            std::string path = selected_paths[0];
            struct stat st;
            if (stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) {
                GtkWidget* item_open_with = gtk_menu_item_new_with_label(i18n::_("open_with").c_str());
                GtkWidget* open_with_menu = gtk_menu_new();
                gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_open_with), open_with_menu);
                
                std::string mime = utils::get_mime_type(path);
                if (!mime.empty()) {
                    GList* apps = g_app_info_get_all_for_type(mime.c_str());
                    std::vector<std::string> seen_names;
                    for (GList* l = apps; l != NULL; l = l->next) {
                        GAppInfo* app = G_APP_INFO(l->data);
                        const char* name = g_app_info_get_name(app);
                        if (name) {
                            std::string name_s(name);
                            if (std::find(seen_names.begin(), seen_names.end(), name_s) == seen_names.end()) {
                                seen_names.push_back(name_s);
                                
                                GtkWidget* app_item = gtk_menu_item_new_with_label(name);
                                
                                struct LaunchData {
                                    GAppInfo* app;
                                    std::string path;
                                };
                                g_object_ref(app);
                                LaunchData* ld = new LaunchData{app, path};
                                
                                g_signal_connect_data(app_item, "activate", G_CALLBACK(+[](GtkWidget* w, gpointer d) {
                                    LaunchData* ldata = static_cast<LaunchData*>(d);
                                    GFile* gfile = g_file_new_for_path(ldata->path.c_str());
                                    GList* files = NULL;
                                    files = g_list_append(files, gfile);
                                    g_app_info_launch(ldata->app, files, NULL, NULL);
                                    g_list_free(files);
                                    g_object_unref(gfile);
                                }), ld, [](gpointer d, GClosure*) {
                                    LaunchData* ldata = static_cast<LaunchData*>(d);
                                    g_object_unref(ldata->app);
                                    delete ldata;
                                }, G_CONNECT_AFTER);
                                
                                gtk_menu_shell_append(GTK_MENU_SHELL(open_with_menu), app_item);
                            }
                        }
                    }
                    g_list_free_full(apps, g_object_unref);
                }
                
                GList* children = gtk_container_get_children(GTK_CONTAINER(open_with_menu));
                if (children) {
                    GtkWidget* sep = gtk_separator_menu_item_new();
                    gtk_menu_shell_append(GTK_MENU_SHELL(open_with_menu), sep);
                    g_list_free(children);
                }
                
                GtkWidget* other_app_item = gtk_menu_item_new_with_label(i18n::_("other_application").c_str());
                struct OtherAppData {
                    FileView* self;
                    std::string path;
                };
                OtherAppData* oad = new OtherAppData{this, path};
                g_signal_connect_data(other_app_item, "activate", G_CALLBACK(+[](GtkWidget* w, gpointer d) {
                    OtherAppData* odata = static_cast<OtherAppData*>(d);
                    odata->self->handle_other_app({odata->path});
                }), oad, [](gpointer d, GClosure*) { delete static_cast<OtherAppData*>(d); }, G_CONNECT_AFTER);
                
                gtk_menu_shell_append(GTK_MENU_SHELL(open_with_menu), other_app_item);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_open_with);
            }
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
    gtk_widget_set_sensitive(item_paste, !parent->clipboard_files.empty());
    g_signal_connect_swapped(item_paste, "activate", G_CALLBACK(+[](FileView* self) {
        self->handle_paste();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_paste);
    
    if (is_item) {
        GtkWidget* sep2 = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep2);
        
        GtkWidget* item_rename = gtk_menu_item_new_with_label(i18n::_("rename").c_str());
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
            
            struct stat st;
            if (stat(selected_paths[0].c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
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
    }
    
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
}

void FileView::handle_open(const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                on_dir_activated(path);
            } else {
                utils::open_file(path);
            }
        }
    }
}

void FileView::handle_other_app(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    std::string path = paths[0];
    
    GFile* gfile = g_file_new_for_path(path.c_str());
    GtkWidget* dialog = gtk_app_chooser_dialog_new(GTK_WINDOW(parent->get_window()), GTK_DIALOG_MODAL, gfile);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        GAppInfo* app_info = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(dialog));
        if (app_info) {
            GList* files = NULL;
            files = g_list_append(files, gfile);
            g_app_info_launch(app_info, files, NULL, NULL);
            g_list_free(files);
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
    struct stat st;
    bool is_dir = false;
    std::string size_str = "Calculating...";
    std::string modified = "N/A";
    std::string owner = "unknown";
    std::string group = "unknown";
    std::string permissions = "";
    
    if (stat(path.c_str(), &st) == 0) {
        is_dir = S_ISDIR(st.st_mode);
        if (!is_dir) {
            size_str = utils::format_size(st.st_size) + " (" + std::to_string(st.st_size) + " bytes)";
        }
        
        struct tm* timeinfo = localtime(&st.st_mtime);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        modified = buffer;
        
        struct passwd* pw = getpwuid(st.st_uid);
        if (pw) owner = pw->pw_name;
        
        struct group* gr = getgrgid(st.st_gid);
        if (gr) group = gr->gr_name;
        
        char perm_buf[32];
        std::snprintf(perm_buf, sizeof(perm_buf), "%o", st.st_mode & 0777);
        permissions = perm_buf;
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
    if (is_dir && lbl_size_value) {
        // Increment reference count of label so it stays alive during async thread
        g_object_ref(lbl_size_value);
        std::thread([path, lbl_size_value]() {
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
            
            scan(path);
            
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
    parent->clipboard_files = paths;
    parent->clipboard_action = "cut";
}

void FileView::handle_copy(const std::vector<std::string>& paths) {
    parent->clipboard_files = paths;
    parent->clipboard_action = "copy";
}

void FileView::handle_paste() {
    parent->start_paste_operation(parent->clipboard_files, current_dir, parent->clipboard_action);
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
            std::string new_path = utils::get_parent_directory(old_path) + "/" + new_name;
            if (std::rename(old_path.c_str(), new_path.c_str()) == 0) {
                parent->load_directory(current_dir, false);
            }
        }
    }
    gtk_widget_destroy(dialog);
}

void FileView::handle_trash(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    
    for (const auto& path : paths) {
        GFile* gfile = g_file_new_for_path(path.c_str());
        g_file_trash(gfile, NULL, NULL);
        g_object_unref(gfile);
    }
    parent->load_directory(current_dir, false);
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
        for (const auto& path : paths) {
            utils::delete_path_recursive(path);
        }
        parent->load_directory(current_dir, false);
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
            parent->start_compress_operation(paths, current_dir + "/" + archive_name, format);
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
    while (access((current_dir + "/" + name).c_str(), F_OK) == 0) {
        counter++;
        name = base_name + " " + std::to_string(counter);
    }

    std::string folder_path = current_dir + "/" + name;
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
    while (access((current_dir + "/" + name).c_str(), F_OK) == 0) {
        counter++;
        name = base_name + " " + std::to_string(counter);
    }

    std::string file_path = current_dir + "/" + name;
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
