#pragma once

#include <string>

struct PanelSettings {
    int height = 24;
    int icon_size = 16;
    bool reserve_space = true;
    std::string theme = "auto";
    std::string system_theme = "light";
    std::string logo_light = "/usr/share/themes/miloOS/logo.png";
    std::string logo_dark = "/usr/share/themes/miloOS-Dark/logo.png";
    std::string menu_file = "/etc/xdg/menus/milo.menu";
    std::string clock_format = "%a %d, %R";
    std::string clock_tooltip_format = "%A %d %B %Y";
    std::string clock_font = "SF Pro Text Medium 10";

    static PanelSettings load();
    bool is_dark() const;
};
