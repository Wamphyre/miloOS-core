#include "app_window.hpp"
#include "utils.hpp"
#include "i18n.hpp"
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <stdexcept>
#include <filesystem>

static std::atomic<int> open_window_count{0};

struct AppWindow::OperationControl {
    std::atomic<bool> cancelled{false};
    GCancellable* cancellable = g_cancellable_new();

    ~OperationControl() {
        g_object_unref(cancellable);
    }

    void cancel() {
        cancelled.store(true);
        g_cancellable_cancel(cancellable);
    }
};

class ScopedTempDirectory {
public:
    explicit ScopedTempDirectory(const char* pattern) {
        GError* error = nullptr;
        char* created = g_dir_make_tmp(pattern, &error);
        if (!created) {
            std::string message = error ? error->message : "Could not create a temporary directory.";
            if (error) g_error_free(error);
            throw std::runtime_error(message);
        }
        path_ = created;
        g_free(created);
    }

    ~ScopedTempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

static std::string summarize_operation_locations(const std::vector<std::string>& paths) {
    std::string message;
    const size_t limit = 6;
    for (size_t i = 0; i < paths.size() && i < limit; ++i) {
        const std::string name = utils::get_filename(paths[i]);
        message += "\n- " + (name.empty() ? paths[i] : name);
    }
    if (paths.size() > limit) message += "\n- ...";
    return message;
}

static bool run_process_with_cancellation(const std::vector<std::string>& args, 
                                          const std::string& working_dir, 
                                          std::atomic<GPid>& active_gpid, 
                                          std::atomic<bool>& cancelled,
                                          std::string* error_message = nullptr) {
    if (cancelled.load()) return false;
    
    gchar** argv = g_new0(gchar*, args.size() + 1);
    for (size_t i = 0; i < args.size(); ++i) {
        argv[i] = g_strdup(args[i].c_str());
    }
    
    GPid child_pid = 0;
    GError* error = NULL;
    gboolean ok = g_spawn_async_with_pipes(
        working_dir.empty() ? NULL : working_dir.c_str(),
        argv,
        NULL,
        (GSpawnFlags)(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH),
        NULL, NULL,
        &child_pid,
        NULL, NULL, NULL,
        &error
    );
    
    for (size_t i = 0; i < args.size(); ++i) {
        g_free(argv[i]);
    }
    g_free(argv);
    
    if (!ok) {
        if (error) {
            if (error_message) *error_message = error->message;
            g_error_free(error);
        } else if (error_message) {
            *error_message = "Failed to start process.";
        }
        return false;
    }
    
    active_gpid.store(child_pid);
    
    if (cancelled.load()) {
        kill(child_pid, SIGTERM);
    }
    
    int status = 0;
    while (true) {
        pid_t done = waitpid(child_pid, &status, WNOHANG);
        if (done == child_pid) {
            break;
        }
        if (done == -1) {
            status = -1;
            break;
        }
        if (cancelled.load()) {
            kill(child_pid, SIGTERM);
            waitpid(child_pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    g_spawn_close_pid(child_pid);
    
    active_gpid.store(0);
    
    return !cancelled.load() && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

AppWindow::AppWindow(const std::string& initial_dir, bool delete_on_destroy)
    : history_index(-1), show_hidden(false),
      window_alive(std::make_shared<std::atomic<bool>>(true)),
      delete_on_destroy(delete_on_destroy),
      path_stack(nullptr), path_entry(nullptr), path_completion_model(nullptr), hidden_mitem(nullptr) {
    open_window_count.fetch_add(1);
    
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(window, "milofiles-window");
    gtk_window_set_title(GTK_WINDOW(window), i18n::_("title").c_str());
    gtk_window_set_default_size(GTK_WINDOW(window), 950, 620);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name(GTK_WINDOW(window), "milofiles");
    
    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), main_box);
    
    setup_menu_bar();
    setup_toolbar();
    
    body_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), body_paned, TRUE, TRUE, 0);
    gtk_paned_set_position(GTK_PANED(body_paned), 220);
    
    // Sidebar
    sidebar = std::make_unique<Sidebar>(
        GTK_WINDOW(window),
        [this](const std::string& path) {
            this->navigate_from_location_text(path);
        }
    );
    
    gtk_paned_pack1(GTK_PANED(body_paned), sidebar->get_widget(), FALSE, FALSE);
    
    // FileView
    file_view = std::make_unique<FileView>(
        this,
        [this](const std::string& path) { this->load_directory(path); },
        [this](const std::string& status) { this->update_statusbar(status); }
    );
    // Wrap file view in a card frame
    GtkWidget* file_card = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(file_card), GTK_SHADOW_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(file_card), "file-card");
    gtk_container_add(GTK_CONTAINER(file_card), file_view->get_widget());
    gtk_paned_pack2(GTK_PANED(body_paned), file_card, TRUE, FALSE);
    
    setup_statusbar();
    
    apply_theme_styling();
    GtkSettings* settings = gtk_settings_get_default();
    g_signal_connect(settings, "notify::gtk-theme-name", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
        static_cast<AppWindow*>(data)->apply_theme_styling();
    }), this);
    
    std::string start_path = initial_dir;
    if (start_path.empty()) {
        start_path = g_get_home_dir();
    }
    load_directory(start_path);
    
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), this);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), this);
}

AppWindow::~AppWindow() {
    window_alive->store(false);
    if (operation_control) operation_control->cancel();
    if (path_completion_model) {
        g_object_unref(path_completion_model);
        path_completion_model = nullptr;
    }
}

void AppWindow::show() {
    gtk_widget_show_all(window);
}

GtkWindow* AppWindow::get_window() {
    return GTK_WINDOW(window);
}

void AppWindow::show_error_dialog(const std::string& title, const std::string& msg) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_OK,
                                               "%s", msg.c_str());
    gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void AppWindow::setup_menu_bar() {
    GtkWidget* menu_bar = gtk_menu_bar_new();
    gtk_box_pack_start(GTK_BOX(main_box), menu_bar, FALSE, FALSE, 0);
    
    // File Menu
    GtkWidget* file_menu = gtk_menu_new();
    GtkWidget* file_mitem = gtk_menu_item_new_with_label(i18n::_("file_menu").c_str());
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_mitem), file_menu);

    GtkWidget* new_window_mitem = gtk_menu_item_new_with_label(i18n::_("new_window").c_str());
    g_signal_connect_swapped(new_window_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        std::string start_dir = self->file_view ? self->file_view->get_current_dir() : std::string();
        auto* new_window = new AppWindow(start_dir, true);
        new_window->show();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), new_window_mitem);

    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    
    GtkWidget* new_folder_mitem = gtk_menu_item_new_with_label(i18n::_("new_folder").c_str());
    g_signal_connect_swapped(new_folder_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->handle_new_folder();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), new_folder_mitem);
    
    GtkWidget* new_file_mitem = gtk_menu_item_new_with_label(i18n::_("new_file").c_str());
    g_signal_connect_swapped(new_file_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->handle_new_file();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), new_file_mitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), gtk_separator_menu_item_new());
    
    GtkWidget* close_mitem = gtk_menu_item_new_with_label(i18n::_("close").c_str());
    g_signal_connect_swapped(close_mitem, "activate", G_CALLBACK(gtk_widget_destroy), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), close_mitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), file_mitem);
    
    // Edit Menu
    GtkWidget* edit_menu = gtk_menu_new();
    GtkWidget* edit_mitem = gtk_menu_item_new_with_label(i18n::_("edit_menu").c_str());
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(edit_mitem), edit_menu);
    
    GtkWidget* cut_mitem = gtk_menu_item_new_with_label(i18n::_("cut").c_str());
    g_signal_connect_swapped(cut_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->handle_cut(self->file_view->get_selected_paths());
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), cut_mitem);
    
    GtkWidget* copy_mitem = gtk_menu_item_new_with_label(i18n::_("copy").c_str());
    g_signal_connect_swapped(copy_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->handle_copy(self->file_view->get_selected_paths());
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), copy_mitem);
    
    GtkWidget* paste_mitem = gtk_menu_item_new_with_label(i18n::_("paste").c_str());
    g_signal_connect_swapped(paste_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->handle_paste();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), paste_mitem);

    GtkWidget* select_all_mitem = gtk_menu_item_new_with_label(i18n::_("select_all").c_str());
    g_signal_connect_swapped(select_all_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->select_all();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), select_all_mitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), gtk_separator_menu_item_new());
    
    GtkWidget* rename_mitem = gtk_menu_item_new_with_label(i18n::_("rename").c_str());
    g_signal_connect_swapped(rename_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->handle_rename(self->file_view->get_selected_paths());
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), rename_mitem);
    
    GtkWidget* trash_mitem = gtk_menu_item_new_with_label(i18n::_("trash_action").c_str());
    g_signal_connect_swapped(trash_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->handle_trash(self->file_view->get_selected_paths());
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), trash_mitem);
    
    GtkWidget* delete_mitem = gtk_menu_item_new_with_label(i18n::_("delete").c_str());
    g_signal_connect_swapped(delete_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->handle_delete(self->file_view->get_selected_paths());
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), delete_mitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), gtk_separator_menu_item_new());
    
    GtkWidget* compress_mitem = gtk_menu_item_new_with_label(i18n::_("compress").c_str());
    g_signal_connect_swapped(compress_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->handle_compress(self->file_view->get_selected_paths());
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), compress_mitem);
    
    GtkWidget* extract_mitem = gtk_menu_item_new_with_label(i18n::_("extract_here").c_str());
    g_signal_connect_swapped(extract_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->handle_extract(self->file_view->get_selected_paths());
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu), extract_mitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), edit_mitem);
    
    // View Menu
    GtkWidget* view_menu = gtk_menu_new();
    GtkWidget* view_mitem = gtk_menu_item_new_with_label(i18n::_("view_menu").c_str());
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(view_mitem), view_menu);
    
    GtkWidget* icon_view_mitem = gtk_menu_item_new_with_label(i18n::_("view_icon").c_str());
    g_signal_connect_swapped(icon_view_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->set_view_mode("icon");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->icon_view_btn), TRUE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->list_view_btn), FALSE);
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), icon_view_mitem);
    
    GtkWidget* list_view_mitem = gtk_menu_item_new_with_label(i18n::_("view_list").c_str());
    g_signal_connect_swapped(list_view_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->file_view->set_view_mode("list");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->list_view_btn), TRUE);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->icon_view_btn), FALSE);
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), list_view_mitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), gtk_separator_menu_item_new());
    
    hidden_mitem = gtk_check_menu_item_new_with_label(i18n::_("show_hidden_menu").c_str());
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(hidden_mitem), show_hidden);
    g_signal_connect_swapped(hidden_mitem, "toggled", G_CALLBACK(+[](AppWindow* self) {
        self->show_hidden = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(self->hidden_mitem));
        if (self->file_view) {
            self->load_directory(self->file_view->get_current_dir(), false);
        }
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(view_menu), hidden_mitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), view_mitem);
    
    // Go Menu
    GtkWidget* go_menu = gtk_menu_new();
    GtkWidget* go_mitem = gtk_menu_item_new_with_label(i18n::_("go_menu").c_str());
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(go_mitem), go_menu);
    
    GtkWidget* back_mitem = gtk_menu_item_new_with_label(i18n::_("go_back").c_str());
    g_signal_connect_swapped(back_mitem, "activate", G_CALLBACK(on_back_clicked), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(go_menu), back_mitem);
    
    GtkWidget* forward_mitem = gtk_menu_item_new_with_label(i18n::_("go_forward").c_str());
    g_signal_connect_swapped(forward_mitem, "activate", G_CALLBACK(on_forward_clicked), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(go_menu), forward_mitem);
    
    GtkWidget* up_mitem = gtk_menu_item_new_with_label(i18n::_("go_up").c_str());
    g_signal_connect_swapped(up_mitem, "activate", G_CALLBACK(on_up_clicked), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(go_menu), up_mitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(go_menu), gtk_separator_menu_item_new());
    
    GtkWidget* go_location_mitem = gtk_menu_item_new_with_label(i18n::_("go_location").c_str());
    g_signal_connect_swapped(go_location_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->show_path_entry();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(go_menu), go_location_mitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(go_menu), gtk_separator_menu_item_new());
    
    GtkWidget* connect_mitem = gtk_menu_item_new_with_label(i18n::_("connect_to_server").c_str());
    g_signal_connect_swapped(connect_mitem, "activate", G_CALLBACK(+[](AppWindow* self) {
        self->show_connect_server_dialog();
    }), this);
    gtk_menu_shell_append(GTK_MENU_SHELL(go_menu), connect_mitem);
    
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), go_mitem);
}

void AppWindow::setup_toolbar() {
    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "main-toolbar");
    gtk_box_pack_start(GTK_BOX(main_box), toolbar, FALSE, FALSE, 0);
    
    // Back Button
    back_btn = gtk_button_new_from_icon_name("go-previous-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_sensitive(back_btn, FALSE);
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_back_clicked), this);
    gtk_box_pack_start(GTK_BOX(toolbar), back_btn, FALSE, FALSE, 0);
    
    // Forward Button
    forward_btn = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_sensitive(forward_btn, FALSE);
    g_signal_connect(forward_btn, "clicked", G_CALLBACK(on_forward_clicked), this);
    gtk_box_pack_start(GTK_BOX(toolbar), forward_btn, FALSE, FALSE, 0);
    
    // Up Button
    up_btn = gtk_button_new_from_icon_name("go-up-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(up_btn, "clicked", G_CALLBACK(on_up_clicked), this);
    gtk_box_pack_start(GTK_BOX(toolbar), up_btn, FALSE, FALSE, 0);
    
    // Path breadcrumb bar + editable entry stack
    path_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(path_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(path_stack), 150);

    path_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget* path_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(path_scrolled), "path-scroll");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(path_scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_add_events(path_scrolled, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(path_scrolled, "button-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer data) -> gboolean {
        static_cast<AppWindow*>(data)->show_path_entry();
        return TRUE;
    }), this);
    gtk_container_add(GTK_CONTAINER(path_scrolled), path_box);

    path_entry = gtk_entry_new();
    gtk_widget_set_hexpand(path_entry, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(path_entry), "path-entry");
    gtk_entry_set_activates_default(GTK_ENTRY(path_entry), TRUE);
    g_signal_connect(path_entry, "activate", G_CALLBACK(+[](GtkEntry* entry, gpointer data) {
        AppWindow* self = static_cast<AppWindow*>(data);
        self->navigate_from_location_text(gtk_entry_get_text(entry));
        self->show_path_breadcrumbs();
    }), this);
    g_signal_connect(path_entry, "key-press-event", G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
        AppWindow* self = static_cast<AppWindow*>(data);
        if (event->keyval == GDK_KEY_Escape) {
            self->show_path_breadcrumbs();
            return TRUE;
        }
        return FALSE;
    }), this);
    g_signal_connect(path_entry, "focus-out-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
        static_cast<AppWindow*>(data)->show_path_breadcrumbs();
        return FALSE;
    }), this);

    GtkEntryCompletion* completion = gtk_entry_completion_new();
    path_completion_model = gtk_list_store_new(1, G_TYPE_STRING);
    gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(path_completion_model));
    gtk_entry_completion_set_text_column(completion, 0);
    gtk_entry_completion_set_inline_completion(completion, TRUE);
    gtk_entry_completion_set_popup_completion(completion, TRUE);
    gtk_entry_set_completion(GTK_ENTRY(path_entry), completion);
    g_object_unref(completion);
    update_path_recommendations();

    gtk_stack_add_named(GTK_STACK(path_stack), path_scrolled, "buttons");
    gtk_stack_add_named(GTK_STACK(path_stack), path_entry, "entry");
    gtk_stack_set_visible_child_name(GTK_STACK(path_stack), "buttons");
    gtk_box_pack_start(GTK_BOX(toolbar), path_stack, TRUE, TRUE, 6);
    
    // Search Entry
    search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), (i18n::_("search") + "...").c_str());
    gtk_entry_set_width_chars(GTK_ENTRY(search_entry), 18);
    gtk_style_context_add_class(gtk_widget_get_style_context(search_entry), "search-entry");
    g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_changed), this);
    gtk_box_pack_start(GTK_BOX(toolbar), search_entry, FALSE, FALSE, 0);
    
    // View Mode Toggle (Segmented control style)
    GtkWidget* view_switch_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(view_switch_box), "segmented-control");
    
    icon_view_btn = gtk_toggle_button_new();
    GtkWidget* icon_view_img = gtk_image_new_from_icon_name("view-grid-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_image(GTK_BUTTON(icon_view_btn), icon_view_img);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(icon_view_btn), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_view_btn), "segmented-btn");
    g_signal_connect(icon_view_btn, "toggled", G_CALLBACK(on_view_mode_changed), this);
    gtk_box_pack_start(GTK_BOX(view_switch_box), icon_view_btn, FALSE, FALSE, 0);
    
    list_view_btn = gtk_toggle_button_new();
    GtkWidget* list_view_img = gtk_image_new_from_icon_name("view-list-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_image(GTK_BUTTON(list_view_btn), list_view_img);
    gtk_style_context_add_class(gtk_widget_get_style_context(list_view_btn), "segmented-btn");
    g_signal_connect(list_view_btn, "toggled", G_CALLBACK(on_view_mode_changed), this);
    gtk_box_pack_start(GTK_BOX(view_switch_box), list_view_btn, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(toolbar), view_switch_box, FALSE, FALSE, 0);
}

void AppWindow::setup_statusbar() {
    statusbar = gtk_statusbar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(statusbar), "statusbar");
    gtk_box_pack_start(GTK_BOX(main_box), statusbar, FALSE, FALSE, 0);
    statusbar_context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(statusbar), "miloFiles");
}

void AppWindow::update_statusbar(const std::string& custom_text) {
    gtk_statusbar_pop(GTK_STATUSBAR(statusbar), statusbar_context_id);
    
    std::string text = custom_text;
    std::string free_space = utils::get_free_space_description(file_view->get_current_dir());
    if (!free_space.empty()) {
        if (!text.empty()) text += " | ";
        text += free_space + " " + i18n::_("free");
    }
    
    gtk_statusbar_push(GTK_STATUSBAR(statusbar), statusbar_context_id, text.c_str());
}

void AppWindow::apply_theme_styling() {
    GtkSettings* settings = gtk_settings_get_default();
    gchar* theme_name = nullptr;
    g_object_get(settings, "gtk-theme-name", &theme_name, NULL);
    bool is_dark = (theme_name && std::string(theme_name) == "miloOS-Dark");
    if (theme_name) g_free(theme_name);
    
    std::string bg_window, border_window, bg_sidebar, border_sidebar, bg_card, border_card;
    std::string text_color, text_muted, bg_toolbar, border_toolbar, bg_selected, text_selected;
    std::string bg_segmented, bg_row_hover, bg_row_selected;

    if (is_dark) {
        bg_window = "#18181a";
        border_window = "#2d2d30";
        bg_sidebar = "#1f1f21";
        border_sidebar = "#2a2a2c";
        bg_card = "#242426";
        border_card = "#2d2d30";
        text_color = "#f5f6fa";
        text_muted = "#a0a0a2";
        bg_toolbar = "linear-gradient(to bottom, #2d2d30, #1c1c1e)";
        border_toolbar = "#141416";
        bg_selected = "#007AFF";
        text_selected = "#ffffff";
        bg_segmented = "#2d2d30";
        bg_row_hover = "#2d2d30";
        bg_row_selected = "#007AFF";
    } else {
        bg_window = "#f1f2f6";
        border_window = "#c8c8cc";
        bg_sidebar = "#eef0f4";
        border_sidebar = "#dcdde1";
        bg_card = "#ffffff";
        border_card = "#e3e4e9";
        text_color = "#2c3e50";
        text_muted = "#7f8c8d";
        bg_toolbar = "linear-gradient(to bottom, #f6f6f8, #e5e5e9)";
        border_toolbar = "#c8c8cc";
        bg_selected = "#007AFF";
        text_selected = "#ffffff";
        bg_segmented = "#e3e4e9";
        bg_row_hover = "#f1f2f6";
        bg_row_selected = "#007AFF";
    }

    std::string css = 
        "window#milofiles-window,\n"
        "window#milofiles-window.background,\n"
        "window#milofiles-window.ssd,\n"
        "window#milofiles-window.ssd.background,\n"
        "window#milofiles-window .background,\n"
        "window#milofiles-window.backdrop,\n"
        "window#milofiles-window.backdrop.background {\n"
        "    border-style: none;\n"
        "    border-width: 0px;\n"
        "    border-color: transparent;\n"
        "    box-shadow: none;\n"
        "    background-color: " + bg_window + ";\n"
        "}\n"
        "window#milofiles-window decoration,\n"
        "window#milofiles-window decoration:backdrop,\n"
        "window#milofiles-window.ssd decoration,\n"
        "window#milofiles-window.csd decoration,\n"
        "window#milofiles-window.background decoration,\n"
        "window#milofiles-window.ssd.background decoration,\n"
        "window#milofiles-window.csd.background decoration,\n"
        "window#milofiles-window.backdrop decoration,\n"
        "window#milofiles-window decoration * {\n"
        "    background-color: transparent;\n"
        "    border-style: none;\n"
        "    border-width: 0px;\n"
        "    border-color: transparent;\n"
        "    box-shadow: none;\n"
        "    padding: 0px;\n"
        "    margin: 0px;\n"
        "}\n"
        "window#milofiles-window menubar,\n"
        "window#milofiles-window menubar * {\n"
        "    border-style: none;\n"
        "    border-width: 0px;\n"
        "    border-color: transparent;\n"
        "    box-shadow: none;\n"
        "    background-color: transparent;\n"
        "    margin: 0px;\n"
        "    padding: 0px;\n"
        "    min-height: 0px;\n"
        "}\n"
        "window#milofiles-window scrolledwindow,\n"
        "window#milofiles-window viewport,\n"
        "window#milofiles-window treeview,\n"
        "window#milofiles-window iconview,\n"
        "window#milofiles-window frame,\n"
        "window#milofiles-window .frame,\n"
        "window#milofiles-window viewport.frame,\n"
        "window#milofiles-window paned {\n"
        "    border-style: none;\n"
        "    border-width: 0px;\n"
        "    border-color: transparent;\n"
        "    box-shadow: none;\n"
        "    background-color: transparent;\n"
        "}\n"
        "window#milofiles-window treeview button,\n"
        "window#milofiles-window treeview button:hover,\n"
        "window#milofiles-window treeview button:backdrop {\n"
        "    background-color: " + bg_card + ";\n"
        "    background-image: none;\n"
        "    border-style: solid;\n"
        "    border-width: 0px 1px 1px 0px;\n"
        "    border-color: " + border_card + ";\n"
        "    color: " + text_color + ";\n"
        "    font-weight: bold;\n"
        "    text-shadow: none;\n"
        "    box-shadow: none;\n"
        "    padding: 6px;\n"
        "}\n"
        "window#milofiles-window paned > separator {\n"
        "    background-color: " + border_window + ";\n"
        "    border-style: none;\n"
        "    border-width: 0px;\n"
        "    background-image: none;\n"
        "}\n"
        "window#milofiles-window .main-toolbar {\n"
        "    background-image: " + bg_toolbar + ";\n"
        "    border-style: solid;\n"
        "    border-width: 0px 0px 1px 0px;\n"
        "    border-color: " + border_toolbar + ";\n"
        "    padding: 6px 12px;\n"
        "}\n"
        "window#milofiles-window .sidebar-scroll {\n"
        "    background-color: " + bg_sidebar + ";\n"
        "    border-style: solid;\n"
        "    border-width: 0px 1px 0px 0px;\n"
        "    border-color: " + border_sidebar + ";\n"
        "    box-shadow: none;\n"
        "}\n"
        "window#milofiles-window .sidebar-list {\n"
        "    background-color: transparent;\n"
        "}\n"
        "window#milofiles-window .sidebar-row {\n"
        "    background-color: transparent;\n"
        "    padding: 4px 8px;\n"
        "    border-radius: 6px;\n"
        "    color: " + text_color + ";\n"
        "}\n"
        "window#milofiles-window .sidebar-row:hover {\n"
        "    background-color: " + bg_row_hover + ";\n"
        "}\n"
        "window#milofiles-window .sidebar-row:selected {\n"
        "    background-color: " + bg_row_selected + ";\n"
        "    color: " + text_selected + ";\n"
        "}\n"
        "window#milofiles-window .sidebar-label {\n"
        "    font-size: 13px;\n"
        "    font-weight: 500;\n"
        "}\n"
        "window#milofiles-window .sidebar-eject-btn {\n"
        "    background: transparent;\n"
        "    border-style: none;\n"
        "    box-shadow: none;\n"
        "    padding: 0;\n"
        "    margin: 0;\n"
        "}\n"
        "window#milofiles-window .sidebar-eject-btn:hover {\n"
        "    color: #ff3b30;\n"
        "}\n"
        "window#milofiles-window .file-card {\n"
        "    background-color: " + bg_card + ";\n"
        "    border-radius: 12px;\n"
        "    border-style: solid;\n"
        "    border-width: 1px;\n"
        "    border-color: " + border_card + ";\n"
        "    margin: 12px;\n"
        "    box-shadow: none;\n"
        "}\n"
        "window#milofiles-window .file-view {\n"
        "    background-color: transparent;\n"
        "    color: " + text_color + ";\n"
        "}\n"
        "window#milofiles-window .file-view:selected {\n"
        "    background-color: " + bg_selected + ";\n"
        "    color: " + text_selected + ";\n"
        "}\n"
        "window#milofiles-window .path-scroll {\n"
        "    background-color: " + bg_card + ";\n"
        "    border-radius: 6px;\n"
        "    border-style: solid;\n"
        "    border-width: 1px;\n"
        "    border-color: " + border_card + ";\n"
        "    padding: 2px 8px;\n"
        "}\n"
        "window#milofiles-window .path-entry {\n"
        "    background-color: " + bg_card + ";\n"
        "    border-radius: 6px;\n"
        "    border-style: solid;\n"
        "    border-width: 1px;\n"
        "    border-color: " + border_card + ";\n"
        "    padding: 4px 8px;\n"
        "    color: " + text_color + ";\n"
        "}\n"
        "window#milofiles-window .path-btn {\n"
        "    background: transparent;\n"
        "    border-style: none;\n"
        "    box-shadow: none;\n"
        "    color: " + text_color + ";\n"
        "    font-size: 12px;\n"
        "    padding: 2px 4px;\n"
        "}\n"
        "window#milofiles-window .path-btn:hover {\n"
        "    color: #007AFF;\n"
        "    background-color: " + bg_row_hover + ";\n"
        "    border-radius: 4px;\n"
        "}\n"
        "window#milofiles-window .path-separator {\n"
        "    color: " + text_muted + ";\n"
        "    font-size: 10px;\n"
        "}\n"
        "window#milofiles-window .segmented-control {\n"
        "    background-color: " + bg_segmented + ";\n"
        "    border-radius: 6px;\n"
        "    padding: 2px;\n"
        "}\n"
        "window#milofiles-window .segmented-btn {\n"
        "    background-color: transparent;\n"
        "    border-style: none;\n"
        "    box-shadow: none;\n"
        "    color: " + text_color + ";\n"
        "    padding: 4px 8px;\n"
        "    border-radius: 4px;\n"
        "}\n"
        "window#milofiles-window .segmented-btn:checked {\n"
        "    background-color: " + bg_card + ";\n"
        "    color: " + text_color + ";\n"
        "    box-shadow: 0 1px 3px rgba(0,0,0,0.12), 0 1px 2px rgba(0,0,0,0.08);\n"
        "}\n"
        "window#milofiles-window .search-entry {\n"
        "    border-radius: 6px;\n"
        "    padding: 4px 8px;\n"
        "    font-size: 13px;\n"
        "}\n"
        "window#milofiles-window .statusbar {\n"
        "    background-color: " + bg_sidebar + ";\n"
        "    border-style: solid;\n"
        "    border-width: 1px 0px 0px 0px;\n"
        "    border-color: " + border_sidebar + ";\n"
        "    padding: 4px 12px;\n"
        "    font-size: 11px;\n"
        "    color: " + text_muted + ";\n"
        "}\n"
        ".milo-breadcrumb { border-radius: 4px; padding: 4px 8px; }\n"
        ".dim-label { opacity: 0.65; }\n";

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css.c_str(), -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), 
                                             GTK_STYLE_PROVIDER(provider), 
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void AppWindow::load_directory(const std::string& path, bool add_to_history) {
    std::string norm_path = utils::normalize_path(path);
    if (norm_path.empty()) norm_path = g_get_home_dir();

    if (!utils::is_directory(norm_path)) {
        norm_path = g_get_home_dir();
    }
    
    if (add_to_history) {
        // Truncate history forward if we had navigated back
        if (history_index >= 0 && history_index < (int)history.size() - 1) {
            history.erase(history.begin() + history_index + 1, history.end());
        }
        history.push_back(norm_path);
        history_index = history.size() - 1;
    }
    
    gtk_widget_set_sensitive(back_btn, history_index > 0);
    gtk_widget_set_sensitive(forward_btn, history_index < (int)history.size() - 1);
    gtk_widget_set_sensitive(up_btn, norm_path != "/");
    
    file_view->load_directory(norm_path, show_hidden, current_search_query);
    
    update_breadcrumbs();
    select_sidebar_path(norm_path);
    update_path_recommendations();
}

void AppWindow::update_breadcrumbs() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(path_box));
    for (GList* l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
    
    std::string path = file_view->get_current_dir();

    if (utils::has_uri_scheme(path) && path.rfind("file://", 0) != 0) {
        GtkWidget* btn = gtk_button_new_with_label(path.c_str());
        gtk_widget_set_focus_on_click(btn, FALSE);
        GtkStyleContext* context = gtk_widget_get_style_context(btn);
        gtk_style_context_add_class(context, "flat");
        gtk_style_context_add_class(context, "path-btn");
        gtk_box_pack_start(GTK_BOX(path_box), btn, FALSE, FALSE, 0);
        gtk_widget_show_all(path_box);
        return;
    }

    std::vector<std::string> parts;
    
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    
    // Root button
    {
        GtkWidget* btn = gtk_button_new_with_label("/");
        gtk_widget_set_focus_on_click(btn, FALSE);
        GtkStyleContext* context = gtk_widget_get_style_context(btn);
        gtk_style_context_add_class(context, "flat");
        gtk_style_context_add_class(context, "path-btn");
        
        g_signal_connect_swapped(btn, "clicked", G_CALLBACK(+[](AppWindow* self) {
            self->load_directory("/");
        }), this);
        
        gtk_box_pack_start(GTK_BOX(path_box), btn, FALSE, FALSE, 0);
    }
    
    std::string accumulated = "";
    for (size_t i = 0; i < parts.size(); ++i) {
        GtkWidget* sep = gtk_label_new("›");
        GtkStyleContext* s_context = gtk_widget_get_style_context(sep);
        gtk_style_context_add_class(s_context, "path-separator");
        gtk_box_pack_start(GTK_BOX(path_box), sep, FALSE, FALSE, 4);
        
        accumulated += "/" + parts[i];
        
        GtkWidget* btn = gtk_button_new_with_label(parts[i].c_str());
        gtk_widget_set_focus_on_click(btn, FALSE);
        GtkStyleContext* context = gtk_widget_get_style_context(btn);
        gtk_style_context_add_class(context, "flat");
        gtk_style_context_add_class(context, "path-btn");
        
        struct ClickData {
            AppWindow* self;
            std::string target;
        };
        ClickData* cd = new ClickData{this, accumulated};
        g_signal_connect_data(btn, "clicked", G_CALLBACK(+[](GtkWidget* w, gpointer d) {
            ClickData* data = static_cast<ClickData*>(d);
            data->self->load_directory(data->target);
        }), cd, [](gpointer d, GClosure*) { delete static_cast<ClickData*>(d); }, G_CONNECT_AFTER);
        
        gtk_box_pack_start(GTK_BOX(path_box), btn, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(path_box);
}

void AppWindow::update_path_recommendations() {
    if (!path_completion_model) return;

    gtk_list_store_clear(path_completion_model);
    std::vector<std::string> paths = {
        g_get_home_dir(),
        "/",
        std::string(g_get_home_dir()) + "/.local/share/Trash/files"
    };

    const GUserDirectory special_dirs[] = {
        G_USER_DIRECTORY_DESKTOP,
        G_USER_DIRECTORY_DOCUMENTS,
        G_USER_DIRECTORY_DOWNLOAD,
        G_USER_DIRECTORY_MUSIC,
        G_USER_DIRECTORY_PICTURES,
        G_USER_DIRECTORY_VIDEOS
    };

    for (GUserDirectory dir : special_dirs) {
        const char* special = g_get_user_special_dir(dir);
        if (special) paths.push_back(special);
    }

    std::vector<std::string> seen;
    for (const auto& p : paths) {
        if (p.empty() || access(p.c_str(), F_OK) != 0) continue;
        if (std::find(seen.begin(), seen.end(), p) != seen.end()) continue;
        seen.push_back(p);

        GtkTreeIter iter;
        gtk_list_store_append(path_completion_model, &iter);
        gtk_list_store_set(path_completion_model, &iter, 0, p.c_str(), -1);
    }
}

void AppWindow::show_path_entry() {
    if (!path_stack || !path_entry || !file_view) return;

    gtk_entry_set_text(GTK_ENTRY(path_entry), file_view->get_current_dir().c_str());
    gtk_stack_set_visible_child_name(GTK_STACK(path_stack), "entry");
    gtk_widget_grab_focus(path_entry);
    gtk_editable_select_region(GTK_EDITABLE(path_entry), 0, -1);
}

void AppWindow::show_path_breadcrumbs() {
    if (!path_stack) return;
    gtk_stack_set_visible_child_name(GTK_STACK(path_stack), "buttons");
}

void AppWindow::navigate_from_location_text(const std::string& text) {
    std::string value = text;
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());

    if (value.empty()) return;

    bool has_scheme = value.find("://") != std::string::npos && value.rfind("file://", 0) != 0;
    if (has_scheme) {
        mount_network_share(value);
    } else {
        load_directory(value);
    }
}

void AppWindow::select_sidebar_path(const std::string& path) {
    sidebar->select_path(path);
}

void AppWindow::reload_sidebar() {
    sidebar->reload();
}

void AppWindow::on_back_clicked(GtkWidget* widget, gpointer data) {
    AppWindow* self = static_cast<AppWindow*>(data);
    if (self->history_index > 0) {
        self->history_index--;
        self->load_directory(self->history[self->history_index], false);
    }
}

void AppWindow::on_forward_clicked(GtkWidget* widget, gpointer data) {
    AppWindow* self = static_cast<AppWindow*>(data);
    if (self->history_index < (int)self->history.size() - 1) {
        self->history_index++;
        self->load_directory(self->history[self->history_index], false);
    }
}

void AppWindow::on_up_clicked(GtkWidget* widget, gpointer data) {
    AppWindow* self = static_cast<AppWindow*>(data);
    std::string parent_dir = utils::get_parent_directory(self->file_view->get_current_dir());
    if (!parent_dir.empty()) {
        self->load_directory(parent_dir);
    }
}

void AppWindow::on_search_changed(GtkSearchEntry* entry, gpointer data) {
    AppWindow* self = static_cast<AppWindow*>(data);
    self->current_search_query = gtk_entry_get_text(GTK_ENTRY(entry));
    self->load_directory(self->file_view->get_current_dir(), false);
}

void AppWindow::on_view_mode_changed(GtkToggleButton* btn, gpointer data) {
    AppWindow* self = static_cast<AppWindow*>(data);
    
    // Block recursion
    static bool inside = false;
    if (inside) return;
    inside = true;
    
    gboolean active = gtk_toggle_button_get_active(btn);
    if (active) {
        if ((GtkWidget*)btn == self->icon_view_btn) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->list_view_btn), FALSE);
            self->file_view->set_view_mode("icon");
        } else {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->icon_view_btn), FALSE);
            self->file_view->set_view_mode("list");
        }
    } else {
        // Ensure at least one button is always active
        if ((GtkWidget*)btn == self->icon_view_btn) {
            if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->list_view_btn))) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->icon_view_btn), TRUE);
            }
        } else {
            if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->icon_view_btn))) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->list_view_btn), TRUE);
            }
        }
    }
    
    inside = false;
}

void AppWindow::on_window_destroy(GtkWidget* widget, gpointer data) {
    (void)widget;
    AppWindow* self = static_cast<AppWindow*>(data);
    if (self) {
        self->window_alive->store(false);
        if (self->operation_control) self->operation_control->cancel();
    }
    int remaining = open_window_count.fetch_sub(1) - 1;
    if (remaining <= 0) {
        gtk_main_quit();
    }
    if (self && self->delete_on_destroy) {
        delete self;
    }
}

gboolean AppWindow::on_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
    AppWindow* self = static_cast<AppWindow*>(data);
    guint key = gdk_keyval_to_lower(event->keyval);
    guint state = event->state;

    GtkWidget* focused = gtk_window_get_focus(GTK_WINDOW(self->window));
    if (focused && GTK_IS_ENTRY(focused)) {
        if ((state & GDK_CONTROL_MASK) && key == GDK_KEY_l) {
            self->show_path_entry();
            return TRUE;
        }
        if (event->keyval == GDK_KEY_Escape) {
            return FALSE;
        }
        if (event->keyval == GDK_KEY_BackSpace || event->keyval == GDK_KEY_Delete ||
            ((state & GDK_CONTROL_MASK) &&
             (key == GDK_KEY_c || key == GDK_KEY_x || key == GDK_KEY_v || key == GDK_KEY_a || key == GDK_KEY_z))) {
            return FALSE;
        }
    }

    if (event->keyval == GDK_KEY_BackSpace) {
        on_up_clicked(nullptr, self);
        return TRUE;
    }

    if (state & GDK_MOD1_MASK) {
        if (event->keyval == GDK_KEY_Left) {
            on_back_clicked(nullptr, self);
            return TRUE;
        }
        if (event->keyval == GDK_KEY_Right) {
            on_forward_clicked(nullptr, self);
            return TRUE;
        }
    }

    if (state & GDK_CONTROL_MASK) {
        if (key == GDK_KEY_c) {
            self->file_view->handle_copy(self->file_view->get_selected_paths());
            return TRUE;
        }
        if (key == GDK_KEY_x) {
            self->file_view->handle_cut(self->file_view->get_selected_paths());
            return TRUE;
        }
        if (key == GDK_KEY_v) {
            self->file_view->handle_paste();
            return TRUE;
        }
        if (key == GDK_KEY_h) {
            self->toggle_hidden_files();
            return TRUE;
        }
        if (key == GDK_KEY_n) {
            self->file_view->handle_new_folder();
            return TRUE;
        }
        if (key == GDK_KEY_l) {
            self->show_path_entry();
            return TRUE;
        }
        if (key == GDK_KEY_a) {
            self->file_view->select_all();
            return TRUE;
        }
    }

    if (event->keyval == GDK_KEY_Escape) {
        self->file_view->unselect_all();
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Delete) {
        self->file_view->handle_trash(self->file_view->get_selected_paths());
        return TRUE;
    }

    if (event->keyval == GDK_KEY_F2) {
        self->file_view->handle_rename(self->file_view->get_selected_paths());
        return TRUE;
    }

    return FALSE;
}

void AppWindow::toggle_hidden_files() {
    if (hidden_mitem) {
        gboolean active = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(hidden_mitem));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(hidden_mitem), !active);
    } else if (file_view) {
        show_hidden = !show_hidden;
        load_directory(file_view->get_current_dir(), false);
    }
}

void AppWindow::handle_drag_data_received(GtkWidget* widget, GdkDragContext* context, gint x, gint y,
                                          GtkSelectionData* data, guint info, guint time) {
    gchar** uris = gtk_selection_data_get_uris(data);
    if (!uris) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }
    
    std::vector<std::string> src_paths;
    for (int i = 0; uris[i] != NULL; ++i) {
        GFile* gfile = g_file_new_for_uri(uris[i]);
        std::string location = utils::location_from_gfile(gfile);
        if (!location.empty()) {
            src_paths.push_back(location);
        }
        g_object_unref(gfile);
    }
    g_strfreev(uris);
    
    if (src_paths.empty()) {
        gtk_drag_finish(context, FALSE, FALSE, time);
        return;
    }

    std::string dest_dir = file_view->get_drop_destination(widget, x, y);
    GdkDragAction selected_action = gdk_drag_context_get_selected_action(context);
    std::string action = "copy";
    if (selected_action == GDK_ACTION_MOVE) {
        action = "cut";
    }

    if (action == "cut") {
        bool all_sources_already_here = true;
        for (const auto& src : src_paths) {
            std::string source_parent = utils::get_parent_directory(src);
            if (source_parent.empty() || !utils::same_location(source_parent, dest_dir)) {
                all_sources_already_here = false;
                break;
            }
        }
        if (all_sources_already_here) {
            // This is a no-op, not a successful move. Reporting success can
            // make GTK's model drag source remove the row until the next reload.
            gtk_drag_finish(context, FALSE, FALSE, time);
            return;
        }
    }
    
    gtk_drag_finish(context, TRUE, FALSE, time);
    start_paste_operation(src_paths, dest_dir, action);
}

struct ClipboardData {
    std::vector<std::string> uris;
    std::string action;
};

static void clipboard_get_func(GtkClipboard*, GtkSelectionData* selection_data, guint info, gpointer user_data) {
    ClipboardData* data = static_cast<ClipboardData*>(user_data);
    if (!data) return;

    if (info == 0) {
        std::string payload = data->action.empty() ? "copy" : data->action;
        for (const auto& uri : data->uris) {
            payload += "\n" + uri;
        }
        gtk_selection_data_set(
            selection_data,
            gdk_atom_intern_static_string("x-special/gnome-copied-files"),
            8,
            reinterpret_cast<const guchar*>(payload.c_str()),
            static_cast<gint>(payload.size()));
        return;
    }

    gchar** uri_array = g_new0(gchar*, data->uris.size() + 1);
    for (size_t i = 0; i < data->uris.size(); ++i) {
        uri_array[i] = g_strdup(data->uris[i].c_str());
    }
    gtk_selection_data_set_uris(selection_data, uri_array);
    g_strfreev(uri_array);
}

static void clipboard_clear_func(GtkClipboard*, gpointer user_data) {
    delete static_cast<ClipboardData*>(user_data);
}

void AppWindow::set_clipboard_files(const std::vector<std::string>& locations, const std::string& action) {
    clipboard_files = locations;
    clipboard_action = action;

    auto* data = new ClipboardData();
    data->action = action == "cut" ? "cut" : "copy";
    for (const auto& location : locations) {
        std::string uri = utils::location_to_uri(location);
        if (!uri.empty()) {
            data->uris.push_back(uri);
        }
    }
    if (data->uris.empty()) {
        delete data;
        return;
    }

    GtkTargetEntry targets[] = {
        {const_cast<gchar*>("x-special/gnome-copied-files"), 0, 0},
        {const_cast<gchar*>("text/uri-list"), 0, 1},
    };
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_with_data(
        clipboard,
        targets,
        G_N_ELEMENTS(targets),
        clipboard_get_func,
        clipboard_clear_func,
        data);
    gtk_clipboard_set_can_store(clipboard, targets, G_N_ELEMENTS(targets));
}

bool AppWindow::load_clipboard_files() {
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    std::vector<std::string> locations;
    std::string action = "copy";

    GdkAtom copied_atom = gdk_atom_intern_static_string("x-special/gnome-copied-files");
    GtkSelectionData* copied = gtk_clipboard_wait_for_contents(clipboard, copied_atom);
    if (copied) {
        const guchar* raw = gtk_selection_data_get_data(copied);
        const gint length = gtk_selection_data_get_length(copied);
        if (raw && length > 0) {
            std::string payload(reinterpret_cast<const char*>(raw), length);
            std::stringstream stream(payload);
            std::string line;
            bool first = true;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (first) {
                    action = line == "cut" ? "cut" : "copy";
                    first = false;
                    continue;
                }
                if (line.empty()) continue;
                GFile* gfile = g_file_new_for_uri(line.c_str());
                std::string location = utils::location_from_gfile(gfile);
                if (!location.empty()) {
                    locations.push_back(location);
                }
                g_object_unref(gfile);
            }
        }
        gtk_selection_data_free(copied);
    }

    if (locations.empty()) {
        gchar** uris = gtk_clipboard_wait_for_uris(clipboard);
        if (uris) {
            for (int i = 0; uris[i] != NULL; ++i) {
                GFile* gfile = g_file_new_for_uri(uris[i]);
                std::string location = utils::location_from_gfile(gfile);
                if (!location.empty()) {
                    locations.push_back(location);
                }
                g_object_unref(gfile);
            }
            g_strfreev(uris);
            action = "copy";
        }
    }

    if (locations.empty()) {
        return false;
    }

    clipboard_files = std::move(locations);
    clipboard_action = action;
    return true;
}

bool AppWindow::has_paste_data() {
    if (!clipboard_files.empty()) {
        return true;
    }
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    return gtk_clipboard_wait_is_target_available(clipboard, gdk_atom_intern_static_string("x-special/gnome-copied-files")) ||
           gtk_clipboard_wait_is_target_available(clipboard, gdk_atom_intern_static_string("text/uri-list"));
}

void AppWindow::start_paste_operation(const std::vector<std::string>& src_paths, const std::string& dest_dir, const std::string& action) {
    if (src_paths.empty()) return;

    const std::string resolved_action = action == "cut" ? "cut" : "copy";

    struct PasteJob {
        std::string src;
        std::string dest;
        bool replace;
    };

    enum class ConflictChoice {
        Cancel,
        Skip,
        Replace,
        KeepBoth
    };

    auto ask_conflict = [this](const std::string& name, const std::string& dest) -> ConflictChoice {
        std::string message = i18n::_("replace_existing_prefix") + name + i18n::_("replace_existing_suffix");
        GtkWidget* dialog = gtk_message_dialog_new(
            GTK_WINDOW(window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_NONE,
            "%s",
            message.c_str());
        gtk_window_set_title(GTK_WINDOW(dialog), i18n::_("replace_existing_title").c_str());
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", dest.c_str());
        gtk_dialog_add_button(GTK_DIALOG(dialog), i18n::_("cancel").c_str(), GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button(GTK_DIALOG(dialog), i18n::_("skip").c_str(), 1);
        gtk_dialog_add_button(GTK_DIALOG(dialog), i18n::_("keep_both").c_str(), 2);
        gtk_dialog_add_button(GTK_DIALOG(dialog), i18n::_("replace").c_str(), GTK_RESPONSE_ACCEPT);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        if (response == GTK_RESPONSE_ACCEPT) return ConflictChoice::Replace;
        if (response == 1) return ConflictChoice::Skip;
        if (response == 2) return ConflictChoice::KeepBoth;
        return ConflictChoice::Cancel;
    };

    std::vector<PasteJob> jobs;
    for (const auto& src : src_paths) {
        if (!utils::location_exists(src)) continue;

        std::string name = utils::get_filename(src);
        if (name.empty()) continue;

        std::string src_uri = utils::location_to_uri(src);
        std::string dest_uri = utils::location_to_uri(dest_dir);
        if (!src_uri.empty() && !dest_uri.empty() &&
            dest_uri.size() > src_uri.size() &&
            dest_uri.rfind(src_uri, 0) == 0 &&
            (src_uri.back() == '/' || dest_uri[src_uri.size()] == '/')) {
            continue;
        }
        if (utils::same_location(src, dest_dir)) continue;

        std::string src_parent = utils::get_parent_directory(src);
        if (resolved_action == "cut" && !src_parent.empty() && utils::same_location(src_parent, dest_dir)) {
            continue;
        }

        std::string dest = utils::child_location(dest_dir, name);
        bool replace = false;
        if (utils::location_exists(dest)) {
            if (utils::same_location(src, dest)) {
                if (resolved_action == "cut") continue;
                dest = utils::unique_child_location(dest_dir, name);
            } else {
                ConflictChoice choice = ask_conflict(name, dest);
                if (choice == ConflictChoice::Cancel) return;
                if (choice == ConflictChoice::Skip) continue;
                if (choice == ConflictChoice::KeepBoth) {
                    dest = utils::unique_child_location(dest_dir, name);
                } else {
                    replace = true;
                }
            }
        }

        jobs.push_back({src, dest, replace});
    }

    if (jobs.empty()) return;

    if (operation_control) operation_control->cancel();
    auto control = std::make_shared<OperationControl>();
    operation_control = control;

    current_progress_dialog = std::make_shared<ProgressDialog>(
        GTK_WINDOW(window), 
        i18n::_("paste"), 
        i18n::_("pasting"), 
        [control]() { control->cancel(); }
    );
    
    auto dialog = current_progress_dialog;
    std::weak_ptr<std::atomic<bool>> alive = window_alive;
    
    std::thread([this, jobs, resolved_action, dialog, control, alive]() {
        std::string error_message;
        bool completed = false;
        try {
            for (const auto& job : jobs) {
                if (control->cancelled.load()) break;
                if (!utils::location_exists(job.src)) continue;
                
                std::string name = utils::get_filename(job.src);
                
                struct MsgData {
                    std::shared_ptr<ProgressDialog> dlg;
                    std::string msg;
                };
                
                std::string msg = i18n::_("pasting") + "\n" + name;
                MsgData* md = new MsgData{dialog, msg};
                g_idle_add([](gpointer ud) -> gboolean {
                    MsgData* m = static_cast<MsgData*>(ud);
                    m->dlg->update_message(m->msg);
                    delete m;
                    return FALSE;
                }, md);

                if (job.replace && utils::location_exists(job.dest) && !utils::same_location(job.src, job.dest)) {
                    if (!utils::delete_path_recursive(job.dest, control->cancellable)) {
                        throw std::runtime_error("Could not replace " + job.dest);
                    }
                }
                
                if (resolved_action == "copy") {
                    utils::copy_path_recursive(job.src, job.dest, control->cancellable);
                } else if (resolved_action == "cut") {
                    if (!utils::move_path(job.src, job.dest, control->cancellable)) {
                        throw std::runtime_error("Could not move " + job.src + " to " + job.dest);
                    }
                }
            }
            completed = !control->cancelled.load();
        } catch (const std::exception& e) {
            if (!control->cancelled.load()) error_message = e.what();
        }

        struct CompletionData {
            AppWindow* self;
            std::weak_ptr<std::atomic<bool>> alive;
            std::shared_ptr<OperationControl> control;
            std::shared_ptr<ProgressDialog> dialog;
            std::string action;
            std::string error;
            bool completed;
        };
        g_idle_add([](gpointer ud) -> gboolean {
            auto* d = static_cast<CompletionData*>(ud);
            d->dialog->close_dialog();
            auto alive_ref = d->alive.lock();
            if (alive_ref && alive_ref->load()) {
                if (d->self->current_progress_dialog == d->dialog) {
                    d->self->current_progress_dialog.reset();
                }
                if (d->self->operation_control == d->control) {
                    d->self->operation_control.reset();
                }
                if (!d->error.empty()) {
                    d->self->show_error_dialog(i18n::_("paste_error"), d->error);
                } else if (d->completed) {
                    if (d->action == "cut") {
                        d->self->clipboard_files.clear();
                        d->self->clipboard_action.clear();
                    }
                    d->self->load_directory(d->self->file_view->get_current_dir(), false);
                }
            }
            delete d;
            return FALSE;
        }, new CompletionData{this, alive, control, dialog, resolved_action, error_message, completed});
    }).detach();
}

void AppWindow::start_compress_operation(const std::vector<std::string>& src_paths, const std::string& archive_path, const std::string& format) {
    if (src_paths.empty()) return;
    if (operation_control) operation_control->cancel();
    auto control = std::make_shared<OperationControl>();
    operation_control = control;

    current_progress_dialog = std::make_shared<ProgressDialog>(
        GTK_WINDOW(window), 
        i18n::_("compress"), 
        i18n::_("compressing"), 
        [control]() { control->cancel(); }
    );
    
    auto dialog = current_progress_dialog;
    std::weak_ptr<std::atomic<bool>> alive = window_alive;
    const std::string source_dir = file_view->get_current_dir();

    std::thread([this, src_paths, archive_path, format, source_dir, dialog, control, alive]() {
        std::atomic<GPid> active_gpid{0};
        std::string process_error;
        std::string error_message;
        bool completed = false;

        try {
            bool stage_sources = false;
            for (const auto& source : src_paths) {
                if (utils::location_to_path(source).empty()) {
                    stage_sources = true;
                    break;
                }
            }

            const std::string native_archive = utils::location_to_path(archive_path);
            const bool upload_archive = native_archive.empty();
            std::unique_ptr<ScopedTempDirectory> temporary;
            if (stage_sources || upload_archive) {
                temporary = std::make_unique<ScopedTempDirectory>("milofiles-compress-XXXXXX");
            }

            std::string command_working_dir = utils::location_to_path(source_dir);
            std::vector<std::string> command_sources;
            if (stage_sources) {
                command_working_dir = temporary->path() + "/input";
                std::filesystem::create_directories(command_working_dir);
                for (const auto& source : src_paths) {
                    if (control->cancelled.load()) throw std::runtime_error("Operation cancelled.");
                    const std::string name = utils::get_filename(source);
                    if (name.empty()) throw std::runtime_error("A selected item has no valid name.");
                    const std::string staged = command_working_dir + "/" + name;
                    utils::copy_path_recursive(source, staged, control->cancellable);
                    command_sources.push_back(name);
                }
            } else {
                if (command_working_dir.empty()) {
                    command_working_dir = std::filesystem::current_path().string();
                }
                command_sources = src_paths;
            }

            std::string command_archive = native_archive;
            if (upload_archive) {
                std::string archive_name = utils::get_filename(archive_path);
                if (archive_name.empty()) archive_name = "archive." + format;
                command_archive = temporary->path() + "/" + archive_name;
            }

            std::vector<std::string> args;
            if (format == "zip") {
                args = {"7z", "a", "-tzip", command_archive};
                args.insert(args.end(), command_sources.begin(), command_sources.end());
            } else if (format == "7z") {
                args = {"7z", "a", "-t7z", command_archive};
                args.insert(args.end(), command_sources.begin(), command_sources.end());
            } else {
                args = {"tar", "-caf", command_archive};
                for (const auto& source : command_sources) {
                    if (!stage_sources && source.rfind(command_working_dir + "/", 0) == 0) {
                        args.push_back(source.substr(command_working_dir.size() + 1));
                    } else {
                        args.push_back(source);
                    }
                }
            }

            bool ok = run_process_with_cancellation(
                args, command_working_dir, active_gpid, control->cancelled, &process_error);
            if (!ok) {
                if (control->cancelled.load()) throw std::runtime_error("Operation cancelled.");
                throw std::runtime_error(process_error.empty()
                    ? "Failed to run compression command."
                    : process_error);
            }

            if (upload_archive) {
                utils::copy_path_recursive(command_archive, archive_path, control->cancellable);
            }
            completed = !control->cancelled.load();
        } catch (const std::exception& error) {
            if (!control->cancelled.load()) error_message = error.what();
        }

        struct CompletionData {
            AppWindow* self;
            std::weak_ptr<std::atomic<bool>> alive;
            std::shared_ptr<OperationControl> control;
            std::shared_ptr<ProgressDialog> dialog;
            std::string error;
            bool completed;
        };
        g_idle_add([](gpointer ud) -> gboolean {
            auto* d = static_cast<CompletionData*>(ud);
            d->dialog->close_dialog();
            auto alive_ref = d->alive.lock();
            if (alive_ref && alive_ref->load()) {
                if (d->self->current_progress_dialog == d->dialog) {
                    d->self->current_progress_dialog.reset();
                }
                if (d->self->operation_control == d->control) {
                    d->self->operation_control.reset();
                }
                if (!d->error.empty()) {
                    d->self->show_error_dialog(i18n::_("compress_error"), d->error);
                } else if (d->completed) {
                    d->self->load_directory(d->self->file_view->get_current_dir(), false);
                }
            }
            delete d;
            return FALSE;
        }, new CompletionData{this, alive, control, dialog, error_message, completed});
    }).detach();
}

void AppWindow::start_extract_operation(const std::string& archive_path, const std::string& dest_dir) {
    if (operation_control) operation_control->cancel();
    auto control = std::make_shared<OperationControl>();
    operation_control = control;

    current_progress_dialog = std::make_shared<ProgressDialog>(
        GTK_WINDOW(window), 
        i18n::_("extract_here"), 
        i18n::_("extracting"), 
        [control]() { control->cancel(); }
    );
    
    auto dialog = current_progress_dialog;
    std::weak_ptr<std::atomic<bool>> alive = window_alive;
    
    std::thread([this, archive_path, dest_dir, dialog, control, alive]() {
        std::atomic<GPid> active_gpid{0};
        std::string process_error;
        std::string error_message;
        bool completed = false;

        try {
            const std::string native_archive = utils::location_to_path(archive_path);
            const std::string native_dest = utils::location_to_path(dest_dir);
            const bool stage_archive = native_archive.empty();
            const bool upload_results = native_dest.empty();
            std::unique_ptr<ScopedTempDirectory> temporary;
            if (stage_archive || upload_results) {
                temporary = std::make_unique<ScopedTempDirectory>("milofiles-extract-XXXXXX");
            }

            std::string command_archive = native_archive;
            if (stage_archive) {
                std::string archive_name = utils::get_filename(archive_path);
                if (archive_name.empty()) archive_name = "archive";
                command_archive = temporary->path() + "/" + archive_name;
                utils::copy_path_recursive(archive_path, command_archive, control->cancellable);
            }

            std::string command_dest = native_dest;
            if (upload_results) {
                command_dest = temporary->path() + "/output";
                std::filesystem::create_directories(command_dest);
            }

            std::string arch_lower = command_archive;
            std::transform(arch_lower.begin(), arch_lower.end(), arch_lower.begin(), ::tolower);
            auto has_suffix = [](const std::string& value, const std::string& suffix) {
                return value.size() >= suffix.size() &&
                       value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
            };
            bool is_tar_archive = has_suffix(arch_lower, ".tar") ||
                                  has_suffix(arch_lower, ".tar.gz") ||
                                  has_suffix(arch_lower, ".tgz") ||
                                  has_suffix(arch_lower, ".tar.bz2") ||
                                  has_suffix(arch_lower, ".tbz2") ||
                                  has_suffix(arch_lower, ".tar.xz") ||
                                  has_suffix(arch_lower, ".txz");

            std::vector<std::string> args;
            if (is_tar_archive) {
                args = {"tar", "-xf", command_archive, "-C", command_dest};
            } else {
                args = {"7z", "x", "-y", command_archive, "-o" + command_dest};
            }

            bool ok = run_process_with_cancellation(
                args, command_dest, active_gpid, control->cancelled, &process_error);
            if (!ok) {
                if (control->cancelled.load()) throw std::runtime_error("Operation cancelled.");
                throw std::runtime_error(process_error.empty()
                    ? "Failed to run extraction command."
                    : process_error);
            }

            if (upload_results) {
                for (const auto& entry : std::filesystem::directory_iterator(command_dest)) {
                    if (control->cancelled.load()) throw std::runtime_error("Operation cancelled.");
                    const std::string target = utils::child_location(dest_dir, entry.path().filename().string());
                    utils::copy_path_recursive(entry.path().string(), target, control->cancellable);
                }
            }
            completed = !control->cancelled.load();
        } catch (const std::exception& error) {
            if (!control->cancelled.load()) error_message = error.what();
        }

        struct CompletionData {
            AppWindow* self;
            std::weak_ptr<std::atomic<bool>> alive;
            std::shared_ptr<OperationControl> control;
            std::shared_ptr<ProgressDialog> dialog;
            std::string error;
            bool completed;
        };
        g_idle_add([](gpointer ud) -> gboolean {
            auto* d = static_cast<CompletionData*>(ud);
            d->dialog->close_dialog();
            auto alive_ref = d->alive.lock();
            if (alive_ref && alive_ref->load()) {
                if (d->self->current_progress_dialog == d->dialog) {
                    d->self->current_progress_dialog.reset();
                }
                if (d->self->operation_control == d->control) {
                    d->self->operation_control.reset();
                }
                if (!d->error.empty()) {
                    d->self->show_error_dialog(i18n::_("extract_error"), d->error);
                } else if (d->completed) {
                    d->self->load_directory(d->self->file_view->get_current_dir(), false);
                }
            }
            delete d;
            return FALSE;
        }, new CompletionData{this, alive, control, dialog, error_message, completed});
    }).detach();
}

void AppWindow::start_delete_operation(const std::vector<std::string>& paths, const std::string& view_dir) {
    if (paths.empty()) return;
    if (operation_control) operation_control->cancel();
    auto control = std::make_shared<OperationControl>();
    operation_control = control;

    current_progress_dialog = std::make_shared<ProgressDialog>(
        GTK_WINDOW(window),
        i18n::_("delete"),
        i18n::_("deleting"),
        [control]() { control->cancel(); });

    auto dialog = current_progress_dialog;
    std::weak_ptr<std::atomic<bool>> alive = window_alive;

    std::thread([this, paths, view_dir, dialog, control, alive]() {
        std::vector<std::string> failed;
        for (const auto& path : paths) {
            if (control->cancelled.load()) break;
            if (!utils::delete_path_recursive(path, control->cancellable)) {
                if (!control->cancelled.load()) failed.push_back(path);
            }
        }

        struct CompletionData {
            AppWindow* self;
            std::weak_ptr<std::atomic<bool>> alive;
            std::shared_ptr<OperationControl> control;
            std::shared_ptr<ProgressDialog> dialog;
            std::vector<std::string> failed;
            std::string view_dir;
        };
        g_idle_add([](gpointer ud) -> gboolean {
            auto* d = static_cast<CompletionData*>(ud);
            d->dialog->close_dialog();
            auto alive_ref = d->alive.lock();
            if (alive_ref && alive_ref->load()) {
                if (d->self->current_progress_dialog == d->dialog) {
                    d->self->current_progress_dialog.reset();
                }
                if (d->self->operation_control == d->control) {
                    d->self->operation_control.reset();
                }
                if (!d->failed.empty()) {
                    d->self->show_error_dialog(
                        i18n::_("delete_error"), summarize_operation_locations(d->failed));
                }
                if (utils::same_location(d->self->file_view->get_current_dir(), d->view_dir)) {
                    d->self->load_directory(d->view_dir, false);
                }
            }
            delete d;
            return FALSE;
        }, new CompletionData{this, alive, control, dialog, std::move(failed), view_dir});
    }).detach();
}

void AppWindow::start_trash_operation(const std::vector<std::string>& paths, const std::string& view_dir) {
    if (paths.empty()) return;
    if (operation_control) operation_control->cancel();
    auto control = std::make_shared<OperationControl>();
    operation_control = control;

    current_progress_dialog = std::make_shared<ProgressDialog>(
        GTK_WINDOW(window),
        i18n::_("trash_action"),
        i18n::_("trashing"),
        [control]() { control->cancel(); });

    auto dialog = current_progress_dialog;
    std::weak_ptr<std::atomic<bool>> alive = window_alive;

    std::thread([this, paths, view_dir, dialog, control, alive]() {
        std::vector<std::string> failed;
        for (const auto& path : paths) {
            if (control->cancelled.load()) break;
            GFile* file = utils::new_gfile_for_location(path);
            GError* error = nullptr;
            if (!g_file_trash(file, control->cancellable, &error) && !control->cancelled.load()) {
                failed.push_back(path);
            }
            if (error) g_error_free(error);
            g_object_unref(file);
        }

        struct CompletionData {
            AppWindow* self;
            std::weak_ptr<std::atomic<bool>> alive;
            std::shared_ptr<OperationControl> control;
            std::shared_ptr<ProgressDialog> dialog;
            std::vector<std::string> failed;
            std::string view_dir;
        };
        g_idle_add([](gpointer ud) -> gboolean {
            auto* d = static_cast<CompletionData*>(ud);
            d->dialog->close_dialog();
            auto alive_ref = d->alive.lock();
            if (alive_ref && alive_ref->load()) {
                if (d->self->current_progress_dialog == d->dialog) {
                    d->self->current_progress_dialog.reset();
                }
                if (d->self->operation_control == d->control) {
                    d->self->operation_control.reset();
                }

                if (!d->failed.empty() && !d->control->cancelled.load()) {
                    GtkWidget* confirm = gtk_message_dialog_new(
                        d->self->get_window(),
                        GTK_DIALOG_MODAL,
                        GTK_MESSAGE_QUESTION,
                        GTK_BUTTONS_YES_NO,
                        "%s", i18n::_("trash_fallback_confirm").c_str());
                    gtk_window_set_title(GTK_WINDOW(confirm), i18n::_("delete_title").c_str());
                    gtk_message_dialog_format_secondary_text(
                        GTK_MESSAGE_DIALOG(confirm),
                        "%s", summarize_operation_locations(d->failed).c_str());
                    const gint response = gtk_dialog_run(GTK_DIALOG(confirm));
                    gtk_widget_destroy(confirm);
                    if (response == GTK_RESPONSE_YES) {
                        d->self->start_delete_operation(d->failed, d->view_dir);
                        delete d;
                        return FALSE;
                    }
                }
                if (utils::same_location(d->self->file_view->get_current_dir(), d->view_dir)) {
                    d->self->load_directory(d->view_dir, false);
                }
            }
            delete d;
            return FALSE;
        }, new CompletionData{this, alive, control, dialog, std::move(failed), view_dir});
    }).detach();
}

void AppWindow::mount_network_share(const std::string& uri) {
    if (uri.empty()) return;

    GFile* gfile = g_file_new_for_uri(uri.c_str());
    GMountOperation* mount_op = gtk_mount_operation_new(GTK_WINDOW(window));

    struct MountData {
        AppWindow* self;
        std::weak_ptr<std::atomic<bool>> alive;
        GFile* gfile;
        GMountOperation* mount_op;
    };
    MountData* md = new MountData{this, window_alive, gfile, mount_op};

    g_file_mount_enclosing_volume(gfile, G_MOUNT_MOUNT_NONE, G_MOUNT_OPERATION(mount_op),
                                  NULL,
                                  [](GObject* source, GAsyncResult* res, gpointer user_data) {
        MountData* data = static_cast<MountData*>(user_data);
        GError* error = NULL;
        g_file_mount_enclosing_volume_finish(G_FILE(source), res, &error);

        bool mount_ok = true;
        if (error) {
            if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED)) {
                mount_ok = false;
                struct ErrInfo {
                    AppWindow* self;
                    std::weak_ptr<std::atomic<bool>> alive;
                    std::string msg;
                };
                ErrInfo* ei = new ErrInfo{data->self, data->alive, error->message};
                g_idle_add([](gpointer ud) -> gboolean {
                    ErrInfo* e = static_cast<ErrInfo*>(ud);
                    auto alive_ref = e->alive.lock();
                    if (alive_ref && alive_ref->load()) {
                        e->self->show_error_dialog(i18n::_("connection_error"), e->msg);
                    }
                    delete e;
                    return FALSE;
                }, ei);
            }
            g_error_free(error);
        }

        if (mount_ok) {
            struct NavData {
                AppWindow* self;
                std::weak_ptr<std::atomic<bool>> alive;
                GFile* gfile;
            };
            NavData* nd = new NavData{data->self, data->alive, data->gfile};
            g_object_ref(data->gfile);

            g_idle_add([](gpointer ud) -> gboolean {
                NavData* n = static_cast<NavData*>(ud);
                auto alive_ref = n->alive.lock();
                if (alive_ref && alive_ref->load()) {
                    n->self->reload_sidebar();
                    n->self->load_directory(utils::location_from_gfile(n->gfile));
                }

                g_object_unref(n->gfile);
                delete n;
                return FALSE;
            }, nd);
        }

        g_object_unref(data->gfile);
        g_object_unref(data->mount_op);
        delete data;
    }, md);
}

void AppWindow::show_go_location_dialog() {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(i18n::_("go_location").c_str(),
                                                    GTK_WINDOW(window),
                                                    GTK_DIALOG_MODAL,
                                                    i18n::_("cancel").c_str(), GTK_RESPONSE_CANCEL,
                                                    "OK", GTK_RESPONSE_OK,
                                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(box), 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), file_view->get_current_dir().c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        std::string path = gtk_entry_get_text(GTK_ENTRY(entry));
        navigate_from_location_text(path);
    }
    gtk_widget_destroy(dialog);
}

void AppWindow::show_connect_server_dialog() {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(i18n::_("connect_to_server").c_str(),
                                                    GTK_WINDOW(window),
                                                    GTK_DIALOG_MODAL,
                                                    i18n::_("cancel").c_str(), GTK_RESPONSE_CANCEL,
                                                    i18n::_("connect").c_str(), GTK_RESPONSE_OK,
                                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(box), 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    
    GtkWidget* lbl = gtk_label_new(i18n::_("enter_server_address").c_str());
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), i18n::_("connect_placeholder").c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    
    if (response == GTK_RESPONSE_OK) {
        std::string uri = gtk_entry_get_text(GTK_ENTRY(entry));
        gtk_widget_destroy(dialog);
        
        if (!uri.empty()) {
            mount_network_share(uri);
        }
    } else {
        gtk_widget_destroy(dialog);
    }
}
