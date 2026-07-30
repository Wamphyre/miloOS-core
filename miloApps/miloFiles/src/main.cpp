#include <gtk/gtk.h>
#include <gio/gio.h>
#include <iostream>
#include <string>
#include <vector>
#include <locale.h>
#include <sys/stat.h>
#include "app_window.hpp"
#include "utils.hpp"

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");
    g_set_prgname("milofiles");
    g_set_application_name("miloFiles");

    if (argc == 3 && std::string(argv[1]) == "--register-appimage") {
        const std::string desktop_path = utils::register_appimage(argv[2]);
        if (desktop_path.empty()) {
            return 1;
        }
        std::cout << desktop_path << std::endl;
        return 0;
    }

    if (argc >= 3 && std::string(argv[1]) == "--launch-appimage") {
        std::vector<std::string> arguments;
        for (int index = 3; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }
        return utils::run_appimage(argv[2], arguments) ? 0 : 1;
    }

    gtk_init(&argc, &argv);
    
    std::string initial_dir = "";
    if (argc > 1) {
        std::string arg_path = argv[1];
        if (arg_path.rfind("file://", 0) == 0) {
            GFile* gfile = g_file_new_for_uri(arg_path.c_str());
            char* path = g_file_get_path(gfile);
            if (path) {
                initial_dir = path;
                g_free(path);
            }
            g_object_unref(gfile);
        } else {
            initial_dir = arg_path;
        }

        if (initial_dir.empty() || !utils::is_directory(initial_dir)) {
            initial_dir.clear();
        }
    }
    
    AppWindow app(initial_dir);
    app.show();
    
    gtk_main();
    return 0;
}
