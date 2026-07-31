#include "launcher.hpp"

#include <glib.h>
#include <glib/gstdio.h>

#include <limits.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

const char* ITEM_GROUP = "miloDockItem";

const std::vector<std::string> DEFAULT_DESKTOP_IDS = {
    "milofiles.desktop",
    "xfce4-terminal.desktop",
    "mousepad.desktop",
    "org.xfce.mousepad.desktop",
    "firefox-esr.desktop",
};

std::string basename(const std::string& path) {
    return fs::path(path).filename().string();
}

bool has_suffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string normalize_token(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    if (has_suffix(value, ".desktop")) {
        value = value.substr(0, value.size() - 8);
    }
    if (has_suffix(value, ".appimage")) {
        value = value.substr(0, value.size() - 9);
    }
    std::replace(value.begin(), value.end(), '_', '-');
    return value;
}

std::set<std::string> token_variants(const std::string& value) {
    std::set<std::string> variants;
    std::string token = normalize_token(value);
    if (token.empty()) {
        return variants;
    }
    variants.insert(token);
    auto dot = token.rfind('.');
    if (dot != std::string::npos && dot + 1 < token.size()) {
        variants.insert(token.substr(dot + 1));
    }
    if (has_suffix(token, "-esr")) {
        variants.insert(token.substr(0, token.size() - 4));
    }
    if (token.rfind("org.xfce.", 0) == 0) {
        variants.insert(token.substr(9));
    }
    return variants;
}

void add_tokens(std::set<std::string>& target, const std::string& value) {
    for (const auto& token : token_variants(value)) {
        target.insert(token);
    }
    if (value.find('/') != std::string::npos) {
        for (const auto& token : token_variants(basename(value))) {
            target.insert(token);
        }
    }
}

std::string read_dockitem_launcher(const fs::path& path) {
    GKeyFile* key_file = g_key_file_new();
    GError* error = nullptr;
    g_key_file_load_from_file(key_file, path.c_str(), G_KEY_FILE_NONE, &error);
    if (error) {
        g_error_free(error);
        g_key_file_unref(key_file);
        return "";
    }

    char* raw = g_key_file_get_string(key_file, ITEM_GROUP, "Launcher", nullptr);
    std::string launcher = raw ? raw : "";
    g_free(raw);
    g_key_file_unref(key_file);
    return launcher;
}

std::string path_from_file_uri(const std::string& uri) {
    GFile* file = g_file_new_for_uri(uri.c_str());
    char* raw_path = g_file_get_path(file);
    std::string path = raw_path ? raw_path : "";
    g_free(raw_path);
    g_object_unref(file);
    return path;
}

std::vector<fs::path> app_dirs() {
    std::vector<fs::path> dirs;
    dirs.emplace_back(fs::path(g_get_user_data_dir()) / "applications");

    const char* xdg_data_dirs = g_getenv("XDG_DATA_DIRS");
    std::string raw_dirs = xdg_data_dirs ? xdg_data_dirs : "/usr/local/share:/usr/share";
    std::stringstream stream(raw_dirs);
    std::string dir;
    while (std::getline(stream, dir, ':')) {
        if (!dir.empty()) {
            dirs.emplace_back(fs::path(dir) / "applications");
        }
    }
    return dirs;
}

fs::path executable_dir() {
    char buffer[PATH_MAX] = {0};
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return fs::current_path();
    }
    buffer[length] = '\0';
    return fs::path(buffer).parent_path();
}

fs::path user_launcher_dir() {
    return fs::path(g_get_user_config_dir()) / "miloDock/launchers";
}

std::string find_desktop_path(const std::string& ref) {
    if (ref.empty()) {
        return "";
    }
    if (ref.rfind("file://", 0) == 0) {
        std::string path = path_from_file_uri(ref);
        return fs::exists(path) ? path : "";
    }

    fs::path raw(ref);
    if (raw.is_absolute() && fs::exists(raw)) {
        return raw.string();
    }

    std::string desktop_id = has_suffix(ref, ".desktop") ? basename(ref) : ref;
    std::vector<std::string> candidates = {desktop_id};
    if (desktop_id == "mousepad.desktop") {
        candidates.emplace_back("org.xfce.mousepad.desktop");
    }

    for (const auto& candidate_name : candidates) {
        for (const auto& dir : app_dirs()) {
            fs::path candidate = dir / candidate_name;
            if (fs::exists(candidate)) {
                return candidate.string();
            }
        }
    }

    std::string stem = has_suffix(desktop_id, ".desktop") ? desktop_id.substr(0, desktop_id.size() - 8) : desktop_id;
    for (const auto& dir : app_dirs()) {
        if (!fs::exists(dir)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() == ".desktop" && entry.path().filename().string().find(stem) != std::string::npos) {
                return entry.path().string();
            }
        }
    }

    return "";
}

std::string desktop_key(const std::string& desktop_path, const char* key) {
    GKeyFile* key_file = g_key_file_new();
    GError* error = nullptr;
    g_key_file_load_from_file(key_file, desktop_path.c_str(), G_KEY_FILE_NONE, &error);
    if (error) {
        g_error_free(error);
        g_key_file_unref(key_file);
        return "";
    }
    char* raw = g_key_file_get_string(key_file, "Desktop Entry", key, nullptr);
    std::string value = raw ? raw : "";
    g_free(raw);
    g_key_file_unref(key_file);
    return value;
}

std::string command_basename(const char* commandline) {
    if (!commandline) {
        return "";
    }

    int argc = 0;
    char** argv = nullptr;
    GError* error = nullptr;
    if (!g_shell_parse_argv(commandline, &argc, &argv, &error)) {
        if (error) {
            g_error_free(error);
        }
        return "";
    }

    std::string result;
    for (int i = 0; i < argc; ++i) {
        if (argv[i] && argv[i][0] != '%') {
            result = basename(argv[i]);
            break;
        }
    }
    g_strfreev(argv);
    return result;
}

std::string normalized_gtk_modules(const char* modules) {
    std::vector<std::string> ordered_modules;
    std::set<std::string> seen;

    auto add_module = [&ordered_modules, &seen](const std::string& module) {
        if (module.empty() || seen.count(module)) {
            return;
        }
        seen.insert(module);
        ordered_modules.push_back(module);
    };

    add_module("appmenu-gtk-module");
    std::stringstream stream(modules ? modules : "");
    std::string module;
    while (std::getline(stream, module, ':')) {
        add_module(module);
    }

    std::string result;
    for (const std::string& module : ordered_modules) {
        if (!result.empty()) {
            result += ":";
        }
        result += module;
    }
    return result;
}

Launcher launcher_from_desktop_path(const std::string& desktop_path, bool include_hidden = false) {
    Launcher launcher;
    GDesktopAppInfo* app_info = g_desktop_app_info_new_from_filename(desktop_path.c_str());
    if (!app_info || (!include_hidden && !g_app_info_should_show(G_APP_INFO(app_info)))) {
        if (app_info) {
            g_object_unref(app_info);
        }
        return launcher;
    }

    launcher.desktop_path = desktop_path;
    launcher.desktop_id = basename(desktop_path);
    launcher.app_info = app_info;

    const char* display_name = g_app_info_get_display_name(G_APP_INFO(app_info));
    const char* name = g_app_info_get_name(G_APP_INFO(app_info));
    launcher.name = display_name ? display_name : (name ? name : launcher.desktop_id);

    GIcon* icon = g_app_info_get_icon(G_APP_INFO(app_info));
    launcher.icon = icon ? G_ICON(g_object_ref(icon)) : nullptr;

    add_tokens(launcher.match_tokens, launcher.desktop_id);
    add_tokens(launcher.match_tokens, fs::path(desktop_path).stem().string());
    add_tokens(launcher.match_tokens, launcher.name);
    add_tokens(launcher.match_tokens, desktop_key(desktop_path, "StartupWMClass"));
    const std::string appimage_path =
        desktop_key(desktop_path, "X-miloOS-AppImage");
    if (appimage_path.empty()) {
        add_tokens(
            launcher.match_tokens,
            command_basename(
                g_app_info_get_commandline(G_APP_INFO(app_info))));
    } else {
        add_tokens(launcher.match_tokens, appimage_path);
    }

    return launcher;
}

std::string canonical_path(const std::string& path) {
    gchar* canonical = g_canonicalize_filename(path.c_str(), nullptr);
    std::string result = canonical ? canonical : path;
    g_free(canonical);
    return result;
}

Launcher registered_launcher_for_appimage(const std::string& appimage_path) {
    const std::string wanted = canonical_path(appimage_path);
    for (const auto& directory : app_dirs()) {
        if (!fs::exists(directory)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".desktop") {
                continue;
            }
            const std::string registered =
                desktop_key(entry.path().string(), "X-miloOS-AppImage");
            if (!registered.empty() && canonical_path(registered) == wanted) {
                return launcher_from_desktop_path(entry.path().string(), true);
            }
        }
    }
    return {};
}

std::vector<fs::path> launcher_dirs() {
    std::vector<fs::path> dirs = {
        user_launcher_dir(),
        fs::path("/etc/xdg/miloDock/launchers"),
    };

    fs::path executable_parent = executable_dir();
    dirs.push_back(executable_parent.parent_path() / "launchers");

    for (fs::path cursor = executable_parent; !cursor.empty() && cursor.has_parent_path(); cursor = cursor.parent_path()) {
        fs::path repo_launchers = cursor / "configurations/miloDock/launchers";
        if (fs::exists(repo_launchers)) {
            dirs.push_back(repo_launchers);
            break;
        }
        if (cursor == cursor.parent_path()) {
            break;
        }
    }

    return dirs;
}

std::string safe_launcher_name(const Launcher& launcher, int index) {
    std::string stem = has_suffix(launcher.desktop_id, ".desktop")
        ? launcher.desktop_id.substr(0, launcher.desktop_id.size() - 8)
        : launcher.desktop_id;
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') {
            c = '-';
        }
    }
    if (stem.empty()) {
        stem = "launcher";
    }

    std::ostringstream name;
    name << std::setw(2) << std::setfill('0') << index << "-" << stem << ".dockitem";
    return name.str();
}

} // namespace

Launcher::Launcher(const Launcher& other)
    : desktop_id(other.desktop_id),
      desktop_path(other.desktop_path),
      name(other.name),
      app_info(other.app_info ? G_DESKTOP_APP_INFO(g_object_ref(other.app_info)) : nullptr),
      icon(other.icon ? G_ICON(g_object_ref(other.icon)) : nullptr),
      match_tokens(other.match_tokens) {}

Launcher& Launcher::operator=(const Launcher& other) {
    if (this == &other) {
        return *this;
    }
    if (app_info) {
        g_object_unref(app_info);
    }
    if (icon) {
        g_object_unref(icon);
    }
    desktop_id = other.desktop_id;
    desktop_path = other.desktop_path;
    name = other.name;
    app_info = other.app_info ? G_DESKTOP_APP_INFO(g_object_ref(other.app_info)) : nullptr;
    icon = other.icon ? G_ICON(g_object_ref(other.icon)) : nullptr;
    match_tokens = other.match_tokens;
    return *this;
}

Launcher::Launcher(Launcher&& other) noexcept
    : desktop_id(std::move(other.desktop_id)),
      desktop_path(std::move(other.desktop_path)),
      name(std::move(other.name)),
      app_info(other.app_info),
      icon(other.icon),
      match_tokens(std::move(other.match_tokens)) {
    other.app_info = nullptr;
    other.icon = nullptr;
}

Launcher& Launcher::operator=(Launcher&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    if (app_info) {
        g_object_unref(app_info);
    }
    if (icon) {
        g_object_unref(icon);
    }
    desktop_id = std::move(other.desktop_id);
    desktop_path = std::move(other.desktop_path);
    name = std::move(other.name);
    app_info = other.app_info;
    icon = other.icon;
    match_tokens = std::move(other.match_tokens);
    other.app_info = nullptr;
    other.icon = nullptr;
    return *this;
}

Launcher::~Launcher() {
    if (app_info) {
        g_object_unref(app_info);
    }
    if (icon) {
        g_object_unref(icon);
    }
}

bool launcher_is_available(const Launcher& launcher) {
    std::error_code error;
    if (launcher.desktop_path.empty() ||
        !fs::exists(launcher.desktop_path, error)) {
        return false;
    }

    const std::string appimage_path =
        desktop_key(launcher.desktop_path, "X-miloOS-AppImage");
    if (!appimage_path.empty()) {
        error.clear();
        return fs::exists(appimage_path, error);
    }

    const std::string try_exec =
        desktop_key(launcher.desktop_path, "TryExec");
    if (try_exec.empty()) {
        return true;
    }
    if (fs::path(try_exec).is_absolute()) {
        error.clear();
        return fs::exists(try_exec, error);
    }
    char* executable = g_find_program_in_path(try_exec.c_str());
    const bool available = executable != nullptr;
    g_free(executable);
    return available;
}

std::vector<Launcher> load_launchers() {
    std::vector<Launcher> launchers;
    std::set<std::string> seen;
    bool configuration_found = false;
    const fs::path user_directory = user_launcher_dir();

    for (const auto& dir : launcher_dirs()) {
        if (!fs::exists(dir)) {
            continue;
        }

        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            auto suffix = entry.path().extension().string();
            if (suffix == ".dockitem" || suffix == ".desktop") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());
        if (files.empty()) {
            if (dir == user_directory) {
                configuration_found = true;
                break;
            }
            continue;
        }
        configuration_found = true;

        for (const auto& file : files) {
            std::string ref = file.extension() == ".dockitem" ? read_dockitem_launcher(file) : file.string();
            std::string desktop_path = find_desktop_path(ref);
            if (desktop_path.empty()) {
                if (dir == user_directory) {
                    std::error_code error;
                    fs::remove(file, error);
                }
                continue;
            }
            Launcher launcher = launcher_from_desktop_path(desktop_path, true);
            if (!launcher.app_info || !launcher_is_available(launcher)) {
                if (dir == user_directory) {
                    std::error_code error;
                    fs::remove(file, error);
                }
                continue;
            }
            if (seen.count(launcher.desktop_id)) {
                continue;
            }
            seen.insert(launcher.desktop_id);
            launchers.push_back(std::move(launcher));
        }
        break;
    }

    if (!configuration_found && launchers.empty()) {
        for (const auto& desktop_id : DEFAULT_DESKTOP_IDS) {
            std::string desktop_path = find_desktop_path(desktop_id);
            if (desktop_path.empty()) {
                continue;
            }
            Launcher launcher = launcher_from_desktop_path(desktop_path);
            if (launcher.app_info &&
                launcher_is_available(launcher) &&
                !seen.count(launcher.desktop_id)) {
                seen.insert(launcher.desktop_id);
                launchers.push_back(std::move(launcher));
            }
        }
    }

    return launchers;
}

std::vector<Launcher> all_desktop_launchers() {
    std::vector<Launcher> launchers;
    std::set<std::string> seen;
    for (const auto& dir : app_dirs()) {
        if (!fs::exists(dir)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".desktop") {
                continue;
            }
            Launcher launcher = launcher_from_desktop_path(entry.path().string(), true);
            if (!launcher.app_info ||
                !launcher_is_available(launcher) ||
                seen.count(launcher.desktop_id)) {
                continue;
            }
            seen.insert(launcher.desktop_id);
            launchers.push_back(std::move(launcher));
        }
    }
    return launchers;
}

std::vector<Launcher> launchers_from_uri_list(const std::vector<std::string>& uris) {
    std::vector<Launcher> launchers;
    for (const auto& uri : uris) {
        std::string desktop_path = path_from_file_uri(uri);
        if (desktop_path.empty()) {
            continue;
        }
        std::string lower_path = desktop_path;
        std::transform(
            lower_path.begin(),
            lower_path.end(),
            lower_path.begin(),
            [](unsigned char c) { return std::tolower(c); });
        if (has_suffix(lower_path, ".appimage")) {
            Launcher launcher = launcher_for_appimage(desktop_path);
            if (launcher.app_info) {
                launchers.push_back(std::move(launcher));
            }
            continue;
        }
        if (!has_suffix(desktop_path, ".desktop")) {
            desktop_path = find_desktop_path(basename(desktop_path));
        }
        if (desktop_path.empty()) {
            continue;
        }
        Launcher launcher = launcher_from_desktop_path(desktop_path, true);
        if (launcher.app_info) {
            launchers.push_back(std::move(launcher));
        }
    }
    return launchers;
}

Launcher launcher_from_text(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    if (has_suffix(lower, ".appimage") && fs::exists(value)) {
        return launcher_for_appimage(value);
    }
    std::string desktop_path = find_desktop_path(value);
    return desktop_path.empty() ? Launcher() : launcher_from_desktop_path(desktop_path, true);
}

Launcher launcher_for_appimage(const std::string& path) {
    if (path.empty() || !fs::exists(path)) {
        return {};
    }
    Launcher launcher = registered_launcher_for_appimage(path);
    if (launcher.app_info) {
        return launcher;
    }

    gchar* argv[] = {
        const_cast<gchar*>("milofiles"),
        const_cast<gchar*>("--register-appimage"),
        const_cast<gchar*>(path.c_str()),
        nullptr
    };
    gint wait_status = 0;
    GError* error = nullptr;
    const GSpawnFlags flags = static_cast<GSpawnFlags>(
        G_SPAWN_SEARCH_PATH |
        G_SPAWN_STDOUT_TO_DEV_NULL |
        G_SPAWN_STDERR_TO_DEV_NULL);
    const bool spawned = g_spawn_sync(
        nullptr,
        argv,
        nullptr,
        flags,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &wait_status,
        &error);
    if (error) {
        g_error_free(error);
        error = nullptr;
    }
    if (!spawned || !g_spawn_check_wait_status(wait_status, &error)) {
        if (error) {
            g_error_free(error);
        }
        return {};
    }
    return registered_launcher_for_appimage(path);
}

void save_launcher_order(const std::vector<Launcher>& launchers) {
    fs::path directory = fs::path(g_get_user_config_dir()) / "miloDock/launchers";
    fs::create_directories(directory);

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && (entry.path().extension() == ".dockitem" || entry.path().extension() == ".desktop")) {
            fs::remove(entry.path());
        }
    }

    int index = 1;
    for (const auto& launcher : launchers) {
        fs::path path = directory / safe_launcher_name(launcher, index++);
        GFile* file = g_file_new_for_path(launcher.desktop_path.c_str());
        char* uri = g_file_get_uri(file);
        std::ofstream output(path);
        output << "[" << ITEM_GROUP << "]\n";
        output << "Launcher=" << (uri ? uri : "") << "\n";
        g_free(uri);
        g_object_unref(file);
    }
}

void launch_app(const Launcher& launcher) {
    if (!launcher.app_info) {
        return;
    }

    const char* home = g_get_home_dir();
    if (home) {
        g_chdir(home);
        g_setenv("PWD", home, TRUE);
    }

    GError* error = nullptr;
    GAppLaunchContext* context = g_app_launch_context_new();
    std::string gtk_modules = normalized_gtk_modules(g_getenv("GTK_MODULES"));
    g_app_launch_context_setenv(context, "GTK_MODULES", gtk_modules.c_str());
    g_app_launch_context_setenv(context, "UBUNTU_MENUPROXY", "1");
    const std::string appimage_path =
        desktop_key(launcher.desktop_path, "X-miloOS-AppImage");
    if (!appimage_path.empty()) {
        g_app_launch_context_setenv(
            context, "MILO_APPIMAGE_PATH", appimage_path.c_str());
    }
    g_app_info_launch(G_APP_INFO(launcher.app_info), nullptr, context, &error);
    if (error) {
        g_error_free(error);
    }
    g_object_unref(context);
}
