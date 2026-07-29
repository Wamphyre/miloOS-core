#include "sidebar.hpp"
#include "utils.hpp"
#include "i18n.hpp"
#include <iostream>
#include <thread>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include <cctype>

namespace {

constexpr guint FAVORITE_DRAG_TARGET = 1;

GtkTargetEntry* favorite_drag_targets() {
    static GtkTargetEntry targets[] = {
        {
            const_cast<gchar*>("application/x-milofiles-favorite"),
            GTK_TARGET_SAME_APP,
            FAVORITE_DRAG_TARGET
        }
    };
    return targets;
}

void clear_favorite_drop_indicator(GtkListBox* listbox) {
    for (gint index = 0;; ++index) {
        GtkListBoxRow* row =
            gtk_list_box_get_row_at_index(listbox, index);
        if (!row) break;

        GtkStyleContext* context =
            gtk_widget_get_style_context(GTK_WIDGET(row));
        gtk_style_context_remove_class(
            context,
            "favorite-drop-before");
        gtk_style_context_remove_class(
            context,
            "favorite-drop-after");
    }
}

GtkListBoxRow* favorite_drop_target(
    GtkListBox* listbox,
    gint y,
    bool* after) {
    GtkListBoxRow* row =
        gtk_list_box_get_row_at_y(listbox, y);
    if (row) {
        const char* favorite_uri = static_cast<const char*>(
            g_object_get_data(G_OBJECT(row), "favorite-uri"));
        if (favorite_uri) {
            GtkAllocation allocation;
            gtk_widget_get_allocation(GTK_WIDGET(row), &allocation);
            *after = y >= allocation.y + allocation.height / 2;
            return row;
        }

        const gint row_index = gtk_list_box_row_get_index(row);
        for (gint index = row_index - 1; index >= 0; --index) {
            GtkListBoxRow* previous_row =
                gtk_list_box_get_row_at_index(listbox, index);
            const char* previous_uri = static_cast<const char*>(
                g_object_get_data(
                    G_OBJECT(previous_row),
                    "favorite-uri"));
            if (previous_uri) {
                *after = true;
                return previous_row;
            }
        }
    }

    GtkListBoxRow* last_favorite = nullptr;
    for (gint index = 0;; ++index) {
        GtkListBoxRow* candidate =
            gtk_list_box_get_row_at_index(listbox, index);
        if (!candidate) break;
        if (g_object_get_data(
                G_OBJECT(candidate),
                "favorite-uri")) {
            last_favorite = candidate;
        }
    }
    *after = true;
    return last_favorite;
}

void update_favorite_drop_indicator(GtkListBox* listbox, gint y) {
    clear_favorite_drop_indicator(listbox);
    bool after = false;
    GtkListBoxRow* row =
        favorite_drop_target(listbox, y, &after);
    if (!row) return;

    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(row)),
        after ? "favorite-drop-after" : "favorite-drop-before");
}

}

static std::string sidebar_icon_for_path(const std::string& path) {
    if (utils::has_uri_scheme(path) && path.rfind("file://", 0) != 0) {
        return "folder-remote";
    }

    std::string basename = utils::get_filename(path);
    std::transform(basename.begin(), basename.end(), basename.begin(), [](unsigned char ch) {
        return std::tolower(ch);
    });

    if (basename == "desktop") return "user-desktop";
    if (basename == "documents") return "folder-documents";
    if (basename == "downloads") return "folder-download";
    if (basename == "music") return "folder-music";
    if (basename == "pictures") return "folder-pictures";
    if (basename == "videos") return "folder-videos";
    return "folder";
}

Sidebar::Sidebar(GtkWindow* parent_window, std::function<void(const std::string&)> on_dir_changed_cb)
    : parent(parent_window), on_dir_changed(on_dir_changed_cb),
      alive(std::make_shared<std::atomic<bool>>(true)) {
    
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window), GTK_SHADOW_NONE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, 200, -1);
    
    GtkStyleContext* context = gtk_widget_get_style_context(scrolled_window);
    gtk_style_context_add_class(context, "sidebar-scroll");
    
    sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(sidebar_box, 12);
    gtk_widget_set_margin_bottom(sidebar_box, 12);
    gtk_container_add(GTK_CONTAINER(scrolled_window), sidebar_box);
    
    icon_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    
    volume_monitor = g_volume_monitor_get();
    g_signal_connect(volume_monitor, "mount-added", G_CALLBACK(on_volume_monitor_changed), this);
    g_signal_connect(volume_monitor, "mount-removed", G_CALLBACK(on_volume_monitor_changed), this);
    g_signal_connect(volume_monitor, "volume-added", G_CALLBACK(on_volume_monitor_changed), this);
    g_signal_connect(volume_monitor, "volume-removed", G_CALLBACK(on_volume_monitor_changed), this);
    
    setup_sidebar();
    gtk_widget_show_all(scrolled_window);
}

Sidebar::~Sidebar() {
    alive->store(false);
    g_signal_handlers_disconnect_by_data(volume_monitor, this);
    g_object_unref(volume_monitor);
    g_object_unref(icon_group);
}

GtkWidget* Sidebar::get_widget() {
    return scrolled_window;
}

void Sidebar::reload() {
    // Clear sidebar_box children
    GList* children = gtk_container_get_children(GTK_CONTAINER(sidebar_box));
    for (GList* l = children; l != NULL; l = l->next) {
        gtk_container_remove(GTK_CONTAINER(sidebar_box), GTK_WIDGET(l->data));
    }
    g_list_free(children);
    listboxes.clear();
    
    setup_sidebar();
    gtk_widget_show_all(sidebar_box);
}

void Sidebar::setup_sidebar() {
    // 1. DEVICES
    std::vector<std::pair<std::string, std::string>> devices;
    std::vector<std::string> device_icons;
    std::vector<GVolume*> device_volumes;
    
    // Home Folder
    std::string home_label = g_get_user_name();
    if (!home_label.empty()) {
        home_label[0] = std::toupper(home_label[0]);
    }
    devices.push_back({home_label, g_get_home_dir()});
    device_icons.push_back("user-home");
    device_volumes.push_back(nullptr);
    
    // Root Filesystem
    devices.push_back({i18n::_("file_system"), "/"});
    device_icons.push_back("drive-harddisk");
    device_volumes.push_back(nullptr);
    
    // Connected Volumes/Disks
    GList* volumes = g_volume_monitor_get_volumes(volume_monitor);
    for (GList* l = volumes; l != NULL; l = l->next) {
        GVolume* vol = G_VOLUME(l->data);
        char* name = g_volume_get_name(vol);
        if (!name) continue;
        
        std::string icon_name = "drive-harddisk";
        GIcon* icon = g_volume_get_icon(vol);
        if (icon) {
            char* icon_str = g_icon_to_string(icon);
            if (icon_str) {
                std::string icon_s(icon_str);
                size_t space = icon_s.find_last_of(' ');
                if (space != std::string::npos) {
                    icon_name = icon_s.substr(space + 1);
                } else {
                    icon_name = icon_s;
                }
                g_free(icon_str);
            }
            g_object_unref(icon);
        }
        
        GMount* mount = g_volume_get_mount(vol);
        if (mount) {
            GFile* loc = g_mount_get_default_location(mount);
            std::string location = utils::location_from_gfile(loc);
            if (!location.empty() && location != g_get_home_dir() && location != "/") {
                devices.push_back({name, location});
                device_icons.push_back(icon_name);
                device_volumes.push_back(nullptr);
            }
            g_object_unref(loc);
            g_object_unref(mount);
        } else {
            devices.push_back({name, ""});
            device_icons.push_back(icon_name);
            g_object_ref(vol); // Keep ref for our sidebar
            device_volumes.push_back(vol);
        }
        g_free(name);
    }
    g_list_free_full(volumes, g_object_unref);
    
    // Dynamic Mounts without volumes
    GList* mounts = g_volume_monitor_get_mounts(volume_monitor);
    for (GList* l = mounts; l != NULL; l = l->next) {
        GMount* mount = G_MOUNT(l->data);
        GVolume* vol = g_mount_get_volume(mount);
        if (vol) {
            g_object_unref(vol);
            continue; // Already handled by volume list
        }
        
        GFile* loc = g_mount_get_default_location(mount);
        std::string location = utils::location_from_gfile(loc);
        if (location.empty()) {
            GFile* root = g_mount_get_root(mount);
            location = utils::location_from_gfile(root);
            g_object_unref(root);
        }
        
        if (!location.empty()) {
            if (location != g_get_home_dir() && location != "/") {
                char* name = g_mount_get_name(mount);
                std::string icon_name = "folder-remote";
                GIcon* icon = g_mount_get_icon(mount);
                if (icon) {
                    char* icon_str = g_icon_to_string(icon);
                    if (icon_str) {
                        std::string icon_s(icon_str);
                        size_t space = icon_s.find_last_of(' ');
                        if (space != std::string::npos) {
                            icon_name = icon_s.substr(space + 1);
                        } else {
                            icon_name = icon_s;
                        }
                        g_free(icon_str);
                    }
                    g_object_unref(icon);
                }
                devices.push_back({name ? name : "Remote Mount", location});
                device_icons.push_back(icon_name);
                device_volumes.push_back(nullptr);
                if (name) g_free(name);
            }
        }
        g_object_unref(loc);
    }
    g_list_free_full(mounts, g_object_unref);
    
    add_sidebar_section(i18n::_("devices"), devices, device_icons, device_volumes);
    
    // 2. FAVORITES
    std::vector<std::pair<std::string, std::string>> favorites;
    std::vector<std::string> fav_icons;
    std::vector<GVolume*> fav_volumes;
    
    std::vector<utils::FavoriteItem> fav_items = utils::get_favorites();
    for (const auto& item : fav_items) {
        GFile* gfile = g_file_new_for_uri(item.uri.c_str());
        char* path = g_file_get_path(gfile);
        std::string location;
        if (path) {
            location = path;
            g_free(path);
        } else {
            location = item.uri;
        }
        if (!location.empty()) {
            favorites.push_back({item.label, location});
            fav_icons.push_back(sidebar_icon_for_path(location));
            fav_volumes.push_back(nullptr);
        }
        g_object_unref(gfile);
    }
    
    // Add Trash
    std::string trash_path = std::string(g_get_home_dir()) + "/.local/share/Trash/files";
    favorites.push_back({i18n::_("trash"), trash_path});
    fav_icons.push_back("user-trash");
    fav_volumes.push_back(nullptr);
    
    add_sidebar_section(i18n::_("favorites"), favorites, fav_icons, fav_volumes);
}

void Sidebar::add_sidebar_section(const std::string& section_title, 
                                 const std::vector<std::pair<std::string, std::string>>& items, 
                                 const std::vector<std::string>& icons, 
                                 const std::vector<GVolume*>& volumes) {
    GtkWidget* lbl = gtk_label_new(NULL);
    std::string markup = "<span size='8500' weight='bold' color='#7f8c8d'>" + section_title + "</span>";
    gtk_label_set_markup(GTK_LABEL(lbl), markup.c_str());
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_start(lbl, 16);
    gtk_widget_set_margin_top(lbl, 8);
    gtk_widget_set_margin_bottom(lbl, 2);
    gtk_box_pack_start(GTK_BOX(sidebar_box), lbl, FALSE, FALSE, 0);
    
    GtkWidget* listbox = gtk_list_box_new();
    GtkStyleContext* context = gtk_widget_get_style_context(listbox);
    gtk_style_context_add_class(context, "sidebar-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_SINGLE);
    
    for (size_t i = 0; i < items.size(); ++i) {
        std::string name = items[i].first;
        std::string path = items[i].second;
        std::string icon_name = icons[i];
        GVolume* volume = volumes[i];
        
        // Skip directory if it doesn't exist (unless it's an unmounted volume or Trash)
        if (!path.empty() && name != i18n::_("trash")) {
            if (!utils::has_uri_scheme(path) && !utils::is_directory(path)) {
                if (!volume) continue;
            }
        }
        
        GtkWidget* row = gtk_list_box_row_new();
        GtkStyleContext* row_context = gtk_widget_get_style_context(row);
        gtk_style_context_add_class(row_context, "sidebar-row");
        
        // Associate path and volume data with the row
        if (!path.empty()) {
            g_object_set_data_full(G_OBJECT(row), "path", g_strdup(path.c_str()), g_free);
        }
        if (volume) {
            g_object_ref(volume);
            g_object_set_data_full(G_OBJECT(row), "volume", volume, g_object_unref);
        }

        const bool reorderable_favorite =
            section_title == i18n::_("favorites") &&
            name != i18n::_("trash") &&
            !path.empty();
        if (reorderable_favorite) {
            const std::string favorite_uri = utils::location_to_uri(path);
            g_object_set_data_full(
                G_OBJECT(row),
                "favorite-uri",
                g_strdup(favorite_uri.c_str()),
                g_free);
        }
        
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_start(box, 12);
        gtk_widget_set_margin_end(box, 12);
        gtk_widget_set_margin_top(box, 4);
        gtk_widget_set_margin_bottom(box, 4);
        
        GtkWidget* img = gtk_image_new_from_icon_name(icon_name.c_str(), GTK_ICON_SIZE_MENU);
        GtkStyleContext* img_context = gtk_widget_get_style_context(img);
        gtk_style_context_add_class(img_context, "sidebar-icon");
        gtk_size_group_add_widget(icon_group, img);
        gtk_box_pack_start(GTK_BOX(box), img, FALSE, FALSE, 0);
        
        GtkWidget* name_lbl = gtk_label_new(name.c_str());
        gtk_widget_set_halign(name_lbl, GTK_ALIGN_START);
        gtk_widget_set_valign(name_lbl, GTK_ALIGN_CENTER);
        GtkStyleContext* lbl_context = gtk_widget_get_style_context(name_lbl);
        gtk_style_context_add_class(lbl_context, "sidebar-label");
        gtk_box_pack_start(GTK_BOX(box), name_lbl, TRUE, TRUE, 0);

        if (reorderable_favorite) {
            GtkWidget* drag_handle = gtk_event_box_new();
            gtk_event_box_set_visible_window(GTK_EVENT_BOX(drag_handle), FALSE);
            gtk_widget_set_tooltip_text(
                drag_handle,
                i18n::_("drag_to_reorder").c_str());
            gtk_widget_set_size_request(drag_handle, 24, 24);

            GtkWidget* drag_icon = gtk_image_new_from_icon_name(
                "open-menu-symbolic",
                GTK_ICON_SIZE_MENU);
            gtk_widget_set_opacity(drag_icon, 0.55);
            gtk_container_add(GTK_CONTAINER(drag_handle), drag_icon);

            const char* favorite_uri = static_cast<const char*>(
                g_object_get_data(G_OBJECT(row), "favorite-uri"));
            g_object_set_data_full(
                G_OBJECT(drag_handle),
                "favorite-uri",
                g_strdup(favorite_uri),
                g_free);
            gtk_drag_source_set(
                drag_handle,
                GDK_BUTTON1_MASK,
                favorite_drag_targets(),
                1,
                GDK_ACTION_MOVE);
            g_signal_connect(
                drag_handle,
                "drag-data-get",
                G_CALLBACK(+[](
                    GtkWidget* source,
                    GdkDragContext*,
                    GtkSelectionData* selection,
                    guint,
                    guint,
                    gpointer) {
                    const char* uri = static_cast<const char*>(
                        g_object_get_data(G_OBJECT(source), "favorite-uri"));
                    if (!uri) return;
                    gtk_selection_data_set(
                        selection,
                        gtk_selection_data_get_target(selection),
                        8,
                        reinterpret_cast<const guchar*>(uri),
                        std::strlen(uri) + 1);
                }),
                nullptr);
            gtk_box_pack_end(
                GTK_BOX(box),
                drag_handle,
                FALSE,
                FALSE,
                0);
        }
        
        // Eject button for dynamic mounts
        if (section_title == i18n::_("devices") && !path.empty() && 
            path != g_get_home_dir() && path != "/" && name != i18n::_("trash")) {
            GtkWidget* eject_btn = gtk_button_new();
            gtk_button_set_relief(GTK_BUTTON(eject_btn), GTK_RELIEF_NONE);
            GtkWidget* eject_img = gtk_image_new_from_icon_name("media-eject-symbolic", GTK_ICON_SIZE_MENU);
            gtk_button_set_image(GTK_BUTTON(eject_btn), eject_img);
            
            GtkStyleContext* btn_context = gtk_widget_get_style_context(eject_btn);
            gtk_style_context_add_class(btn_context, "sidebar-eject-btn");
            
            g_object_set_data_full(G_OBJECT(eject_btn), "path", g_strdup(path.c_str()), g_free);
            g_signal_connect(eject_btn, "clicked", G_CALLBACK(on_eject_clicked), this);
            
            gtk_box_pack_end(GTK_BOX(box), eject_btn, FALSE, FALSE, 0);
        }
        
        gtk_container_add(GTK_CONTAINER(row), box);
        gtk_container_add(GTK_CONTAINER(listbox), row);
    }

    if (section_title == i18n::_("favorites")) {
        gtk_drag_dest_set(
            listbox,
            GTK_DEST_DEFAULT_DROP,
            favorite_drag_targets(),
            1,
            GDK_ACTION_MOVE);
        g_signal_connect(
            listbox,
            "drag-motion",
            G_CALLBACK(+[](
                GtkWidget* target,
                GdkDragContext* context,
                gint,
                gint y,
                guint time,
                gpointer) -> gboolean {
                update_favorite_drop_indicator(
                    GTK_LIST_BOX(target),
                    y);
                gdk_drag_status(context, GDK_ACTION_MOVE, time);
                return TRUE;
            }),
            nullptr);
        g_signal_connect(
            listbox,
            "drag-leave",
            G_CALLBACK(+[](
                GtkWidget* target,
                GdkDragContext*,
                guint,
                gpointer) {
                clear_favorite_drop_indicator(
                    GTK_LIST_BOX(target));
            }),
            nullptr);
        g_signal_connect(
            listbox,
            "drag-data-received",
            G_CALLBACK(+[](
                GtkWidget* target,
                GdkDragContext* context,
                gint,
                gint y,
                GtkSelectionData* selection,
                guint,
                guint time,
                gpointer user_data) {
                auto* sidebar = static_cast<Sidebar*>(user_data);
                const guchar* selection_data =
                    gtk_selection_data_get_data(selection);
                const gint selection_length =
                    gtk_selection_data_get_length(selection);
                bool reordered = false;

                if (selection_data && selection_length > 1) {
                    const std::string source_uri(
                        reinterpret_cast<const char*>(selection_data),
                        strnlen(
                            reinterpret_cast<const char*>(selection_data),
                            selection_length));
                    GtkListBox* listbox = GTK_LIST_BOX(target);
                    bool after = false;
                    GtkListBoxRow* target_row =
                        favorite_drop_target(listbox, y, &after);

                    if (target_row) {
                        const char* target_uri = static_cast<const char*>(
                            g_object_get_data(
                                G_OBJECT(target_row),
                                "favorite-uri"));
                        if (target_uri) {
                            reordered = utils::reorder_favorite(
                                source_uri,
                                target_uri,
                                after);
                        }
                    }
                }

                clear_favorite_drop_indicator(GTK_LIST_BOX(target));
                gtk_drag_finish(context, reordered, FALSE, time);
                if (reordered) {
                    g_idle_add(
                        +[](gpointer data) -> gboolean {
                            static_cast<Sidebar*>(data)->reload();
                            return G_SOURCE_REMOVE;
                        },
                        sidebar);
                }
            }),
            this);
    }
    
    g_signal_connect(listbox, "row-activated", G_CALLBACK(on_row_activated), this);
    g_object_set_data_full(G_OBJECT(listbox), "section_title", g_strdup(section_title.c_str()), g_free);
    g_signal_connect(listbox, "button-press-event", G_CALLBACK(on_button_press), this);
    
    gtk_box_pack_start(GTK_BOX(sidebar_box), listbox, FALSE, FALSE, 0);
    listboxes.push_back(listbox);
}

void Sidebar::on_row_activated(GtkListBox* listbox, GtkListBoxRow* row, gpointer user_data) {
    Sidebar* self = static_cast<Sidebar*>(user_data);
    const char* path = (const char*)g_object_get_data(G_OBJECT(row), "path");
    GVolume* volume = (GVolume*)g_object_get_data(G_OBJECT(row), "volume");
    
    if (path && std::strlen(path) > 0) {
        self->on_dir_changed(path);
    } else if (volume) {
        self->handle_mount_volume(volume);
    }
}

void Sidebar::on_eject_clicked(GtkWidget* button, gpointer user_data) {
    Sidebar* self = static_cast<Sidebar*>(user_data);
    const char* path = (const char*)g_object_get_data(G_OBJECT(button), "path");
    if (path) {
        self->handle_unmount_volume(path);
    }
}

void Sidebar::on_volume_monitor_changed(GVolumeMonitor* monitor, gpointer arg1, Sidebar* self) {
    self->reload();
}

gboolean Sidebar::on_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data) {
    if (event->type != GDK_BUTTON_PRESS || event->button != 3) {
        return FALSE; // Only right clicks
    }
    
    Sidebar* self = static_cast<Sidebar*>(user_data);
    GtkListBox* listbox = GTK_LIST_BOX(widget);
    GtkListBoxRow* row = gtk_list_box_get_row_at_y(listbox, event->y);
    if (!row) return FALSE;
    
    gtk_list_box_select_row(listbox, row);
    
    const char* section_title = (const char*)g_object_get_data(G_OBJECT(listbox), "section_title");
    const char* path = (const char*)g_object_get_data(G_OBJECT(row), "path");
    GVolume* volume = (GVolume*)g_object_get_data(G_OBJECT(row), "volume");
    
    // Retrieve the display label
    std::string name = "";
    GList* children = gtk_container_get_children(GTK_CONTAINER(row));
    if (children) {
        GtkWidget* box = GTK_WIDGET(children->data);
        GList* box_children = gtk_container_get_children(GTK_CONTAINER(box));
        for (GList* bc = box_children; bc != NULL; bc = bc->next) {
            GtkWidget* child = GTK_WIDGET(bc->data);
            if (GTK_IS_LABEL(child)) {
                name = gtk_label_get_text(GTK_LABEL(child));
                break;
            }
        }
        g_list_free(box_children);
        g_list_free(children);
    }
    
    GtkWidget* menu = gtk_menu_new();
    
    if (name == i18n::_("trash")) {
        GtkWidget* item_empty = gtk_menu_item_new_with_label(i18n::_("empty_trash").c_str());
        g_signal_connect_swapped(item_empty, "activate", G_CALLBACK(+[](Sidebar* s) {
            s->handle_empty_trash();
        }), self);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_empty);
    } 
    else if (section_title && std::string(section_title) == i18n::_("favorites")) {
        GtkWidget* item_rename = gtk_menu_item_new_with_label(i18n::_("rename_favorite").c_str());
        struct RenameData {
            Sidebar* self;
            GtkListBoxRow* row;
            std::string name;
            std::string path;
        };
        RenameData* data = new RenameData{self, row, name, path ? path : ""};
        g_signal_connect_data(item_rename, "activate", G_CALLBACK(+[](GtkWidget* w, gpointer d) {
            RenameData* rd = static_cast<RenameData*>(d);
            rd->self->handle_rename_favorite(rd->row, rd->name, rd->path);
        }), data, [](gpointer d, GClosure*) { delete static_cast<RenameData*>(d); }, G_CONNECT_AFTER);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_rename);
        
        GtkWidget* item_remove = gtk_menu_item_new_with_label(i18n::_("remove_favorite").c_str());
        g_object_set_data_full(G_OBJECT(item_remove), "path", g_strdup(path ? path : ""), g_free);
        g_signal_connect(item_remove, "activate", G_CALLBACK(+[](GtkWidget* item, gpointer data) {
            Sidebar* s = static_cast<Sidebar*>(data);
            const char* p = (const char*)g_object_get_data(G_OBJECT(item), "path");
            if (p) s->handle_remove_favorite(p);
        }), self);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_remove);
    } 
    else if (section_title && std::string(section_title) == i18n::_("favorites")) {
        // This block is actually unreachable here because section_title is devices in the next block,
        // wait, let's keep the logic correct.
    }
    else if (section_title && std::string(section_title) == i18n::_("devices")) {
        if (volume && !path) {
            GtkWidget* item_mount = gtk_menu_item_new_with_label(i18n::_("mount_volume").c_str());
            g_object_ref(volume);
            g_object_set_data_full(G_OBJECT(item_mount), "volume", volume, g_object_unref);
            g_signal_connect(item_mount, "activate", G_CALLBACK(+[](GtkWidget* item, gpointer data) {
                Sidebar* s = static_cast<Sidebar*>(data);
                GVolume* v = (GVolume*)g_object_get_data(G_OBJECT(item), "volume");
                if (v) s->handle_mount_volume(v);
            }), self);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_mount);
        } 
        else if (path && std::string(path) != g_get_home_dir() && std::string(path) != "/") {
            GtkWidget* item_fav = gtk_menu_item_new_with_label(i18n::_("add_to_favorites").c_str());
            struct FavoriteData {
                Sidebar* self;
                std::string path;
                std::string name;
            };
            FavoriteData* fav_data = new FavoriteData{self, path, name};
            g_signal_connect_data(item_fav, "activate", G_CALLBACK(+[](GtkWidget*, gpointer d) {
                FavoriteData* fd = static_cast<FavoriteData*>(d);
                fd->self->handle_add_favorite(fd->path, fd->name);
            }), fav_data, [](gpointer d, GClosure*) { delete static_cast<FavoriteData*>(d); }, G_CONNECT_AFTER);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_fav);

            GtkWidget* item_unmount = gtk_menu_item_new_with_label(i18n::_("unmount_volume").c_str());
            g_object_set_data_full(G_OBJECT(item_unmount), "path", g_strdup(path), g_free);
            g_signal_connect(item_unmount, "activate", G_CALLBACK(+[](GtkWidget* item, gpointer data) {
                Sidebar* s = static_cast<Sidebar*>(data);
                const char* p = (const char*)g_object_get_data(G_OBJECT(item), "path");
                if (p) s->handle_unmount_volume(p);
            }), self);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_unmount);
        } 
        else {
            gtk_widget_destroy(menu);
            return FALSE;
        }
    } 
    else {
        gtk_widget_destroy(menu);
        return FALSE;
    }
    
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
    return TRUE;
}

void Sidebar::handle_empty_trash() {
    GtkWidget* dialog = gtk_message_dialog_new(parent,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_YES_NO,
                                               "%s", i18n::_("confirm_empty_trash").c_str());
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        utils::empty_trash();
        reload();
    }
}

void Sidebar::handle_rename_favorite(GtkListBoxRow* row, const std::string& name, const std::string& path) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(i18n::_("rename_favorite").c_str(),
                                                    parent,
                                                    GTK_DIALOG_MODAL,
                                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                    i18n::_("apply").c_str(), GTK_RESPONSE_OK,
                                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(box), 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    
    GtkWidget* lbl = gtk_label_new(i18n::_("enter_new_name").c_str());
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), name.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_OK) {
        std::string new_name = gtk_entry_get_text(GTK_ENTRY(entry));
        // strip whitespace
        new_name.erase(new_name.begin(), std::find_if(new_name.begin(), new_name.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        new_name.erase(std::find_if(new_name.rbegin(), new_name.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), new_name.end());
        
        if (!new_name.empty() && new_name != name) {
            std::string uri = utils::location_to_uri(path);
            if (!uri.empty()) {
                utils::rename_favorite(uri, new_name);
            }
            reload();
        }
    }
    gtk_widget_destroy(dialog);
}

void Sidebar::handle_remove_favorite(const std::string& path) {
    std::string uri = utils::location_to_uri(path);
    if (!uri.empty()) {
        utils::remove_favorite(uri);
    }
    reload();
}

void Sidebar::handle_add_favorite(const std::string& path, const std::string& name) {
    if (!path.empty() && utils::add_favorite(path, name)) {
        reload();
    }
}

void Sidebar::handle_mount_volume(GVolume* volume) {
    char* dev_path = g_volume_get_identifier(volume, "unix-device");
    if (!dev_path) return;
    
    std::string dev_str(dev_path);
    g_free(dev_path);
    
    g_object_ref(volume);
    std::weak_ptr<std::atomic<bool>> sidebar_alive = alive;
    
    std::thread([this, sidebar_alive, dev_str, volume]() {
        std::string cmd = "udisksctl mount -b " + dev_str;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buffer[256];
            std::string result = "";
            while (!feof(pipe)) {
                if (fgets(buffer, 256, pipe) != NULL) {
                    result += buffer;
                }
            }
            pclose(pipe);
            
            size_t pos = result.find(" at ");
            if (pos != std::string::npos) {
                std::string mount_path = result.substr(pos + 4);
                while (!mount_path.empty() && (std::isspace(mount_path.back()) || mount_path.back() == '.')) {
                    mount_path.pop_back();
                }
                
                struct NavigateData {
                    Sidebar* self;
                    std::weak_ptr<std::atomic<bool>> alive;
                    std::string path;
                };
                NavigateData* data = new NavigateData{this, sidebar_alive, mount_path};
                g_idle_add([](gpointer user_data) -> gboolean {
                    NavigateData* d = static_cast<NavigateData*>(user_data);
                    auto alive_ref = d->alive.lock();
                    if (alive_ref && alive_ref->load()) {
                        d->self->on_dir_changed(d->path);
                        d->self->reload();
                    }
                    delete d;
                    return FALSE;
                }, data);
            }
        }
        g_object_unref(volume);
    }).detach();
}

void Sidebar::handle_unmount_volume(const std::string& path) {
    std::weak_ptr<std::atomic<bool>> sidebar_alive = alive;
    std::thread([this, sidebar_alive, path]() {
        std::vector<std::string> argv = {"gio", "mount", "-u", path};
        gchar** spawn_argv = g_new0(gchar*, argv.size() + 1);
        for (size_t i = 0; i < argv.size(); ++i) {
            spawn_argv[i] = g_strdup(argv[i].c_str());
        }
        g_spawn_sync(NULL, spawn_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL);
        for (size_t i = 0; i < argv.size(); ++i) {
            g_free(spawn_argv[i]);
        }
        g_free(spawn_argv);
        
        struct ReloadData {
            Sidebar* self;
            std::weak_ptr<std::atomic<bool>> alive;
        };
        g_idle_add([](gpointer user_data) -> gboolean {
            auto* data = static_cast<ReloadData*>(user_data);
            auto alive_ref = data->alive.lock();
            if (alive_ref && alive_ref->load()) data->self->reload();
            delete data;
            return FALSE;
        }, new ReloadData{this, sidebar_alive});
    }).detach();
}

void Sidebar::select_path(const std::string& path) {
    for (GtkWidget* listbox : listboxes) {
        // Temporarily block activated signals
        g_signal_handlers_block_by_func(listbox, (gpointer)on_row_activated, this);
        gtk_list_box_unselect_all(GTK_LIST_BOX(listbox));
        
        GList* children = gtk_container_get_children(GTK_CONTAINER(listbox));
        for (GList* l = children; l != NULL; l = l->next) {
            GtkListBoxRow* row = GTK_LIST_BOX_ROW(l->data);
            const char* rpath = (const char*)g_object_get_data(G_OBJECT(row), "path");
            if (rpath) {
                if (utils::same_location(rpath, path)) {
                    gtk_list_box_select_row(GTK_LIST_BOX(listbox), row);
                }
            }
        }
        g_list_free(children);
        
        g_signal_handlers_unblock_by_func(listbox, (gpointer)on_row_activated, this);
    }
}
