#ifndef PROGRESS_DIALOG_HPP
#define PROGRESS_DIALOG_HPP

#include <gtk/gtk.h>
#include <string>
#include <functional>

class ProgressDialog {
public:
    ProgressDialog(GtkWindow* parent, const std::string& title, const std::string& message, std::function<void()> cancel_cb = nullptr);
    ~ProgressDialog();

    void update_message(const std::string& message);
    void close_dialog();
    GtkWindow* get_widget();

private:
    GtkWidget* window;
    GtkWidget* lbl_msg;
    GtkWidget* pbar;
    guint pulse_timeout_id;
    std::function<void()> cancel_callback;

    static gboolean pulse_progressbar_callback(gpointer data);
    static void on_cancel_clicked(GtkWidget* button, gpointer data);
    static void on_window_destroyed(GtkWidget* widget, gpointer data);
};

#endif // PROGRESS_DIALOG_HPP
