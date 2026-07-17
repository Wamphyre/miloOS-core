#ifndef SIDEBAR_HPP
#define SIDEBAR_HPP

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>

class Sidebar {
public:
    Sidebar(GtkWindow* parent_window, std::function<void(const std::string&)> on_dir_changed_cb);
    ~Sidebar();

    GtkWidget* get_widget();
    void reload();
    void select_path(const std::string& path);

private:
    GtkWidget* scrolled_window;
    GtkWidget* sidebar_box;
    GtkWindow* parent;
    GtkSizeGroup* icon_group;
    GVolumeMonitor* volume_monitor;
    std::function<void(const std::string&)> on_dir_changed;
    std::vector<GtkWidget*> listboxes;
    std::shared_ptr<std::atomic<bool>> alive;

    void setup_sidebar();
    void add_sidebar_section(const std::string& section_title, 
                             const std::vector<std::pair<std::string, std::string>>& items, 
                             const std::vector<std::string>& icons, 
                             const std::vector<GVolume*>& volumes);

    // Callbacks
    static void on_row_activated(GtkListBox* listbox, GtkListBoxRow* row, gpointer user_data);
    static gboolean on_button_press(GtkWidget* widget, GdkEventButton* event, gpointer user_data);
    static void on_eject_clicked(GtkWidget* button, gpointer user_data);
    static void on_volume_monitor_changed(GVolumeMonitor* monitor, gpointer user_data, Sidebar* self);

    // Context Menu Actions
    void handle_empty_trash();
    void handle_rename_favorite(GtkListBoxRow* row, const std::string& name, const std::string& path);
    void handle_remove_favorite(const std::string& path);
    void handle_add_favorite(const std::string& path, const std::string& name);
    void handle_mount_volume(GVolume* volume);
    void handle_unmount_volume(const std::string& path);
};

#endif // SIDEBAR_HPP
