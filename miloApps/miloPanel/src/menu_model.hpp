#pragma once

#include <gtk/gtk.h>

#include <string>
#include <vector>

struct MenuEntry {
    bool separator = false;
    std::string desktop_id;
};

std::vector<MenuEntry> load_menu_entries(const std::string& menu_file);
GtkWidget* build_app_menu(const std::vector<MenuEntry>& entries, GtkWindow* parent);
