#pragma once

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include <set>
#include <string>
#include <vector>

struct Launcher {
    std::string desktop_id;
    std::string desktop_path;
    std::string name;
    GDesktopAppInfo* app_info = nullptr;
    GIcon* icon = nullptr;
    std::set<std::string> match_tokens;

    Launcher() = default;
    Launcher(const Launcher& other);
    Launcher& operator=(const Launcher& other);
    Launcher(Launcher&& other) noexcept;
    Launcher& operator=(Launcher&& other) noexcept;
    ~Launcher();
};

std::vector<Launcher> load_launchers();
std::vector<Launcher> all_desktop_launchers();
std::vector<Launcher> launchers_from_uri_list(const std::vector<std::string>& uris);
Launcher launcher_from_text(const std::string& value);
Launcher launcher_for_appimage(const std::string& path);
bool launcher_is_available(const Launcher& launcher);
void save_launcher_order(const std::vector<Launcher>& launchers);
void launch_app(const Launcher& launcher);
