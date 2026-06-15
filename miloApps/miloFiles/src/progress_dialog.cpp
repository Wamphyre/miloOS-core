#include "progress_dialog.hpp"
#include "i18n.hpp"

struct UpdateMsgData {
    ProgressDialog* dialog;
    std::string message;
};

ProgressDialog::ProgressDialog(GtkWindow* parent, const std::string& title, const std::string& message, std::function<void()> cancel_cb)
    : window(nullptr), lbl_msg(nullptr), pbar(nullptr), pulse_timeout_id(0), cancel_callback(cancel_cb) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), title.c_str());
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(window), parent);
    }
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 350, 110);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 14);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    lbl_msg = gtk_label_new(message.c_str());
    gtk_label_set_xalign(GTK_LABEL(lbl_msg), 0.0);
    gtk_label_set_yalign(GTK_LABEL(lbl_msg), 0.5);
    gtk_label_set_line_wrap(GTK_LABEL(lbl_msg), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl_msg), 45);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_msg, FALSE, FALSE, 0);

    pbar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), pbar, FALSE, FALSE, 0);

    pulse_timeout_id = g_timeout_add(100, pulse_progressbar_callback, this);

    if (cancel_callback) {
        GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget* btn_cancel = gtk_button_new_with_label(i18n::_("cancel").c_str());
        g_signal_connect(btn_cancel, "clicked", G_CALLBACK(on_cancel_clicked), this);
        gtk_box_pack_end(GTK_BOX(hbox), btn_cancel, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(window);
}

ProgressDialog::~ProgressDialog() {
    close_dialog();
}

gboolean ProgressDialog::pulse_progressbar_callback(gpointer data) {
    ProgressDialog* self = static_cast<ProgressDialog*>(data);
    if (self->pbar) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(self->pbar));
    }
    return TRUE;
}

void ProgressDialog::update_message(const std::string& message) {
    UpdateMsgData* data = new UpdateMsgData{this, message};
    g_idle_add([](gpointer user_data) -> gboolean {
        UpdateMsgData* d = static_cast<UpdateMsgData*>(user_data);
        if (d->dialog && d->dialog->window && d->dialog->lbl_msg) {
            gtk_label_set_text(GTK_LABEL(d->dialog->lbl_msg), d->message.c_str());
        }
        delete d;
        return FALSE;
    }, data);
}

void ProgressDialog::on_cancel_clicked(GtkWidget* button, gpointer data) {
    ProgressDialog* self = static_cast<ProgressDialog*>(data);
    if (self->cancel_callback) {
        self->cancel_callback();
    }
    self->close_dialog();
}

void ProgressDialog::close_dialog() {
    g_idle_add([](gpointer user_data) -> gboolean {
        ProgressDialog* self = static_cast<ProgressDialog*>(user_data);
        if (self->pulse_timeout_id != 0) {
            g_source_remove(self->pulse_timeout_id);
            self->pulse_timeout_id = 0;
        }
        if (self->window) {
            gtk_widget_destroy(self->window);
            self->window = nullptr;
            self->lbl_msg = nullptr;
            self->pbar = nullptr;
        }
        return FALSE;
    }, this);
}

GtkWindow* ProgressDialog::get_widget() {
    return GTK_WINDOW(window);
}
