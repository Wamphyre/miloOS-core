#include "utils.hpp"
#include "i18n.hpp"
#include <gtk/gtk.h>
#include <gio/gio.h>
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
#include <cstring>
#include <filesystem>
#include <cerrno>
#include <cctype>

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

static void ensure_bookmarks_dir() {
    std::string config_dir = std::string(g_get_user_config_dir()) + "/gtk-3.0";
    g_mkdir_with_parents(config_dir.c_str(), 0700);
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
    GFile* gfile = g_file_new_for_path(path.c_str());
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
    
    // 2. Audio files compatible with VLC
    const std::vector<std::string> audio_extensions = {".mp3", ".wav", ".ogg", ".flac", ".m4a", ".aac", ".wma", ".opus", ".mid", ".midi", ".mka"};
    if (std::find(audio_extensions.begin(), audio_extensions.end(), ext) != audio_extensions.end() || mime.rfind("audio/", 0) == 0) {
        if (which("vlc")) return "vlc";
    }
    
    // 3. Video files: VLC
    const std::vector<std::string> video_extensions = {".mp4", ".mkv", ".avi", ".mov", ".wmv", ".flv", ".webm", ".mpeg", ".mpg", ".m4v"};
    if (std::find(video_extensions.begin(), video_extensions.end(), ext) != video_extensions.end() || mime.rfind("video/", 0) == 0) {
        if (which("vlc")) return "vlc";
    }
    
    // 4. Image files: ristretto
    const std::vector<std::string> image_extensions = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".svg", ".tiff", ".ico"};
    if (std::find(image_extensions.begin(), image_extensions.end(), ext) != image_extensions.end() || mime.rfind("image/", 0) == 0) {
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

bool open_file(const std::string& path) {
    std::string cmd = get_custom_default_command(path);
    if (!cmd.empty()) {
        std::vector<std::string> argv = {cmd, path};
        return run_command_async(argv);
    }
    
    // Fallback to GIO launcher
    GFile* gfile = g_file_new_for_path(path.c_str());
    char* uri = g_file_get_uri(gfile);
    GError* error = NULL;
    g_app_info_launch_default_for_uri(uri, NULL, &error);
    bool success = (error == NULL);
    if (error) {
        g_error_free(error);
    }
    g_free(uri);
    g_object_unref(gfile);
    if (success) return true;

    return run_command_async({"xdg-open", path});
}

bool is_dangerous_archive_path(const std::string& path) {
    // Check for ".." or absolute paths starting with "/" inside archives to prevent directory traversal
    if (path.find("../") != std::string::npos || path.find("..\\") != std::string::npos) {
        return true;
    }
    return false;
}

bool empty_trash() {
    std::string trash_path = std::string(g_get_user_data_dir()) + "/Trash";
    std::string files_path = trash_path + "/files";
    std::string info_path = trash_path + "/info";
    
    auto purge_dir = [](const std::string& dir_path) {
        DIR* dir = opendir(dir_path.c_str());
        if (!dir) return;
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            std::string full_path = dir_path + "/" + name;
            
            GFile* gfile = g_file_new_for_path(full_path.c_str());
            g_file_delete(gfile, NULL, NULL);
            g_object_unref(gfile);
        }
        closedir(dir);
    };
    
    purge_dir(files_path);
    purge_dir(info_path);
    return true;
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
    GFile* gfile = nullptr;
    if (path.rfind("file://", 0) == 0) {
        gfile = g_file_new_for_uri(path.c_str());
    } else {
        gfile = g_file_new_for_path(path.c_str());
    }

    char* uri = g_file_get_uri(gfile);
    char* local_path = g_file_get_path(gfile);
    g_object_unref(gfile);
    if (!uri) {
        if (local_path) g_free(local_path);
        return false;
    }
    
    std::vector<FavoriteItem> favorites = get_favorites();
    for (const auto& fav : favorites) {
        if (fav.uri == uri) {
            g_free(uri);
            if (local_path) g_free(local_path);
            return true; // Already exists
        }
    }
    
    ensure_bookmarks_dir();
    std::string bookmarks_path = get_bookmarks_path();
    std::ofstream file(bookmarks_path, std::ios::app);
    if (!file.is_open()) {
        g_free(uri);
        if (local_path) g_free(local_path);
        return false;
    }
    
    std::string final_label = label;
    if (final_label.empty()) {
        final_label = local_path ? get_filename(local_path) : "";
    }
    
    file << uri << " " << final_label << "\n";
    g_free(uri);
    if (local_path) g_free(local_path);
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

std::string get_file_type_description(const std::string& path, bool is_dir) {
    if (is_dir) return i18n::_("folder");
    
    GFile* gfile = g_file_new_for_path(path.c_str());
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
    
    GError* error = NULL;
    bool success = g_spawn_async(NULL, spawn_argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
    if (error) {
        g_error_free(error);
    }
    
    for (size_t i = 0; i < argv.size(); ++i) {
        g_free(spawn_argv[i]);
    }
    g_free(spawn_argv);
    return success;
}

bool make_directory(const std::string& path) {
    GFile* gfile = g_file_new_for_path(path.c_str());
    GError* error = NULL;
    bool success = g_file_make_directory(gfile, NULL, &error);
    if (error) {
        g_error_free(error);
    }
    g_object_unref(gfile);
    return success;
}

bool create_empty_file(const std::string& path) {
    GFile* gfile = g_file_new_for_path(path.c_str());
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
    GFile* gfile = g_file_new_for_path(path.c_str());
    GFile* parent = g_file_get_parent(gfile);
    std::string parent_path = "";
    if (parent) {
        char* path_str = g_file_get_path(parent);
        if (path_str) {
            parent_path = path_str;
            g_free(path_str);
        }
        g_object_unref(parent);
    }
    g_object_unref(gfile);
    return parent_path;
}

std::string get_filename(const std::string& path) {
    GFile* gfile = g_file_new_for_path(path.c_str());
    char* basename = g_file_get_basename(gfile);
    std::string filename = "";
    if (basename) {
        filename = basename;
        g_free(basename);
    }
    g_object_unref(gfile);
    return filename;
}

bool delete_path_recursive(const std::string& path) {
    try {
        std::filesystem::remove_all(path);
        return true;
    } catch (...) {
        return false;
    }
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

void copy_path_recursive(const std::string& src, const std::string& dest) {
    std::filesystem::copy(src, dest, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
}

bool move_path(const std::string& src, const std::string& dest) {
    if (std::rename(src.c_str(), dest.c_str()) == 0) {
        return true;
    }

    try {
        copy_path_recursive(src, dest);
        return delete_path_recursive(src);
    } catch (...) {
        return false;
    }
}

} // namespace utils
