#ifndef I18N_HPP
#define I18N_HPP

#include <string>
#include <unordered_map>
#include <locale>
#include <cstdlib>

namespace i18n {

inline std::string get_language() {
    const char* lc_all = std::getenv("LC_ALL");
    if (lc_all != nullptr) {
        std::string lang(lc_all);
        if (lang.rfind("es", 0) == 0) {
            return "es";
        }
    }
    const char* lang_env = std::getenv("LANG");
    if (lang_env != nullptr) {
        std::string lang(lang_env);
        if (lang.rfind("es", 0) == 0) {
            return "es";
        }
    }
    return "en";
}

const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> TRANSLATIONS = {
    {"en", {
        {"title", "miloFiles"},
        {"devices", "DEVICES"},
        {"favorites", "FAVORITES"},
        {"name", "Name"},
        {"size", "Size"},
        {"type", "Type"},
        {"modified", "Modified"},
        {"items", "items"},
        {"free", "free"},
        {"search", "Search"},
        {"open", "Open"},
        {"properties", "Get Info"},
        {"cut", "Cut"},
        {"copy", "Copy"},
        {"paste", "Paste"},
        {"select_all", "Select All"},
        {"rename", "Rename"},
        {"trash_action", "Move to Trash"},
        {"trash", "Trash"},
        {"delete", "Delete Permanently"},
        {"file_system", "File System"},
        {"go_location", "Go to Location..."},
        {"new_folder", "New Folder"},
        {"new_file", "New File"},
        {"properties_title", "Get Info"},
        {"general", "General"},
        {"permissions", "Permissions"},
        {"location", "Location"},
        {"owner", "Owner"},
        {"folder", "Folder"},
        {"file", "File"},
        {"appimage_application", "AppImage Application"},
        {"cannot_read_dir", "Cannot read directory"},
        {"new_folder_name", "New Folder"},
        {"new_file_name", "New File.txt"},
        {"enter_new_name", "Enter new name:"},
        {"confirm_delete", "Are you sure you want to permanently delete these items?"},
        {"delete_title", "Confirm Delete"},
        {"delete_error", "Delete Error"},
        {"trash_fallback_confirm", "These items cannot be moved to the Trash here. Delete them permanently instead?"},
        {"paste_error", "Paste Error"},
        {"cannot_create_folder", "Cannot create folder"},
        {"open_error", "Cannot open file"},
        {"group", "Group"},
        {"octal", "Octal"},
        {"apply", "Apply"},
        {"file_menu", "File"},
        {"new_window", "New Window"},
        {"edit_menu", "Edit"},
        {"view_menu", "View"},
        {"go_menu", "Go"},
        {"close", "Close"},
        {"view_icon", "as Icons"},
        {"view_list", "as List"},
        {"show_hidden_menu", "Show Hidden Files"},
        {"go_back", "Back"},
        {"go_forward", "Forward"},
        {"go_up", "Enclosing Folder"},
        {"connect_to_server", "Connect to Server"},
        {"enter_server_address", "Enter server address:"},
        {"connect", "Connect"},
        {"connection_error", "Connection Error"},
        {"compress", "Compress..."},
        {"extract_here", "Extract Here"},
        {"compress_error", "Compression Error"},
        {"extract_error", "Extraction Error"},
        {"eject_error", "Eject Error"},
        {"archive_name", "Archive name:"},
        {"format", "Format:"},
        {"calculating", "Calculating..."},
        {"dangerous_archive_path", "Dangerous path traversal detected in archive!"},
        {"open_with", "Open with"},
        {"other_application", "Other Application..."},
        {"connect_placeholder", "smb://server/share or ftp://server/folder"},
        {"open_terminal", "Open Terminal"},
        {"cancel", "Cancel"},
        {"skip", "Skip"},
        {"replace", "Replace"},
        {"keep_both", "Keep Both"},
        {"replace_existing_title", "Replace Existing Item"},
        {"replace_existing_prefix", "An item named \""},
        {"replace_existing_suffix", "\" already exists in the destination."},
        {"compressing", "Compressing files..."},
        {"extracting", "Extracting archive..."},
        {"pasting", "Copying/Moving files..."},
        {"deleting", "Deleting files..."},
        {"trashing", "Moving files to the Trash..."},
        {"empty_trash", "Empty Trash"},
        {"rename_favorite", "Rename Favorite..."},
        {"remove_favorite", "Remove from Favorites"},
        {"drag_to_reorder", "Drag to reorder"},
        {"confirm_empty_trash", "Are you sure you want to empty the Trash?"},
        {"add_to_favorites", "Add to Favorites"},
        {"mount_volume", "Mount Volume"},
        {"unmount_volume", "Unmount Volume"}
    }},
    {"es", {
        {"title", "miloFiles"},
        {"devices", "DISPOSITIVOS"},
        {"favorites", "FAVORITOS"},
        {"name", "Nombre"},
        {"size", "Tamaño"},
        {"type", "Tipo"},
        {"modified", "Modificado"},
        {"items", "elementos"},
        {"free", "libres"},
        {"search", "Buscar"},
        {"open", "Abrir"},
        {"properties", "Obtener Información"},
        {"cut", "Cortar"},
        {"copy", "Copiar"},
        {"paste", "Pegar"},
        {"select_all", "Seleccionar todo"},
        {"rename", "Renombrar"},
        {"trash_action", "Mover a la Papelera"},
        {"trash", "Papelera"},
        {"delete", "Eliminar Permanentemente"},
        {"file_system", "Sistema de archivos"},
        {"go_location", "Ir a la ubicación..."},
        {"new_folder", "Nueva Carpeta"},
        {"new_file", "Nuevo Archivo"},
        {"properties_title", "Obtener Información"},
        {"general", "General"},
        {"permissions", "Permisos"},
        {"location", "Ubicación"},
        {"owner", "Propietario"},
        {"folder", "Carpeta"},
        {"file", "Archivo"},
        {"appimage_application", "Aplicación AppImage"},
        {"cannot_read_dir", "No se puede leer el directorio"},
        {"new_folder_name", "Nueva Carpeta"},
        {"new_file_name", "Nuevo Archivo.txt"},
        {"enter_new_name", "Introduzca el nuevo nombre:"},
        {"confirm_delete", "¿Está seguro de que desea eliminar permanentemente estos elementos?"},
        {"delete_title", "Confirmar Eliminación"},
        {"delete_error", "Error al eliminar"},
        {"trash_fallback_confirm", "No se pueden mover estos elementos a la Papelera aquí. ¿Eliminarlos permanentemente?"},
        {"paste_error", "Error al Pegar"},
        {"cannot_create_folder", "No se pudo crear la carpeta"},
        {"open_error", "No se pudo abrir el archivo"},
        {"group", "Grupo"},
        {"octal", "Octal"},
        {"apply", "Aplicar"},
        {"file_menu", "Archivo"},
        {"new_window", "Nueva ventana"},
        {"edit_menu", "Editar"},
        {"view_menu", "Ver"},
        {"go_menu", "Ir"},
        {"close", "Cerrar"},
        {"view_icon", "como Iconos"},
        {"view_list", "como Lista"},
        {"show_hidden_menu", "Mostrar archivos ocultos"},
        {"go_back", "Atrás"},
        {"go_forward", "Adelante"},
        {"go_up", "Carpeta contenedora"},
        {"connect_to_server", "Conectarse al servidor"},
        {"enter_server_address", "Introduzca la dirección del servidor:"},
        {"connect", "Conectar"},
        {"connection_error", "Error de conexión"},
        {"compress", "Comprimir..."},
        {"extract_here", "Extraer aquí"},
        {"compress_error", "Error de compresión"},
        {"extract_error", "Error de extracción"},
        {"eject_error", "Error al expulsar"},
        {"archive_name", "Nombre del archivo:"},
        {"format", "Formato:"},
        {"calculating", "Calculando..."},
        {"dangerous_archive_path", "¡Ruta de extracción peligrosa detectada en el archivo!"},
        {"open_with", "Abrir con"},
        {"other_application", "Otra aplicación..."},
        {"connect_placeholder", "smb://servidor/carpeta o ftp://servidor/carpeta"},
        {"open_terminal", "Abrir terminal"},
        {"cancel", "Cancelar"},
        {"skip", "Omitir"},
        {"replace", "Reemplazar"},
        {"keep_both", "Conservar ambos"},
        {"replace_existing_title", "Reemplazar elemento existente"},
        {"replace_existing_prefix", "Ya existe un elemento llamado \""},
        {"replace_existing_suffix", "\" en el destino."},
        {"compressing", "Comprimiendo archivos..."},
        {"extracting", "Extrayendo archivo..."},
        {"pasting", "Copiando/Moviendo archivos..."},
        {"deleting", "Eliminando archivos..."},
        {"trashing", "Moviendo archivos a la Papelera..."},
        {"empty_trash", "Vaciar papelera"},
        {"rename_favorite", "Renombrar favorito..."},
        {"remove_favorite", "Quitar de favoritos"},
        {"drag_to_reorder", "Arrastrar para reordenar"},
        {"confirm_empty_trash", "¿Está seguro de que desea vaciar la papelera?"},
        {"add_to_favorites", "Añadir a favoritos"},
        {"mount_volume", "Montar volumen"},
        {"unmount_volume", "Desmontar volumen"}
    }}
};

inline std::string _(const std::string& key) {
    std::string lang = get_language();
    auto it_lang = TRANSLATIONS.find(lang);
    if (it_lang != TRANSLATIONS.end()) {
        auto it_key = it_lang->second.find(key);
        if (it_key != it_lang->second.end()) {
            return it_key->second;
        }
    }
    // Fallback to English
    auto it_en = TRANSLATIONS.find("en");
    if (it_en != TRANSLATIONS.end()) {
        auto it_key = it_en->second.find(key);
        if (it_key != it_en->second.end()) {
            return it_key->second;
        }
    }
    return key;
}

} // namespace i18n

#endif // I18N_HPP
