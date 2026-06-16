#pragma once

#include <cstdlib>
#include <string>
#include <unordered_map>

namespace i18n {

inline std::string language() {
    const char* candidates[] = {
        std::getenv("LC_ALL"),
        std::getenv("LC_MESSAGES"),
        std::getenv("LANGUAGE"),
        std::getenv("LANG"),
        nullptr
    };

    for (const char* value : candidates) {
        if (!value || !*value) {
            continue;
        }
        std::string lang(value);
        if (lang.rfind("es", 0) == 0) {
            return "es";
        }
        if (lang.rfind("en", 0) == 0) {
            return "en";
        }
    }
    return "en";
}

inline std::string tr(const std::string& key) {
    static const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> translations = {
        {"en", {
            {"desktop", "Desktop"},
            {"file_menu", "File"},
            {"view_menu", "View"},
            {"go_menu", "Go"},
            {"new_window", "New Window"},
            {"open_terminal", "Open Terminal"},
            {"reload_desktop", "Reload Desktop"},
            {"home_folder", "Home"},
            {"desktop_folder", "Desktop"},
            {"documents_folder", "Documents"},
            {"downloads_folder", "Downloads"},
            {"notifications", "Notifications"},
            {"audio_mixer", "Audio Mixer..."},
            {"volume_output", "Output"},
            {"volume_input", "Input"},
        }},
        {"es", {
            {"desktop", "Escritorio"},
            {"file_menu", "Archivo"},
            {"view_menu", "Ver"},
            {"go_menu", "Ir"},
            {"new_window", "Nueva ventana"},
            {"open_terminal", "Abrir terminal"},
            {"reload_desktop", "Recargar escritorio"},
            {"home_folder", "Carpeta personal"},
            {"desktop_folder", "Escritorio"},
            {"documents_folder", "Documentos"},
            {"downloads_folder", "Descargas"},
            {"notifications", "Notificaciones"},
            {"audio_mixer", "Mezclador de audio..."},
            {"volume_output", "Salida"},
            {"volume_input", "Entrada"},
        }}
    };

    const std::string lang = language();
    auto lang_it = translations.find(lang);
    if (lang_it != translations.end()) {
        auto key_it = lang_it->second.find(key);
        if (key_it != lang_it->second.end()) {
            return key_it->second;
        }
    }

    auto fallback_it = translations.find("en");
    if (fallback_it != translations.end()) {
        auto key_it = fallback_it->second.find(key);
        if (key_it != fallback_it->second.end()) {
            return key_it->second;
        }
    }
    return key;
}

} // namespace i18n
