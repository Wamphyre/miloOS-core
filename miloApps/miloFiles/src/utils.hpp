#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <vector>
#include <cstdint>
#include <gio/gio.h>

namespace utils {

struct FavoriteItem {
    std::string uri;
    std::string label;
};

std::string format_size(int64_t size);
bool has_uri_scheme(const std::string& location);
GFile* new_gfile_for_location(const std::string& location);
std::string location_from_gfile(GFile* file);
std::string location_to_uri(const std::string& location);
std::string location_to_path(const std::string& location);
std::string child_location(const std::string& parent, const std::string& child_name);
bool location_exists(const std::string& location);
bool is_directory(const std::string& location);
bool same_location(const std::string& a, const std::string& b);
std::string get_mime_type(const std::string& path);
std::string get_custom_default_command(const std::string& path);
bool is_appimage(const std::string& path);
std::string appimage_icon_path(const std::string& path);
std::string register_appimage(const std::string& path);
bool launch_appimage(const std::string& path);
bool open_files(const std::vector<std::string>& paths);
bool open_file(const std::string& path);
bool is_dangerous_archive_path(const std::string& path);
std::string trash_location();
bool is_trash_location(const std::string& location);
bool empty_trash();
std::vector<FavoriteItem> get_favorites();
bool add_favorite(const std::string& path, const std::string& label = "");
bool remove_favorite(const std::string& uri);
bool rename_favorite(const std::string& uri, const std::string& new_label);
bool reorder_favorite(const std::string& uri, const std::string& target_uri, bool after);
std::string get_file_type_description(const std::string& path, bool is_dir);
std::string get_file_modification_time(const std::string& path);
bool run_command_async(const std::vector<std::string>& argv);
bool make_directory(const std::string& path);
bool create_empty_file(const std::string& path);
std::string get_parent_directory(const std::string& path);
std::string get_filename(const std::string& path);
bool delete_path_recursive(const std::string& path, GCancellable* cancellable = nullptr);
std::string get_free_space_description(const std::string& path);
std::string normalize_path(const std::string& path);
void copy_path_recursive(const std::string& src, const std::string& dest, GCancellable* cancellable = nullptr);
bool move_path(const std::string& src, const std::string& dest, GCancellable* cancellable = nullptr);
std::string unique_child_location(const std::string& dest_dir, const std::string& name);

} // namespace utils

#endif // UTILS_HPP
