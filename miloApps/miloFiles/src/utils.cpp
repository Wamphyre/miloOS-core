#include "utils.hpp"
#include "i18n.hpp"
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <dirent.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <cerrno>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace utils {

std::string get_bookmarks_path();
std::string get_filename(const std::string& path);

static std::string trim_copy(const std::string& value) {
    std::string out = value;
    out.erase(out.begin(), std::find_if(out.begin(), out.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    out.erase(std::find_if(out.rbegin(), out.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), out.end());
    return out;
}

static std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static void ensure_bookmarks_dir() {
    std::string config_dir = std::string(g_get_user_config_dir()) + "/gtk-3.0";
    g_mkdir_with_parents(config_dir.c_str(), 0700);
}

bool has_uri_scheme(const std::string& location) {
    const size_t pos = location.find("://");
    if (pos == std::string::npos || pos == 0) {
        return false;
    }
    for (size_t i = 0; i < pos; ++i) {
        const unsigned char ch = static_cast<unsigned char>(location[i]);
        if (!std::isalpha(ch) && !std::isdigit(ch) && ch != '+' && ch != '-' && ch != '.') {
            return false;
        }
    }
    return true;
}

static GFile* file_for_location(const std::string& location) {
    if (has_uri_scheme(location)) {
        return g_file_new_for_uri(location.c_str());
    }
    return g_file_new_for_path(location.c_str());
}

static std::string location_from_file(GFile* file) {
    if (!file) {
        return "";
    }

    char* uri = g_file_get_uri(file);
    if (uri && !g_str_has_prefix(uri, "file://")) {
        std::string result = uri;
        g_free(uri);
        return result;
    }

    char* path = g_file_get_path(file);
    if (path) {
        std::string result = path;
        g_free(path);
        if (uri) {
            g_free(uri);
        }
        return result;
    }

    std::string result = uri ? uri : "";
    if (uri) {
        g_free(uri);
    }
    return result;
}

GFile* new_gfile_for_location(const std::string& location) {
    return file_for_location(location);
}

std::string location_from_gfile(GFile* file) {
    return location_from_file(file);
}

static std::string uri_to_path(const std::string& uri) {
    GFile* gfile = g_file_new_for_uri(uri.c_str());
    char* path = g_file_get_path(gfile);
    std::string result = path ? path : "";
    if (path) g_free(path);
    g_object_unref(gfile);
    return result;
}

static std::string path_to_uri(const std::string& path) {
    GFile* gfile = g_file_new_for_path(path.c_str());
    char* uri = g_file_get_uri(gfile);
    std::string result = uri ? uri : "";
    if (uri) g_free(uri);
    g_object_unref(gfile);
    return result;
}

std::string location_to_uri(const std::string& location) {
    GFile* gfile = file_for_location(location);
    char* uri = g_file_get_uri(gfile);
    std::string result = uri ? uri : "";
    if (uri) {
        g_free(uri);
    }
    g_object_unref(gfile);
    return result;
}

std::string location_to_path(const std::string& location) {
    GFile* gfile = file_for_location(location);
    char* path = g_file_get_path(gfile);
    std::string result = path ? path : "";
    if (path) {
        g_free(path);
    }
    g_object_unref(gfile);
    return result;
}

std::string child_location(const std::string& parent, const std::string& child_name) {
    GFile* parent_file = file_for_location(parent);
    GFile* child = g_file_get_child(parent_file, child_name.c_str());
    std::string result = location_from_file(child);
    g_object_unref(child);
    g_object_unref(parent_file);
    return result;
}

bool location_exists(const std::string& location) {
    GFile* gfile = file_for_location(location);
    const bool exists = g_file_query_exists(gfile, NULL);
    g_object_unref(gfile);
    return exists;
}

bool is_directory(const std::string& location) {
    GFile* gfile = file_for_location(location);
    GError* error = NULL;
    GFileInfo* info = g_file_query_info(
        gfile,
        G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NONE,
        NULL,
        &error);
    bool result = false;
    if (info) {
        result = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
        g_object_unref(info);
    }
    if (error) {
        g_error_free(error);
    }
    g_object_unref(gfile);
    return result;
}

bool same_location(const std::string& a, const std::string& b) {
    GFile* fa = file_for_location(a);
    GFile* fb = file_for_location(b);
    const bool same = g_file_equal(fa, fb);
    g_object_unref(fa);
    g_object_unref(fb);
    return same;
}

static std::vector<FavoriteItem> get_default_favorites() {
    std::vector<FavoriteItem> favorites;
    const GUserDirectory dirs[] = {
        G_USER_DIRECTORY_DESKTOP,
        G_USER_DIRECTORY_DOCUMENTS,
        G_USER_DIRECTORY_DOWNLOAD,
        G_USER_DIRECTORY_MUSIC,
        G_USER_DIRECTORY_PICTURES,
        G_USER_DIRECTORY_VIDEOS
    };

    for (GUserDirectory dir : dirs) {
        const char* path = g_get_user_special_dir(dir);
        if (!path) continue;

        struct stat st;
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        std::string uri = path_to_uri(path);
        if (!uri.empty()) {
            favorites.push_back({uri, get_filename(path)});
        }
    }
    return favorites;
}

static void save_favorites(const std::vector<FavoriteItem>& favorites) {
    ensure_bookmarks_dir();
    std::ofstream file(get_bookmarks_path());
    if (!file.is_open()) return;

    for (const auto& fav : favorites) {
        if (fav.uri.empty()) continue;

        std::string basename;
        std::string path = uri_to_path(fav.uri);
        if (!path.empty()) {
            basename = get_filename(path);
        }

        file << fav.uri;
        if (!fav.label.empty() && fav.label != basename) {
            file << " " << fav.label;
        }
        file << "\n";
    }
}

struct AppImageMetadata {
    std::string canonical_path;
    std::string cache_key;
    std::string icon_path;
    std::string desktop_path;
};

static std::string sha256(const std::string& value) {
    gchar* digest = g_compute_checksum_for_string(
        G_CHECKSUM_SHA256,
        value.c_str(),
        static_cast<gssize>(value.size()));
    std::string result = digest ? digest : "";
    g_free(digest);
    return result;
}

static std::string canonical_local_path(const std::string& location) {
    std::string local_path = location_to_path(location);
    if (local_path.empty()) {
        return "";
    }
    gchar* canonical = g_canonicalize_filename(local_path.c_str(), nullptr);
    std::string result = canonical ? canonical : local_path;
    g_free(canonical);
    return result;
}

static std::string appimage_cache_key(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return "";
    }
    std::ostringstream identity;
    identity << path << '\n' << st.st_size << '\n' << st.st_mtime;
    return sha256(identity.str());
}

static std::string desktop_exec_argument(const std::string& value) {
    std::string quoted = "\"";
    for (const char character : value) {
        if (character == '"' ||
            character == '`' ||
            character == '$' ||
            character == '\\') {
            quoted += '\\';
        }
        quoted += character;
    }
    quoted += '"';
    return quoted;
}

static std::string desktop_file_argument_code(GKeyFile* desktop) {
    gchar* raw_exec =
        g_key_file_get_string(desktop, "Desktop Entry", "Exec", nullptr);
    if (!raw_exec) {
        return "";
    }

    const std::string exec = raw_exec;
    g_free(raw_exec);
    size_t selected_position = std::string::npos;
    std::string selected_code;
    for (const char* code : {"%f", "%F", "%u", "%U"}) {
        const size_t position = exec.find(code);
        if (position < selected_position) {
            selected_position = position;
            selected_code = code;
        }
    }
    return selected_code;
}

static bool desktop_list_contains(
    GKeyFile* desktop,
    const char* key,
    const char* expected_value) {
    gsize value_count = 0;
    gchar** values = g_key_file_get_string_list(
        desktop,
        "Desktop Entry",
        key,
        &value_count,
        nullptr);
    if (!values) {
        return false;
    }

    bool found = false;
    for (gsize index = 0; index < value_count; ++index) {
        if (g_strcmp0(values[index], expected_value) == 0) {
            found = true;
            break;
        }
    }
    g_strfreev(values);
    return found;
}

static std::vector<std::string> xfce_helper_categories(GKeyFile* desktop) {
    std::vector<std::string> categories;
    if (desktop_list_contains(desktop, "Categories", "WebBrowser") ||
        desktop_list_contains(desktop, "MimeType", "x-scheme-handler/http") ||
        desktop_list_contains(desktop, "MimeType", "x-scheme-handler/https")) {
        categories.emplace_back("WebBrowser");
    }
    if (desktop_list_contains(desktop, "Categories", "Email") ||
        desktop_list_contains(desktop, "MimeType", "x-scheme-handler/mailto")) {
        categories.emplace_back("MailReader");
    }
    if (desktop_list_contains(desktop, "Categories", "FileManager") ||
        desktop_list_contains(desktop, "MimeType", "inode/directory")) {
        categories.emplace_back("FileManager");
    }
    if (desktop_list_contains(desktop, "Categories", "TerminalEmulator")) {
        categories.emplace_back("TerminalEmulator");
    }
    return categories;
}

static void copy_appimage_helper_names(
    GKeyFile* source,
    GKeyFile* helper) {
    gsize key_count = 0;
    gchar** keys = g_key_file_get_keys(
        source, "Desktop Entry", &key_count, nullptr);
    bool copied_name = false;
    for (gsize index = 0; keys && index < key_count; ++index) {
        const char* key = keys[index];
        if (g_strcmp0(key, "Name") != 0 &&
            !g_str_has_prefix(key, "Name[")) {
            continue;
        }
        gchar* raw_name = g_key_file_get_string(
            source, "Desktop Entry", key, nullptr);
        if (!raw_name || !*raw_name) {
            g_free(raw_name);
            continue;
        }
        const std::string name = std::string(raw_name) + " (AppImage)";
        g_key_file_set_string(helper, "Desktop Entry", key, name.c_str());
        copied_name = copied_name || g_strcmp0(key, "Name") == 0;
        g_free(raw_name);
    }
    g_strfreev(keys);

    if (!copied_name) {
        g_key_file_set_string(
            helper, "Desktop Entry", "Name", "AppImage application");
    }
}

static void remove_stale_xfce_appimage_helpers(
    const std::filesystem::path& helpers_directory) {
    std::error_code filesystem_error;
    for (std::filesystem::directory_iterator iterator(
             helpers_directory, filesystem_error), end;
         !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const std::filesystem::path helper_path = iterator->path();
        const std::string filename = helper_path.filename().string();
        if (!iterator->is_regular_file() ||
            filename.rfind("milopkg-appimage-", 0) != 0 ||
            helper_path.extension() != ".desktop") {
            continue;
        }

        GKeyFile* helper = g_key_file_new();
        if (!g_key_file_load_from_file(
                helper, helper_path.c_str(), G_KEY_FILE_NONE, nullptr)) {
            g_key_file_unref(helper);
            continue;
        }
        gchar* appimage_path = g_key_file_get_string(
            helper, "Desktop Entry", "X-miloOS-AppImage", nullptr);
        const bool appimage_exists = appimage_path &&
            g_file_test(appimage_path, G_FILE_TEST_IS_REGULAR);
        g_free(appimage_path);
        g_key_file_unref(helper);
        if (!appimage_exists) {
            g_remove(helper_path.c_str());
        }
    }
}

static bool remove_xfce_appimage_helpers(
    const std::string& appimage_path,
    const std::string& desktop_id) {
    const std::filesystem::path helpers_directory =
        std::filesystem::path(g_get_user_data_dir()) / "xfce4" / "helpers";
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(helpers_directory, filesystem_error)) {
        return false;
    }

    const std::string canonical_appimage =
        canonical_local_path(appimage_path);
    bool removed = false;
    for (std::filesystem::directory_iterator iterator(
             helpers_directory, filesystem_error), end;
         !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const std::filesystem::path helper_path = iterator->path();
        const std::string filename = helper_path.filename().string();
        if (!iterator->is_regular_file() ||
            filename.rfind("milopkg-appimage-", 0) != 0 ||
            helper_path.extension() != ".desktop") {
            continue;
        }

        GKeyFile* helper = g_key_file_new();
        if (!g_key_file_load_from_file(
                helper, helper_path.c_str(), G_KEY_FILE_NONE, nullptr)) {
            g_key_file_unref(helper);
            continue;
        }
        gchar* raw_appimage = g_key_file_get_string(
            helper, "Desktop Entry", "X-miloOS-AppImage", nullptr);
        gchar* raw_desktop_id = g_key_file_get_string(
            helper, "Desktop Entry", "X-miloOS-DesktopId", nullptr);
        const bool path_matches = !canonical_appimage.empty() && raw_appimage &&
            canonical_local_path(raw_appimage) == canonical_appimage;
        const bool desktop_matches = !desktop_id.empty() &&
            g_strcmp0(raw_desktop_id, desktop_id.c_str()) == 0;
        g_free(raw_appimage);
        g_free(raw_desktop_id);
        g_key_file_unref(helper);
        if ((path_matches || desktop_matches) &&
            g_remove(helper_path.c_str()) == 0) {
            removed = true;
        }
    }
    return removed;
}

static void write_xfce_appimage_helpers(
    GKeyFile* desktop,
    const AppImageMetadata& metadata,
    const std::string& path_key,
    const std::filesystem::path& registered_desktop) {
    const std::filesystem::path helpers_directory =
        std::filesystem::path(g_get_user_data_dir()) / "xfce4" / "helpers";
    if (g_mkdir_with_parents(helpers_directory.c_str(), 0700) != 0) {
        return;
    }

    remove_stale_xfce_appimage_helpers(helpers_directory);
    const std::string helper_prefix = "milopkg-appimage-" + path_key + "-";
    std::error_code filesystem_error;
    for (std::filesystem::directory_iterator iterator(
             helpers_directory, filesystem_error), end;
         !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const std::filesystem::path helper_path = iterator->path();
        if (helper_path.filename().string().rfind(helper_prefix, 0) == 0 &&
            helper_path.extension() == ".desktop") {
            std::filesystem::remove(helper_path, filesystem_error);
            filesystem_error.clear();
        }
    }

    gchar* raw_icon = g_key_file_get_string(
        desktop, "Desktop Entry", "Icon", nullptr);
    const std::string icon = raw_icon ? raw_icon : metadata.icon_path;
    g_free(raw_icon);
    const bool startup_notify = g_key_file_get_boolean(
        desktop, "Desktop Entry", "StartupNotify", nullptr);
    const std::string launch_command =
        "%B --launch-appimage " +
        desktop_exec_argument(metadata.canonical_path);

    for (const std::string& category : xfce_helper_categories(desktop)) {
        GKeyFile* helper = g_key_file_new();
        g_key_file_set_string(helper, "Desktop Entry", "Version", "1.0");
        g_key_file_set_string(
            helper, "Desktop Entry", "Type", "X-XFCE-Helper");
        copy_appimage_helper_names(desktop, helper);
        g_key_file_set_string(
            helper, "Desktop Entry", "Icon", icon.c_str());
        g_key_file_set_boolean(
            helper, "Desktop Entry", "NoDisplay", true);
        g_key_file_set_boolean(
            helper, "Desktop Entry", "StartupNotify", startup_notify);
        g_key_file_set_string(
            helper, "Desktop Entry", "X-XFCE-Binaries", "milofiles;");
        g_key_file_set_string(
            helper, "Desktop Entry", "X-XFCE-Category", category.c_str());
        g_key_file_set_string(
            helper,
            "Desktop Entry",
            "X-XFCE-Commands",
            (launch_command + ";").c_str());
        g_key_file_set_string(
            helper,
            "Desktop Entry",
            "X-XFCE-CommandsWithParameter",
            (launch_command + " \"%s\";").c_str());
        g_key_file_set_string(
            helper,
            "Desktop Entry",
            "X-miloOS-AppImage",
            metadata.canonical_path.c_str());
        g_key_file_set_string(
            helper,
            "Desktop Entry",
            "X-miloOS-DesktopId",
            registered_desktop.filename().c_str());

        gsize helper_length = 0;
        GError* error = nullptr;
        gchar* helper_data =
            g_key_file_to_data(helper, &helper_length, &error);
        const std::filesystem::path helper_path =
            helpers_directory /
            (helper_prefix + category + ".desktop");
        if (helper_data &&
            g_file_set_contents(
                helper_path.c_str(),
                helper_data,
                static_cast<gssize>(helper_length),
                &error)) {
            g_chmod(helper_path.c_str(), 0644);
        }
        g_free(helper_data);
        if (error) {
            g_error_free(error);
        }
        g_key_file_unref(helper);
    }
}

static void refresh_desktop_database(
    const std::filesystem::path& applications_directory) {
    gchar* updater = g_find_program_in_path("update-desktop-database");
    if (!updater) {
        return;
    }
    std::string directory = applications_directory.string();
    gchar* argv[] = {
        updater,
        const_cast<gchar*>(directory.c_str()),
        nullptr
    };
    gint wait_status = 0;
    GError* error = nullptr;
    g_spawn_sync(
        nullptr,
        argv,
        nullptr,
        static_cast<GSpawnFlags>(
            G_SPAWN_STDOUT_TO_DEV_NULL |
            G_SPAWN_STDERR_TO_DEV_NULL),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &wait_status,
        &error);
    if (error) {
        g_error_free(error);
    }
    g_free(updater);
}

static bool appimage_registration_values(
    const std::filesystem::path& desktop_path,
    std::string& appimage_path,
    std::string& package) {
    GKeyFile* desktop = g_key_file_new();
    if (!g_key_file_load_from_file(
            desktop, desktop_path.c_str(), G_KEY_FILE_NONE, nullptr)) {
        g_key_file_unref(desktop);
        return false;
    }
    gchar* raw_appimage = g_key_file_get_string(
        desktop, "Desktop Entry", "X-miloOS-AppImage", nullptr);
    gchar* raw_package = g_key_file_get_string(
        desktop, "Desktop Entry", "X-miloPKG-Package", nullptr);
    appimage_path = raw_appimage ? canonical_local_path(raw_appimage) : "";
    package = raw_package ? to_lower_copy(trim_copy(raw_package)) : "";
    g_free(raw_appimage);
    g_free(raw_package);
    g_key_file_unref(desktop);
    return !appimage_path.empty();
}

void cleanup_appimage_registrations() {
    const std::filesystem::path applications_directory =
        std::filesystem::path(g_get_user_data_dir()) / "applications";
    const std::filesystem::path helpers_directory =
        std::filesystem::path(g_get_user_data_dir()) / "xfce4" / "helpers";
    std::error_code filesystem_error;
    if (!std::filesystem::is_directory(
            applications_directory, filesystem_error)) {
        remove_stale_xfce_appimage_helpers(helpers_directory);
        return;
    }

    bool removed = false;
    for (std::filesystem::directory_iterator iterator(
             applications_directory, filesystem_error), end;
         !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const std::filesystem::path desktop_path = iterator->path();
        const std::string filename = desktop_path.filename().string();
        if (!iterator->is_regular_file() ||
            filename.rfind("milopkg-appimage-", 0) != 0 ||
            desktop_path.extension() != ".desktop") {
            continue;
        }

        std::string appimage_path;
        std::string package;
        const bool valid_registration = appimage_registration_values(
            desktop_path, appimage_path, package);
        const bool appimage_exists = valid_registration &&
            g_file_test(appimage_path.c_str(), G_FILE_TEST_IS_REGULAR);
        if (appimage_exists) {
            continue;
        }

        remove_xfce_appimage_helpers(appimage_path, filename);
        if (g_remove(desktop_path.c_str()) == 0) {
            removed = true;
        }
    }

    if (std::filesystem::is_directory(helpers_directory, filesystem_error)) {
        remove_stale_xfce_appimage_helpers(helpers_directory);
    }
    if (removed) {
        refresh_desktop_database(applications_directory);
    }
}

bool unregister_appimage(const std::string& path) {
    const std::string canonical_path = canonical_local_path(path);
    if (canonical_path.empty()) {
        return false;
    }

    const std::filesystem::path applications_directory =
        std::filesystem::path(g_get_user_data_dir()) / "applications";
    std::error_code filesystem_error;
    bool removed = false;
    if (std::filesystem::is_directory(
            applications_directory, filesystem_error)) {
        for (std::filesystem::directory_iterator iterator(
                 applications_directory, filesystem_error), end;
             !filesystem_error && iterator != end;
             iterator.increment(filesystem_error)) {
            const std::filesystem::path desktop_path = iterator->path();
            const std::string filename = desktop_path.filename().string();
            if (!iterator->is_regular_file() ||
                filename.rfind("milopkg-appimage-", 0) != 0 ||
                desktop_path.extension() != ".desktop") {
                continue;
            }

            std::string registered_path;
            std::string package;
            if (!appimage_registration_values(
                    desktop_path, registered_path, package) ||
                registered_path != canonical_path) {
                continue;
            }
            remove_xfce_appimage_helpers(registered_path, filename);
            if (g_remove(desktop_path.c_str()) == 0) {
                removed = true;
            }
        }
    }
    remove_xfce_appimage_helpers(canonical_path, "");
    if (removed) {
        refresh_desktop_database(applications_directory);
    }
    return removed;
}

static void remove_duplicate_appimage_registrations(
    const std::filesystem::path& applications_directory,
    const std::filesystem::path& registered_desktop,
    const std::string& canonical_path,
    const std::string& package) {
    std::error_code filesystem_error;
    bool removed = false;
    for (std::filesystem::directory_iterator iterator(
             applications_directory, filesystem_error), end;
         !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        const std::filesystem::path desktop_path = iterator->path();
        const std::string filename = desktop_path.filename().string();
        if (desktop_path == registered_desktop ||
            !iterator->is_regular_file() ||
            filename.rfind("milopkg-appimage-", 0) != 0 ||
            desktop_path.extension() != ".desktop") {
            continue;
        }

        std::string other_path;
        std::string other_package;
        if (!appimage_registration_values(
                desktop_path, other_path, other_package)) {
            continue;
        }
        const bool same_path = other_path == canonical_path;
        const bool same_package = !package.empty() && package == other_package;
        if (!same_path && !same_package) {
            continue;
        }
        remove_xfce_appimage_helpers("", filename);
        if (g_remove(desktop_path.c_str()) == 0) {
            removed = true;
        }
    }
    if (removed) {
        refresh_desktop_database(applications_directory);
    }
}

static uint16_t little_endian_u16(const unsigned char* bytes) {
    return static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8);
}

static uint32_t little_endian_u32(const unsigned char* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

static uint64_t little_endian_u64(const unsigned char* bytes) {
    uint64_t value = 0;
    for (int index = 7; index >= 0; --index) {
        value = (value << 8) | bytes[index];
    }
    return value;
}

static uint64_t appimage_squashfs_offset(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return 0;
    }
    input.seekg(0, std::ios::end);
    const uint64_t file_size = static_cast<uint64_t>(input.tellg());
    input.seekg(0, std::ios::beg);

    constexpr size_t CHUNK_SIZE = 1024 * 1024;
    std::array<unsigned char, CHUNK_SIZE + 3> buffer{};
    size_t carry = 0;
    uint64_t consumed = 0;
    while (input) {
        input.read(
            reinterpret_cast<char*>(buffer.data() + carry),
            CHUNK_SIZE);
        const size_t received = static_cast<size_t>(input.gcount());
        const size_t available = carry + received;
        if (available < 4) {
            break;
        }

        const uint64_t base = consumed >= carry ? consumed - carry : 0;
        for (size_t index = 0; index + 48 <= available; ++index) {
            if (buffer[index] != 'h' ||
                buffer[index + 1] != 's' ||
                buffer[index + 2] != 'q' ||
                buffer[index + 3] != 's') {
                continue;
            }
            const uint32_t block_size = little_endian_u32(buffer.data() + index + 12);
            const uint16_t major = little_endian_u16(buffer.data() + index + 28);
            const uint64_t bytes_used = little_endian_u64(buffer.data() + index + 40);
            const uint64_t offset = base + index;
            const bool valid_block_size =
                block_size >= 4096 &&
                block_size <= 1024 * 1024 &&
                (block_size & (block_size - 1)) == 0;
            if (major == 4 &&
                valid_block_size &&
                bytes_used >= 96 &&
                offset + bytes_used <= file_size) {
                return offset;
            }
        }

        carry = std::min<size_t>(47, available);
        std::memmove(
            buffer.data(),
            buffer.data() + available - carry,
            carry);
        consumed += received;
    }
    return 0;
}

static bool extract_appimage_member(
    const std::string& appimage,
    uint64_t squashfs_offset,
    const std::string& member,
    const std::string& working_directory) {
    const std::string offset = std::to_string(squashfs_offset);
    const std::string destination =
        (std::filesystem::path(working_directory) / "squashfs-root").string();
    gchar* argv[] = {
        const_cast<gchar*>("unsquashfs"),
        const_cast<gchar*>("-f"),
        const_cast<gchar*>("-no-progress"),
        const_cast<gchar*>("-o"),
        const_cast<gchar*>(offset.c_str()),
        const_cast<gchar*>("-d"),
        const_cast<gchar*>(destination.c_str()),
        const_cast<gchar*>(appimage.c_str()),
        const_cast<gchar*>(member.c_str()),
        nullptr
    };
    gint wait_status = 0;
    GError* error = nullptr;
    const GSpawnFlags flags = static_cast<GSpawnFlags>(
        G_SPAWN_SEARCH_PATH |
        G_SPAWN_STDOUT_TO_DEV_NULL |
        G_SPAWN_STDERR_TO_DEV_NULL);
    const bool spawned = g_spawn_sync(
        working_directory.c_str(),
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
    if (!spawned) {
        return false;
    }
    const bool succeeded = g_spawn_check_wait_status(wait_status, &error);
    if (error) {
        g_error_free(error);
    }
    return succeeded;
}

static std::string cached_icon_in(const std::filesystem::path& cache_directory) {
    const std::vector<std::string> suffixes = {".svg", ".png", ".xpm"};
    for (const auto& suffix : suffixes) {
        const std::filesystem::path candidate = cache_directory / ("icon" + suffix);
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate.string();
        }
    }
    return "";
}

static AppImageMetadata ensure_appimage_metadata(const std::string& location) {
    namespace fs = std::filesystem;

    AppImageMetadata metadata;
    metadata.canonical_path = canonical_local_path(location);
    if (metadata.canonical_path.empty() || !is_appimage(metadata.canonical_path)) {
        return metadata;
    }
    metadata.cache_key = appimage_cache_key(metadata.canonical_path);
    if (metadata.cache_key.empty()) {
        return {};
    }
    const fs::path cache_directory =
        fs::path(g_get_user_cache_dir()) / "milofiles/appimages" / metadata.cache_key;
    const fs::path cached_desktop = cache_directory / "application.desktop";
    metadata.icon_path = cached_icon_in(cache_directory);
    if (!metadata.icon_path.empty() && fs::is_regular_file(cached_desktop)) {
        metadata.desktop_path = cached_desktop.string();
        return metadata;
    }
    const uint64_t squashfs_offset =
        appimage_squashfs_offset(metadata.canonical_path);
    gchar* unsquashfs = g_find_program_in_path("unsquashfs");
    const bool can_extract = squashfs_offset != 0 && unsquashfs != nullptr;
    g_free(unsquashfs);
    if (!can_extract) {
        return {};
    }

    std::error_code filesystem_error;
    fs::create_directories(cache_directory, filesystem_error);
    if (filesystem_error) {
        return {};
    }

    GError* temporary_error = nullptr;
    gchar* temporary_raw = g_dir_make_tmp("milofiles-appimage-XXXXXX", &temporary_error);
    if (!temporary_raw) {
        if (temporary_error) {
            g_error_free(temporary_error);
        }
        return {};
    }
    const fs::path temporary_directory(temporary_raw);
    g_free(temporary_raw);
    const fs::path extracted_root = temporary_directory / "squashfs-root";

    bool success = extract_appimage_member(
        metadata.canonical_path,
        squashfs_offset,
        ".DirIcon",
        temporary_directory.string());
    fs::path icon_member;
    if (success) {
        const fs::path dir_icon = extracted_root / ".DirIcon";
        if (fs::is_symlink(dir_icon)) {
            icon_member = fs::read_symlink(dir_icon, filesystem_error);
            if (filesystem_error) {
                success = false;
            }
        } else if (fs::is_regular_file(dir_icon)) {
            icon_member = ".DirIcon";
        } else {
            success = false;
        }
    }

    const std::string icon_suffix = to_lower_copy(icon_member.extension().string());
    if (success &&
        (icon_member.is_absolute() ||
         icon_member.has_parent_path() ||
         (icon_suffix != ".svg" && icon_suffix != ".png" && icon_suffix != ".xpm"))) {
        success = false;
    }

    if (success && icon_member != fs::path(".DirIcon")) {
        success = extract_appimage_member(
            metadata.canonical_path,
            squashfs_offset,
            icon_member.generic_string(),
            temporary_directory.string());
    }

    const fs::path extracted_icon = extracted_root / icon_member;
    if (success && fs::is_regular_file(extracted_icon)) {
        const fs::path cached_icon = cache_directory / ("icon" + icon_suffix);
        fs::copy_file(
            extracted_icon,
            cached_icon,
            fs::copy_options::overwrite_existing,
            filesystem_error);
        success = !filesystem_error;
        if (success) {
            metadata.icon_path = cached_icon.string();
        }
    } else {
        success = false;
    }

    if (success) {
        const fs::path desktop_member = icon_member.stem().string() + ".desktop";
        success = extract_appimage_member(
            metadata.canonical_path,
            squashfs_offset,
            desktop_member.generic_string(),
            temporary_directory.string());
        const fs::path extracted_desktop = extracted_root / desktop_member;
        if (success && fs::is_regular_file(extracted_desktop)) {
            filesystem_error.clear();
            fs::copy_file(
                extracted_desktop,
                cached_desktop,
                fs::copy_options::overwrite_existing,
                filesystem_error);
            success = !filesystem_error;
            if (success) {
                metadata.desktop_path = cached_desktop.string();
            }
        } else {
            success = false;
        }
    }

    fs::remove_all(temporary_directory, filesystem_error);
    if (!success) {
        return {};
    }
    return metadata;
}

static std::string normalized_gtk_modules(const char* modules) {
    std::vector<std::string> ordered;
    auto add_module = [&ordered](const std::string& module) {
        if (!module.empty() &&
            std::find(ordered.begin(), ordered.end(), module) == ordered.end()) {
            ordered.push_back(module);
        }
    };
    add_module("appmenu-gtk-module");
    std::stringstream stream(modules ? modules : "");
    std::string module;
    while (std::getline(stream, module, ':')) {
        add_module(module);
    }
    std::string result;
    for (const auto& item : ordered) {
        if (!result.empty()) {
            result += ":";
        }
        result += item;
    }
    return result;
}

std::string format_size(int64_t size) {
    if (size < 0) return "";
    
    double size_d = static_cast<double>(size);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);
    
    if (size < 1024) {
        return std::to_string(size) + " B";
    } else if (size < 1024 * 1024) {
        ss << (size_d / 1024.0);
        return ss.str() + " KB";
    } else if (size < 1024 * 1024 * 1024) {
        ss << (size_d / (1024.0 * 1024.0));
        return ss.str() + " MB";
    } else {
        ss << (size_d / (1024.0 * 1024.0 * 1024.0));
        return ss.str() + " GB";
    }
}

std::string get_mime_type(const std::string& path) {
    GFile* gfile = file_for_location(path);
    GError* error = NULL;
    GFileInfo* info = g_file_query_info(gfile, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, NULL, &error);
    std::string mime = "application/octet-stream";
    if (info) {
        const char* ct = g_file_info_get_content_type(info);
        if (ct) {
            mime = ct;
        }
        g_object_unref(info);
    } else if (error) {
        g_error_free(error);
    }
    g_object_unref(gfile);
    return mime;
}

enum class PreferredContentHandler {
    None,
    Vlc,
    Ristretto
};

static PreferredContentHandler preferred_content_handler(
    const std::string& path) {
    const std::string filename = get_filename(path);
    const size_t dot_position = filename.find_last_of('.');
    const std::string extension = dot_position == std::string::npos
        ? ""
        : to_lower_copy(filename.substr(dot_position));
    const std::string mime = get_mime_type(path);
    const std::unordered_set<std::string> audio_extensions = {
        ".mp3", ".wav", ".ogg", ".flac", ".m4a", ".aac",
        ".wma", ".opus", ".mid", ".midi", ".mka"
    };
    const std::unordered_set<std::string> video_extensions = {
        ".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv",
        ".webm", ".mpeg", ".mpg", ".m4v"
    };
    const std::unordered_set<std::string> image_extensions = {
        ".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp",
        ".svg", ".tiff", ".ico"
    };

    if (audio_extensions.count(extension) > 0 ||
        video_extensions.count(extension) > 0 ||
        mime.rfind("audio/", 0) == 0 ||
        mime.rfind("video/", 0) == 0) {
        return PreferredContentHandler::Vlc;
    }
    if (image_extensions.count(extension) > 0 ||
        mime.rfind("image/", 0) == 0) {
        return PreferredContentHandler::Ristretto;
    }
    return PreferredContentHandler::None;
}

static bool app_info_executable_exists(GAppInfo* app_info) {
    const char* executable = g_app_info_get_executable(app_info);
    if (!executable || !*executable) {
        return false;
    }
    if (g_path_is_absolute(executable)) {
        return g_file_test(executable, G_FILE_TEST_IS_EXECUTABLE);
    }
    gchar* resolved = g_find_program_in_path(executable);
    const bool exists = resolved != nullptr;
    g_free(resolved);
    return exists;
}

static GAppInfo* desktop_app_info_for_id(const std::string& desktop_id) {
    GDesktopAppInfo* desktop_info =
        g_desktop_app_info_new(desktop_id.c_str());
    if (!desktop_info) {
        return nullptr;
    }
    if (!app_info_executable_exists(G_APP_INFO(desktop_info))) {
        g_object_unref(desktop_info);
        return nullptr;
    }
    return G_APP_INFO(desktop_info);
}

static GAppInfo* registered_appimage_for_package(
    const std::string& package) {
    namespace fs = std::filesystem;

    const fs::path applications =
        fs::path(g_get_user_data_dir()) / "applications";
    std::error_code filesystem_error;
    if (!fs::is_directory(applications, filesystem_error)) {
        return nullptr;
    }

    std::vector<fs::path> desktop_paths;
    for (fs::directory_iterator iterator(applications, filesystem_error), end;
         !filesystem_error && iterator != end;
         iterator.increment(filesystem_error)) {
        if (iterator->is_regular_file(filesystem_error) &&
            iterator->path().filename().string().rfind(
                "milopkg-appimage-", 0) == 0 &&
            iterator->path().extension() == ".desktop") {
            desktop_paths.push_back(iterator->path());
        }
    }
    std::sort(desktop_paths.begin(), desktop_paths.end());

    for (const auto& desktop_path : desktop_paths) {
        GKeyFile* desktop = g_key_file_new();
        if (!g_key_file_load_from_file(
                desktop,
                desktop_path.c_str(),
                G_KEY_FILE_NONE,
                nullptr)) {
            g_key_file_unref(desktop);
            continue;
        }

        gchar* raw_package = g_key_file_get_string(
            desktop, "Desktop Entry", "X-miloPKG-Package", nullptr);
        gchar* raw_appimage = g_key_file_get_string(
            desktop, "Desktop Entry", "X-miloOS-AppImage", nullptr);
        const std::string packaged = raw_package
            ? to_lower_copy(raw_package)
            : "";
        const std::string appimage = raw_appimage ? raw_appimage : "";
        g_free(raw_package);
        g_free(raw_appimage);
        g_key_file_unref(desktop);

        if (packaged != package &&
            packaged.rfind(package + ":", 0) != 0) {
            continue;
        }
        if (appimage.empty() ||
            !g_file_test(appimage.c_str(), G_FILE_TEST_IS_EXECUTABLE)) {
            continue;
        }

        GDesktopAppInfo* desktop_info =
            g_desktop_app_info_new_from_filename(desktop_path.c_str());
        if (!desktop_info) {
            continue;
        }
        if (!app_info_executable_exists(G_APP_INFO(desktop_info))) {
            g_object_unref(desktop_info);
            continue;
        }
        return G_APP_INFO(desktop_info);
    }
    return nullptr;
}

static GAppInfo* preferred_app_info(PreferredContentHandler handler) {
    std::string package;
    std::vector<std::string> desktop_ids;
    if (handler == PreferredContentHandler::Vlc) {
        package = "vlc";
        desktop_ids = {"vlc.desktop", "org.videolan.VLC.desktop"};
    } else if (handler == PreferredContentHandler::Ristretto) {
        package = "ristretto";
        desktop_ids = {
            "org.xfce.ristretto.desktop",
            "ristretto.desktop"
        };
    } else {
        return nullptr;
    }

    for (const auto& desktop_id : desktop_ids) {
        GAppInfo* app_info = desktop_app_info_for_id(desktop_id);
        if (app_info) {
            return app_info;
        }
    }
    return registered_appimage_for_package(package);
}

std::string get_custom_default_command(const std::string& path) {
    std::string filename = get_filename(path);
    std::string ext = "";
    size_t dot_pos = filename.find_last_of('.');
    if (dot_pos != std::string::npos) {
        ext = filename.substr(dot_pos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    
    std::string mime = get_mime_type(path);
    
    auto which = [](const std::string& cmd) -> bool {
        char* p = g_find_program_in_path(cmd.c_str());
        if (p) {
            g_free(p);
            return true;
        }
        return false;
    };
    
    // 1. PDF files: open with firefox or chrome
    if (ext == ".pdf" || mime == "application/pdf") {
        for (const char* browser : {"firefox", "google-chrome", "chrome", "chromium"}) {
            if (which(browser)) return browser;
        }
    }
    
    const PreferredContentHandler preferred_handler =
        preferred_content_handler(path);
    if (preferred_handler == PreferredContentHandler::Vlc) {
        if (which("vlc")) return "vlc";
    }
    if (preferred_handler == PreferredContentHandler::Ristretto) {
        if (which("ristretto")) return "ristretto";
    }
    
    // 5. Text files: mousepad
    const std::vector<std::string> text_extensions = {".txt", ".md", ".py", ".sh", ".json", ".xml", ".cfg", ".conf", ".ini", ".yaml", ".yml", ".log", ".js", ".css", ".html", ".c", ".cpp", ".h", ".hpp", ".java", ".go", ".rs"};
    if (std::find(text_extensions.begin(), text_extensions.end(), ext) != text_extensions.end() || 
        mime.rfind("text/", 0) == 0 || 
        mime == "application/x-shellscript" || 
        mime == "application/javascript" || 
        mime == "application/json") {
        if (which("mousepad")) return "mousepad";
    }
    
    return "";
}

bool is_appimage(const std::string& location) {
    const std::string path = canonical_local_path(location);
    if (path.empty()) {
        return false;
    }
    std::string filename = to_lower_copy(get_filename(path));
    if (filename.size() < 9 ||
        filename.compare(filename.size() - 9, 9, ".appimage") != 0) {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    unsigned char header[11] = {};
    input.read(reinterpret_cast<char*>(header), sizeof(header));
    return input.gcount() == static_cast<std::streamsize>(sizeof(header)) &&
        header[0] == 0x7f &&
        header[1] == 'E' &&
        header[2] == 'L' &&
        header[3] == 'F' &&
        header[8] == 'A' &&
        header[9] == 'I' &&
        header[10] == 2;
}

std::string appimage_icon_path(const std::string& path) {
    return ensure_appimage_metadata(path).icon_path;
}

std::string register_appimage(const std::string& path) {
    cleanup_appimage_registrations();
    const AppImageMetadata metadata = ensure_appimage_metadata(path);
    if (metadata.canonical_path.empty() ||
        metadata.desktop_path.empty() ||
        metadata.icon_path.empty()) {
        return "";
    }

    GKeyFile* desktop = g_key_file_new();
    GError* error = nullptr;
    if (!g_key_file_load_from_file(
            desktop,
            metadata.desktop_path.c_str(),
            static_cast<GKeyFileFlags>(
                G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS),
            &error)) {
        if (error) {
            g_error_free(error);
        }
        g_key_file_unref(desktop);
        return "";
    }

    const std::string path_key = sha256(metadata.canonical_path);
    const std::filesystem::path applications_directory =
        std::filesystem::path(g_get_user_data_dir()) / "applications";
    std::error_code filesystem_error;
    std::filesystem::create_directories(applications_directory, filesystem_error);
    if (filesystem_error) {
        g_key_file_unref(desktop);
        return "";
    }
    const std::filesystem::path registered_desktop =
        applications_directory / ("milopkg-appimage-" + path_key + ".desktop");

    if (!g_key_file_has_key(desktop, "Desktop Entry", "Name", nullptr)) {
        std::string name = get_filename(metadata.canonical_path);
        if (to_lower_copy(name).size() > 9) {
            name.resize(name.size() - 9);
        }
        g_key_file_set_string(desktop, "Desktop Entry", "Name", name.c_str());
    }
    const std::string file_argument_code =
        desktop_file_argument_code(desktop);
    std::string launch_command =
        "milofiles --launch-appimage " +
        desktop_exec_argument(metadata.canonical_path);
    if (!file_argument_code.empty()) {
        launch_command += " " + file_argument_code;
    }
    g_key_file_set_string(desktop, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(
        desktop, "Desktop Entry", "Exec", launch_command.c_str());
    g_key_file_set_string(
        desktop, "Desktop Entry", "TryExec", "milofiles");
    g_key_file_set_string(desktop, "Desktop Entry", "Icon", metadata.icon_path.c_str());
    g_key_file_set_boolean(desktop, "Desktop Entry", "Hidden", false);
    g_key_file_set_boolean(desktop, "Desktop Entry", "NoDisplay", false);
    g_key_file_set_boolean(desktop, "Desktop Entry", "DBusActivatable", false);
    g_key_file_remove_key(desktop, "Desktop Entry", "OnlyShowIn", nullptr);
    g_key_file_remove_key(desktop, "Desktop Entry", "NotShowIn", nullptr);
    g_key_file_set_string(
        desktop, "Desktop Entry", "X-miloOS-AppImage", metadata.canonical_path.c_str());
    g_key_file_set_string(
        desktop, "Desktop Entry", "X-miloOS-AppImage-CacheKey", metadata.cache_key.c_str());
    gchar* raw_package = g_key_file_get_string(
        desktop, "Desktop Entry", "X-miloPKG-Package", nullptr);
    const std::string package =
        raw_package ? to_lower_copy(trim_copy(raw_package)) : "";
    g_free(raw_package);

    gsize desktop_length = 0;
    gchar* desktop_data = g_key_file_to_data(desktop, &desktop_length, &error);
    bool written = desktop_data &&
        g_file_set_contents(
            registered_desktop.c_str(),
            desktop_data,
            static_cast<gssize>(desktop_length),
            &error);
    g_free(desktop_data);
    if (error) {
        g_error_free(error);
    }
    if (!written) {
        g_key_file_unref(desktop);
        return "";
    }
    g_chmod(registered_desktop.c_str(), 0644);
    write_xfce_appimage_helpers(
        desktop, metadata, path_key, registered_desktop);
    g_key_file_unref(desktop);
    remove_duplicate_appimage_registrations(
        applications_directory,
        registered_desktop,
        metadata.canonical_path,
        package);
    refresh_desktop_database(applications_directory);
    return registered_desktop.string();
}

bool run_appimage(
    const std::string& path,
    const std::vector<std::string>& arguments) {
    const std::string canonical_path = canonical_local_path(path);
    if (canonical_path.empty() || !is_appimage(canonical_path)) {
        return false;
    }

    std::vector<std::string> command = {canonical_path};
    command.insert(command.end(), arguments.begin(), arguments.end());
    gchar** spawn_argv = g_new0(gchar*, command.size() + 1);
    for (size_t index = 0; index < command.size(); ++index) {
        spawn_argv[index] = g_strdup(command[index].c_str());
    }

    gchar** spawn_environment = g_get_environ();
    const std::string gtk_modules =
        normalized_gtk_modules(g_getenv("GTK_MODULES"));
    spawn_environment = g_environ_setenv(
        spawn_environment, "GTK_MODULES", gtk_modules.c_str(), TRUE);
    spawn_environment = g_environ_setenv(
        spawn_environment, "UBUNTU_MENUPROXY", "1", TRUE);
    spawn_environment = g_environ_setenv(
        spawn_environment,
        "MILO_APPIMAGE_PATH",
        canonical_path.c_str(),
        TRUE);

    GError* error = nullptr;
    const bool launched = g_spawn_async(
        nullptr,
        spawn_argv,
        spawn_environment,
        G_SPAWN_SEARCH_PATH,
        nullptr,
        nullptr,
        nullptr,
        &error);
    if (error) {
        g_error_free(error);
    }
    g_strfreev(spawn_environment);
    for (size_t index = 0; index < command.size(); ++index) {
        g_free(spawn_argv[index]);
    }
    g_free(spawn_argv);
    return launched;
}

bool launch_appimage(const std::string& path) {
    const std::string desktop_path = register_appimage(path);
    if (desktop_path.empty()) {
        return false;
    }

    GDesktopAppInfo* app_info =
        g_desktop_app_info_new_from_filename(desktop_path.c_str());
    if (!app_info) {
        return false;
    }
    GAppLaunchContext* context = g_app_launch_context_new();
    const std::string gtk_modules = normalized_gtk_modules(g_getenv("GTK_MODULES"));
    g_app_launch_context_setenv(context, "GTK_MODULES", gtk_modules.c_str());
    g_app_launch_context_setenv(context, "UBUNTU_MENUPROXY", "1");
    g_app_launch_context_setenv(context, "MILO_APPIMAGE_PATH", path.c_str());
    GError* error = nullptr;
    const bool launched =
        g_app_info_launch(G_APP_INFO(app_info), nullptr, context, &error);
    if (error) {
        g_error_free(error);
    }
    g_object_unref(context);
    g_object_unref(app_info);
    return launched;
}

bool open_file(const std::string& path) {
    return open_files({path});
}

bool open_files(const std::vector<std::string>& paths) {
    if (paths.empty()) return false;

    struct CommandGroup {
        std::string command;
        std::vector<std::string> paths;
    };
    struct AppGroup {
        GAppInfo* app = nullptr;
        std::vector<std::string> locations;
        bool strict = false;
    };

    std::vector<CommandGroup> command_groups;
    std::vector<AppGroup> app_groups;
    std::vector<std::string> default_locations;
    GAppInfo* vlc_app =
        preferred_app_info(PreferredContentHandler::Vlc);
    GAppInfo* ristretto_app =
        preferred_app_info(PreferredContentHandler::Ristretto);
    bool success = true;

    auto add_app_location = [&app_groups](
        GAppInfo* app,
        const std::string& location,
        bool strict) {
        auto group = std::find_if(
            app_groups.begin(), app_groups.end(),
            [app](const AppGroup& candidate) {
                return g_app_info_equal(candidate.app, app);
            });
        if (group == app_groups.end()) {
            app_groups.push_back({
                G_APP_INFO(g_object_ref(app)),
                {location},
                strict
            });
        } else {
            group->locations.push_back(location);
            group->strict = group->strict || strict;
        }
    };

    for (const auto& location : paths) {
        const std::string local_path = location_to_path(location);
        if (!local_path.empty() && is_appimage(local_path)) {
            if (!launch_appimage(local_path)) {
                success = false;
            }
            continue;
        }
        const std::string command = get_custom_default_command(location);
        if (!command.empty() && !local_path.empty()) {
            auto group = std::find_if(
                command_groups.begin(), command_groups.end(),
                [&command](const CommandGroup& candidate) {
                    return candidate.command == command;
                });
            if (group == command_groups.end()) {
                command_groups.push_back({command, {local_path}});
            } else {
                group->paths.push_back(local_path);
            }
            continue;
        }

        const PreferredContentHandler preferred_handler =
            preferred_content_handler(location);
        GAppInfo* preferred_app = nullptr;
        if (preferred_handler == PreferredContentHandler::Vlc) {
            preferred_app = vlc_app;
        } else if (
            preferred_handler == PreferredContentHandler::Ristretto) {
            preferred_app = ristretto_app;
        }
        if (preferred_app) {
            add_app_location(preferred_app, location, true);
        } else {
            default_locations.push_back(location);
        }
    }

    for (const auto& group : command_groups) {
        std::vector<std::string> argv = {group.command};
        if (group.command == "vlc") {
            // Match file-manager activation semantics while making reuse
            // independent from the user's current VLC preference.
            argv.push_back("--one-instance");
            argv.push_back("--started-from-file");
        }
        argv.insert(argv.end(), group.paths.begin(), group.paths.end());
        if (!run_command_async(argv)) success = false;
    }

    std::vector<std::string> fallback_locations;
    for (const auto& location : default_locations) {
        GFile* file = file_for_location(location);
        GError* error = nullptr;
        GAppInfo* app = g_file_query_default_handler(file, nullptr, &error);
        g_object_unref(file);
        if (error) g_error_free(error);

        if (!app) {
            fallback_locations.push_back(location);
            continue;
        }
        add_app_location(app, location, false);
        g_object_unref(app);
    }

    for (auto& group : app_groups) {
        GList* files = nullptr;
        for (auto location = group.locations.rbegin(); location != group.locations.rend(); ++location) {
            files = g_list_prepend(files, file_for_location(*location));
        }

        GError* error = nullptr;
        if (!g_app_info_launch(group.app, files, nullptr, &error)) {
            if (group.strict) {
                success = false;
            } else {
                fallback_locations.insert(
                    fallback_locations.end(),
                    group.locations.begin(),
                    group.locations.end());
            }
        }
        if (error) g_error_free(error);
        g_list_free_full(files, g_object_unref);
        g_object_unref(group.app);
    }
    if (vlc_app) {
        g_object_unref(vlc_app);
    }
    if (ristretto_app) {
        g_object_unref(ristretto_app);
    }

    for (const auto& location : fallback_locations) {
        const std::string local_path = location_to_path(location);
        if (!run_command_async({"xdg-open", local_path.empty() ? location : local_path})) {
            success = false;
        }
    }

    return success;
}

bool is_dangerous_archive_path(const std::string& path) {
    // Check for ".." or absolute paths starting with "/" inside archives to prevent directory traversal
    if (path.find("../") != std::string::npos || path.find("..\\") != std::string::npos) {
        return true;
    }
    return false;
}

std::string trash_location() {
    return "trash:///";
}

bool is_trash_location(const std::string& location) {
    if (location.empty()) return false;

    GFile* file = file_for_location(location);
    GFile* trash = g_file_new_for_uri(trash_location().c_str());
    bool matches = g_file_equal(file, trash) ||
        g_file_has_prefix(file, trash);
    g_object_unref(trash);

    if (!matches) {
        const std::filesystem::path local_trash =
            std::filesystem::path(g_get_user_data_dir()) / "Trash/files";
        GFile* local = g_file_new_for_path(local_trash.c_str());
        matches = g_file_equal(file, local) ||
            g_file_has_prefix(file, local);
        g_object_unref(local);
    }

    g_object_unref(file);
    return matches;
}

bool empty_trash() {
    bool success = true;

    GFile* trash = g_file_new_for_uri(trash_location().c_str());
    GError* error = nullptr;
    GFileEnumerator* enumerator = g_file_enumerate_children(
        trash,
        G_FILE_ATTRIBUTE_STANDARD_NAME,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        nullptr,
        &error);
    if (enumerator) {
        GFileInfo* info = nullptr;
        while ((info = g_file_enumerator_next_file(
                    enumerator,
                    nullptr,
                    &error)) != nullptr) {
            const char* name = g_file_info_get_name(info);
            GFile* child = name ? g_file_get_child(trash, name) : nullptr;
            if (child) {
                GError* delete_error = nullptr;
                if (!g_file_delete(child, nullptr, &delete_error)) {
                    success = false;
                }
                if (delete_error) g_error_free(delete_error);
                g_object_unref(child);
            }
            g_object_unref(info);
        }
        if (error) {
            success = false;
            g_error_free(error);
            error = nullptr;
        }
        g_object_unref(enumerator);
    } else if (error) {
        g_error_free(error);
        error = nullptr;
    }
    g_object_unref(trash);

    const std::filesystem::path trash_root =
        std::filesystem::path(g_get_user_data_dir()) / "Trash";
    const std::filesystem::path files_path = trash_root / "files";
    const std::filesystem::path info_path = trash_root / "info";
    std::unordered_set<std::string> failed_entries;
    std::error_code filesystem_error;

    if (std::filesystem::is_directory(files_path, filesystem_error)) {
        std::filesystem::directory_iterator iterator(
            files_path,
            filesystem_error);
        const std::filesystem::directory_iterator end;
        while (!filesystem_error && iterator != end) {
            const std::filesystem::path entry = iterator->path();
            const std::string name = entry.filename().string();
            iterator.increment(filesystem_error);
            if (!delete_path_recursive(entry.string())) {
                failed_entries.insert(name);
                success = false;
            }
        }
        if (filesystem_error) success = false;
    }

    filesystem_error.clear();
    if (std::filesystem::is_directory(info_path, filesystem_error)) {
        std::filesystem::directory_iterator iterator(
            info_path,
            filesystem_error);
        const std::filesystem::directory_iterator end;
        while (!filesystem_error && iterator != end) {
            const std::filesystem::path entry = iterator->path();
            std::string data_name = entry.filename().string();
            iterator.increment(filesystem_error);
            constexpr const char* suffix = ".trashinfo";
            if (data_name.size() > std::strlen(suffix) &&
                data_name.compare(
                    data_name.size() - std::strlen(suffix),
                    std::strlen(suffix),
                    suffix) == 0) {
                data_name.resize(data_name.size() - std::strlen(suffix));
            }
            if (failed_entries.find(data_name) != failed_entries.end()) {
                continue;
            }
            if (!delete_path_recursive(entry.string())) {
                success = false;
            }
        }
        if (filesystem_error) success = false;
    }

    return success;
}

std::string get_bookmarks_path() {
    return std::string(g_get_user_config_dir()) + "/gtk-3.0/bookmarks";
}

std::vector<FavoriteItem> get_favorites() {
    std::vector<FavoriteItem> favorites;
    std::string path = get_bookmarks_path();
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || st.st_size == 0) {
        favorites = get_default_favorites();
        save_favorites(favorites);
        return favorites;
    }

    std::ifstream file(path);
    if (!file.is_open()) return favorites;
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::stringstream ss(line);
        std::string uri;
        ss >> uri;
        
        std::string label;
        std::string word;
        while (ss >> word) {
            if (!label.empty()) label += " ";
            label += word;
        }
        
        if (label.empty()) {
            GFile* gfile = g_file_new_for_uri(uri.c_str());
            char* basename = g_file_get_basename(gfile);
            if (basename) {
                label = basename;
                g_free(basename);
            } else {
                label = uri;
            }
            g_object_unref(gfile);
        }
        
        favorites.push_back({uri, label});
    }
    return favorites;
}

bool add_favorite(const std::string& path, const std::string& label) {
    GFile* gfile = file_for_location(path);

    char* uri = g_file_get_uri(gfile);
    char* local_path = g_file_get_path(gfile);
    if (!uri) {
        if (local_path) g_free(local_path);
        g_object_unref(gfile);
        return false;
    }
    
    std::vector<FavoriteItem> favorites = get_favorites();
    for (const auto& fav : favorites) {
        if (fav.uri == uri) {
            g_free(uri);
            if (local_path) g_free(local_path);
            g_object_unref(gfile);
            return true; // Already exists
        }
    }
    
    ensure_bookmarks_dir();
    std::string bookmarks_path = get_bookmarks_path();
    std::ofstream file(bookmarks_path, std::ios::app);
    if (!file.is_open()) {
        g_free(uri);
        if (local_path) g_free(local_path);
        g_object_unref(gfile);
        return false;
    }
    
    std::string final_label = label;
    if (final_label.empty()) {
        char* basename = g_file_get_basename(gfile);
        final_label = basename ? basename : "";
        if (basename) g_free(basename);
    }
    
    file << uri << " " << final_label << "\n";
    g_free(uri);
    if (local_path) g_free(local_path);
    g_object_unref(gfile);
    return true;
}

bool remove_favorite(const std::string& uri) {
    std::vector<FavoriteItem> favorites = get_favorites();
    std::string bookmarks_path = get_bookmarks_path();
    std::ofstream file(bookmarks_path);
    if (!file.is_open()) return false;
    
    for (const auto& fav : favorites) {
        if (fav.uri != uri) {
            file << fav.uri << " " << fav.label << "\n";
        }
    }
    return true;
}

bool rename_favorite(const std::string& uri, const std::string& new_label) {
    std::vector<FavoriteItem> favorites = get_favorites();
    std::string bookmarks_path = get_bookmarks_path();
    std::ofstream file(bookmarks_path);
    if (!file.is_open()) return false;
    
    for (auto& fav : favorites) {
        if (fav.uri == uri) {
            fav.label = new_label;
        }
        file << fav.uri << " " << fav.label << "\n";
    }
    return true;
}

bool reorder_favorite(
    const std::string& uri,
    const std::string& target_uri,
    bool after) {
    if (uri.empty() || target_uri.empty() || uri == target_uri) {
        return false;
    }
    std::vector<FavoriteItem> favorites = get_favorites();
    auto source = std::find_if(
        favorites.begin(), favorites.end(),
        [&uri](const FavoriteItem& item) { return item.uri == uri; });
    auto target = std::find_if(
        favorites.begin(), favorites.end(),
        [&target_uri](const FavoriteItem& item) { return item.uri == target_uri; });
    if (source == favorites.end() || target == favorites.end()) {
        return false;
    }

    FavoriteItem moved = *source;
    favorites.erase(source);
    target = std::find_if(
        favorites.begin(), favorites.end(),
        [&target_uri](const FavoriteItem& item) { return item.uri == target_uri; });
    if (target == favorites.end()) {
        return false;
    }
    if (after) {
        ++target;
    }
    favorites.insert(target, std::move(moved));
    save_favorites(favorites);
    return true;
}

std::string get_file_type_description(const std::string& path, bool is_dir) {
    if (is_dir) return i18n::_("folder");
    if (is_appimage(path)) return i18n::_("appimage_application");
    
    GFile* gfile = file_for_location(path);
    GFileInfo* info = g_file_query_info(gfile, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
    std::string desc = i18n::_("file");
    if (info) {
        const char* ct = g_file_info_get_content_type(info);
        if (ct) {
            char* native_desc = g_content_type_get_description(ct);
            if (native_desc) {
                desc = native_desc;
                g_free(native_desc);
            }
        }
        g_object_unref(info);
    }
    g_object_unref(gfile);
    return desc;
}

std::string get_file_modification_time(const std::string& path) {
    struct stat attr;
    if (stat(path.c_str(), &attr) == 0) {
        struct tm* timeinfo = localtime(&attr.st_mtime);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
        return std::string(buffer);
    }
    return "";
}

bool run_command_async(const std::vector<std::string>& argv) {
    if (argv.empty()) return false;

    gchar** spawn_argv = g_new0(gchar*, argv.size() + 1);
    for (size_t i = 0; i < argv.size(); ++i) {
        spawn_argv[i] = g_strdup(argv[i].c_str());
    }

    gchar** spawn_environment = g_get_environ();
    const char* inherited_identity_variables[] = {
        "GIO_LAUNCHED_DESKTOP_FILE",
        "GIO_LAUNCHED_DESKTOP_FILE_PID",
        "DESKTOP_STARTUP_ID",
        "MILO_APPIMAGE_PATH",
        "APPIMAGE",
        nullptr
    };
    for (const char** variable = inherited_identity_variables; *variable; ++variable) {
        spawn_environment = g_environ_unsetenv(spawn_environment, *variable);
    }

    GError* error = NULL;
    bool success = g_spawn_async(
        NULL,
        spawn_argv,
        spawn_environment,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        NULL,
        &error);
    if (error) {
        g_error_free(error);
    }

    g_strfreev(spawn_environment);
    for (size_t i = 0; i < argv.size(); ++i) {
        g_free(spawn_argv[i]);
    }
    g_free(spawn_argv);
    return success;
}

bool make_directory(const std::string& path) {
    GFile* gfile = file_for_location(path);
    GError* error = NULL;
    bool success = g_file_make_directory(gfile, NULL, &error);
    if (error) {
        g_error_free(error);
    }
    g_object_unref(gfile);
    return success;
}

bool create_empty_file(const std::string& path) {
    GFile* gfile = file_for_location(path);
    GError* error = NULL;
    GFileOutputStream* stream = g_file_create(gfile, G_FILE_CREATE_NONE, NULL, &error);
    bool success = false;
    if (stream) {
        g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, NULL);
        g_object_unref(stream);
        success = true;
    }
    if (error) {
        g_error_free(error);
    }
    g_object_unref(gfile);
    return success;
}

std::string get_parent_directory(const std::string& path) {
    GFile* gfile = file_for_location(path);
    GFile* parent = g_file_get_parent(gfile);
    std::string parent_path = "";
    if (parent) {
        parent_path = location_from_file(parent);
        g_object_unref(parent);
    }
    g_object_unref(gfile);
    return parent_path;
}

std::string get_filename(const std::string& path) {
    GFile* gfile = file_for_location(path);
    char* basename = g_file_get_basename(gfile);
    std::string filename = "";
    if (basename) {
        filename = basename;
        g_free(basename);
    }
    g_object_unref(gfile);
    return filename;
}

bool delete_path_recursive(const std::string& path, GCancellable* cancellable) {
    if (cancellable && g_cancellable_is_cancelled(cancellable)) return false;

    GFile* gfile = file_for_location(path);
    GError* error = NULL;
    GFileInfo* info = g_file_query_info(
        gfile,
        G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        cancellable,
        &error);

    bool success = false;
    if (info) {
        if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
            GFileEnumerator* enumerator = g_file_enumerate_children(
                gfile,
                G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                cancellable,
                &error);
            success = true;
            if (enumerator) {
                GFileInfo* child_info = NULL;
                while ((child_info = g_file_enumerator_next_file(enumerator, cancellable, &error)) != NULL) {
                    const char* child_name = g_file_info_get_name(child_info);
                    GFile* child = child_name ? g_file_get_child(gfile, child_name) : NULL;
                    if (child) {
                        std::string child_location_str = location_from_file(child);
                        if (!delete_path_recursive(child_location_str, cancellable)) {
                            success = false;
                        }
                        g_object_unref(child);
                    }
                    g_object_unref(child_info);
                    if (error) {
                        success = false;
                        g_error_free(error);
                        error = NULL;
                        break;
                    }
                }
                g_object_unref(enumerator);
            } else {
                success = false;
            }
        } else {
            success = true;
        }
        g_object_unref(info);
    } else {
        if (error) {
            g_error_free(error);
            error = NULL;
        }
        success = true;
    }

    if (error) {
        g_error_free(error);
        error = NULL;
    }

    if (success) {
        success = g_file_delete(gfile, cancellable, &error);
        if (error) {
            g_error_free(error);
        }
    }

    g_object_unref(gfile);
    return success;
}

std::string get_free_space_description(const std::string& path) {
    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) == 0) {
        int64_t free_bytes = static_cast<int64_t>(stat.f_bavail) * stat.f_frsize;
        return format_size(free_bytes);
    }
    return "";
}

std::string normalize_path(const std::string& path) {
    std::string expanded = trim_copy(path);
    if (expanded.empty()) return "";

    if (has_uri_scheme(expanded) && expanded.rfind("file://", 0) != 0) {
        return expanded;
    }

    if (expanded.rfind("file://", 0) == 0) {
        gchar* local_path = g_filename_from_uri(expanded.c_str(), NULL, NULL);
        if (local_path) {
            expanded = local_path;
            g_free(local_path);
        }
    }

    const char* home = g_get_home_dir();
    if (expanded == "~") {
        expanded = home;
    } else if (expanded.rfind("~/", 0) == 0) {
        expanded = std::string(home) + expanded.substr(1);
    }

    try {
        return std::filesystem::weakly_canonical(expanded).string();
    } catch (...) {
        try {
            return std::filesystem::absolute(expanded).string();
        } catch (...) {
            return expanded;
        }
    }
}

void copy_path_recursive(const std::string& src, const std::string& dest, GCancellable* cancellable) {
    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        throw std::runtime_error("Operation cancelled.");
    }

    GFile* src_file = file_for_location(src);
    GFile* dest_file = file_for_location(dest);
    GError* error = NULL;
    GFileInfo* info = g_file_query_info(
        src_file,
        G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
        cancellable,
        &error);

    if (!info) {
        std::string message = error ? error->message : "Could not query source.";
        if (error) g_error_free(error);
        g_object_unref(src_file);
        g_object_unref(dest_file);
        throw std::runtime_error(message);
    }

    if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
        if (!g_file_query_exists(dest_file, cancellable)) {
            if (!g_file_make_directory(dest_file, cancellable, &error)) {
                std::string message = error ? error->message : "Could not create destination folder.";
                if (error) g_error_free(error);
                g_object_unref(info);
                g_object_unref(src_file);
                g_object_unref(dest_file);
                throw std::runtime_error(message);
            }
        }

        GFileEnumerator* enumerator = g_file_enumerate_children(
            src_file,
            G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            cancellable,
            &error);
        if (!enumerator) {
            std::string message = error ? error->message : "Could not enumerate source folder.";
            if (error) g_error_free(error);
            g_object_unref(info);
            g_object_unref(src_file);
            g_object_unref(dest_file);
            throw std::runtime_error(message);
        }

        GFileInfo* child_info = NULL;
        while ((child_info = g_file_enumerator_next_file(enumerator, cancellable, &error)) != NULL) {
            const char* child_name = g_file_info_get_name(child_info);
            if (child_name) {
                GFile* child_src = g_file_get_child(src_file, child_name);
                GFile* child_dest = g_file_get_child(dest_file, child_name);
                std::string child_src_location = location_from_file(child_src);
                std::string child_dest_location = location_from_file(child_dest);
                g_object_unref(child_src);
                g_object_unref(child_dest);
                g_object_unref(child_info);
                child_info = NULL;
                copy_path_recursive(child_src_location, child_dest_location, cancellable);
            }
            if (child_info) g_object_unref(child_info);
        }
        if (error) {
            std::string message = error->message;
            g_error_free(error);
            g_object_unref(enumerator);
            g_object_unref(info);
            g_object_unref(src_file);
            g_object_unref(dest_file);
            throw std::runtime_error(message);
        }
        g_object_unref(enumerator);
    } else {
        if (!g_file_copy(src_file, dest_file,
                         G_FILE_COPY_OVERWRITE,
                         cancellable, NULL, NULL, &error)) {
            std::string message = error ? error->message : "Could not copy file.";
            if (error) g_error_free(error);
            g_object_unref(info);
            g_object_unref(src_file);
            g_object_unref(dest_file);
            throw std::runtime_error(message);
        }
    }

    g_object_unref(info);
    g_object_unref(src_file);
    g_object_unref(dest_file);
}

bool move_path(const std::string& src, const std::string& dest, GCancellable* cancellable) {
    if (cancellable && g_cancellable_is_cancelled(cancellable)) return false;

    const bool moving_appimage = is_appimage(src);

    GFile* src_file = file_for_location(src);
    GFile* dest_file = file_for_location(dest);
    GError* error = NULL;
    if (g_file_move(src_file, dest_file, G_FILE_COPY_NONE, cancellable, NULL, NULL, &error)) {
        g_object_unref(src_file);
        g_object_unref(dest_file);
        if (moving_appimage) {
            register_appimage(dest);
        }
        return true;
    }
    if (error) {
        g_error_free(error);
        error = NULL;
    }
    g_object_unref(src_file);
    g_object_unref(dest_file);

    try {
        copy_path_recursive(src, dest, cancellable);
        const bool deleted = delete_path_recursive(src, cancellable);
        if (deleted && moving_appimage) {
            register_appimage(dest);
        }
        return deleted;
    } catch (...) {
        return false;
    }
}

std::string unique_child_location(const std::string& dest_dir, const std::string& name) {
    std::string dest = child_location(dest_dir, name);
    if (!location_exists(dest)) {
        return dest;
    }

    std::string base = name;
    std::string ext = "";
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        base = name.substr(0, dot);
        ext = name.substr(dot);
    }

    std::string copy_name = base + " copy" + ext;
    dest = child_location(dest_dir, copy_name);
    int counter = 2;
    while (location_exists(dest)) {
        copy_name = base + " copy " + std::to_string(counter++) + ext;
        dest = child_location(dest_dir, copy_name);
    }
    return dest;
}

} // namespace utils
