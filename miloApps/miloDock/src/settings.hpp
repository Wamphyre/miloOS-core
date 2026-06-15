#pragma once

#include <string>

struct DockSettings {
    int icon_size = 38;
    int launcher_spacing = 3;
    bool auto_hide = false;
    std::string effect = "magnify";
    std::string theme = "auto";
    std::string system_theme = "light";

    static DockSettings load();
    void save() const;
    bool is_dark() const;
};
