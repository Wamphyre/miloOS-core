#include "window_tracker.hpp"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

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

void add_token_variants(std::set<std::string>& target, const std::string& value) {
    std::string token = normalize_token(value);
    if (token.empty()) {
        return;
    }
    target.insert(token);
    auto dot = token.rfind('.');
    if (dot != std::string::npos && dot + 1 < token.size()) {
        target.insert(token.substr(dot + 1));
    }
    if (has_suffix(token, "-esr")) {
        target.insert(token.substr(0, token.size() - 4));
    }
    if (token.rfind("org.xfce.", 0) == 0) {
        target.insert(token.substr(9));
    }
}

bool tokens_overlap(
    const std::set<std::string>& first,
    const std::set<std::string>& second) {
    for (const auto& token : first) {
        if (second.count(token)) {
            return true;
        }
    }
    return false;
}

bool atom_in(const std::vector<Atom>& atoms, Atom needle) {
    return std::find(atoms.begin(), atoms.end(), needle) != atoms.end();
}

std::string path_basename(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string process_environment_value(long pid, const std::string& key) {
    if (pid <= 0 || key.empty()) {
        return "";
    }
    std::ifstream input(
        "/proc/" + std::to_string(pid) + "/environ",
        std::ios::binary);
    if (!input.is_open()) {
        return "";
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string prefix = key + "=";
    const std::string environment = contents.str();
    size_t offset = 0;
    while (offset < environment.size()) {
        const size_t end = environment.find('\0', offset);
        const size_t length =
            end == std::string::npos ? environment.size() - offset : end - offset;
        const std::string entry = environment.substr(offset, length);
        if (entry.rfind(prefix, 0) == 0) {
            return entry.substr(prefix.size());
        }
        if (end == std::string::npos) {
            break;
        }
        offset = end + 1;
    }
    return "";
}

} // namespace

WindowTracker::WindowTracker() {
    GdkDisplay* gdk_display = gdk_display_get_default();
    if (!gdk_display || !GDK_IS_X11_DISPLAY(gdk_display)) {
        return;
    }
    display_ = GDK_DISPLAY_XDISPLAY(gdk_display);
    root_ = DefaultRootWindow(display_);
}

Atom WindowTracker::atom(const char* name) const {
    return display_ ? XInternAtom(display_, name, False) : None;
}

std::vector<Atom> WindowTracker::atom_list(Window window, Atom property) const {
    std::vector<Atom> result;
    if (!display_ || property == None) {
        return result;
    }

    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    int status = XGetWindowProperty(
        display_, window, property, 0, 4096, False, XA_ATOM,
        &actual_type, &actual_format, &nitems, &bytes_after, &data);
    if (status == Success && data && actual_format == 32) {
        auto* atoms = reinterpret_cast<Atom*>(data);
        for (unsigned long i = 0; i < nitems; ++i) {
            result.push_back(atoms[i]);
        }
    }
    if (data) {
        XFree(data);
    }
    return result;
}

std::vector<Window> WindowTracker::window_list(Window window, Atom property) const {
    std::vector<Window> result;
    if (!display_ || property == None) {
        return result;
    }

    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    int status = XGetWindowProperty(
        display_, window, property, 0, 65536, False, XA_WINDOW,
        &actual_type, &actual_format, &nitems, &bytes_after, &data);
    if (status == Success && data && actual_format == 32) {
        auto* windows = reinterpret_cast<Window*>(data);
        for (unsigned long i = 0; i < nitems; ++i) {
            result.push_back(windows[i]);
        }
    }
    if (data) {
        XFree(data);
    }
    return result;
}

long WindowTracker::cardinal(Window window, Atom property, long fallback) const {
    if (!display_ || property == None) {
        return fallback;
    }

    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    int status = XGetWindowProperty(
        display_, window, property, 0, 1, False, XA_CARDINAL,
        &actual_type, &actual_format, &nitems, &bytes_after, &data);
    long value = fallback;
    if (status == Success && data && actual_format == 32 && nitems > 0) {
        value = static_cast<long>(*reinterpret_cast<unsigned long*>(data));
    }
    if (data) {
        XFree(data);
    }
    return value;
}

std::string WindowTracker::text(Window window, Atom property) const {
    if (!display_ || property == None) {
        return "";
    }

    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;
    Atom utf8 = atom("UTF8_STRING");
    int status = XGetWindowProperty(
        display_, window, property, 0, 4096, False, utf8,
        &actual_type, &actual_format, &nitems, &bytes_after, &data);
    std::string value;
    if (status == Success && data && actual_format == 8 && nitems > 0) {
        value.assign(reinterpret_cast<char*>(data), nitems);
    }
    if (data) {
        XFree(data);
    }
    return value;
}

std::set<std::string> WindowTracker::tokens_for_values(const std::vector<std::string>& values) const {
    std::set<std::string> tokens;
    for (const auto& value : values) {
        add_token_variants(tokens, value);
    }
    return tokens;
}

int WindowTracker::current_desktop() const {
    return static_cast<int>(cardinal(root_, atom("_NET_CURRENT_DESKTOP"), 0));
}

Window WindowTracker::active_window() const {
    auto active = window_list(root_, atom("_NET_ACTIVE_WINDOW"));
    return active.empty() ? 0 : active.front();
}

std::vector<TrackedWindow> WindowTracker::windows() const {
    std::vector<TrackedWindow> result;
    if (!available()) {
        return result;
    }

    GdkDisplay* gdk_display = gdk_display_get_default();
    if (gdk_display && GDK_IS_X11_DISPLAY(gdk_display)) {
        gdk_x11_display_error_trap_push(gdk_display);
    }

    const Atom state_atom = atom("_NET_WM_STATE");
    const Atom skip_tasklist_atom = atom("_NET_WM_STATE_SKIP_TASKBAR");
    const Atom hidden_atom = atom("_NET_WM_STATE_HIDDEN");
    const Atom max_horz_atom = atom("_NET_WM_STATE_MAXIMIZED_HORZ");
    const Atom max_vert_atom = atom("_NET_WM_STATE_MAXIMIZED_VERT");
    const Atom fullscreen_atom = atom("_NET_WM_STATE_FULLSCREEN");
    const Atom desktop_atom = atom("_NET_WM_DESKTOP");
    const Atom wm_name_atom = atom("_NET_WM_NAME");
    const Atom wm_desktop_file_atom = atom("_NET_WM_DESKTOP_FILE");
    const Atom gtk_application_id_atom = atom("_GTK_APPLICATION_ID");
    const Atom wm_pid_atom = atom("_NET_WM_PID");

    for (Window xid : window_list(root_, atom("_NET_CLIENT_LIST"))) {
        XWindowAttributes attrs;
        if (!XGetWindowAttributes(display_, xid, &attrs)) {
            continue;
        }

        TrackedWindow window;
        window.xid = xid;
        window.width = attrs.width;
        window.height = attrs.height;

        Window child = 0;
        int translated_x = 0;
        int translated_y = 0;
        if (XTranslateCoordinates(display_, xid, root_, 0, 0, &translated_x, &translated_y, &child)) {
            window.x = translated_x;
            window.y = translated_y;
        } else {
            window.x = attrs.x;
            window.y = attrs.y;
        }

        window.name = text(xid, wm_name_atom);
        if (window.name.empty()) {
            char* raw_name = nullptr;
            if (XFetchName(display_, xid, &raw_name) && raw_name) {
                window.name = raw_name;
                XFree(raw_name);
            }
        }

        XClassHint class_hint;
        std::memset(&class_hint, 0, sizeof(class_hint));
        if (XGetClassHint(display_, xid, &class_hint)) {
            if (class_hint.res_class) {
                window.wm_class = class_hint.res_class;
                XFree(class_hint.res_class);
            }
            if (class_hint.res_name) {
                window.wm_instance = class_hint.res_name;
                XFree(class_hint.res_name);
            }
        }
        window.desktop_file = text(xid, wm_desktop_file_atom);
        window.gtk_application_id = text(xid, gtk_application_id_atom);
        window.pid = cardinal(xid, wm_pid_atom, 0);
        window.appimage_path =
            process_environment_value(window.pid, "APPIMAGE");
        if (window.appimage_path.empty()) {
            window.appimage_path =
                process_environment_value(window.pid, "MILO_APPIMAGE_PATH");
        }
        if (window.desktop_file.empty()) {
            const std::string launched_desktop_file = path_basename(
                process_environment_value(
                    window.pid, "GIO_LAUNCHED_DESKTOP_FILE"));
            const auto native_tokens = tokens_for_values({
                window.wm_class,
                window.wm_instance,
                window.gtk_application_id
            });
            const auto launched_tokens =
                tokens_for_values({launched_desktop_file});
            if (!launched_desktop_file.empty() &&
                (!window.appimage_path.empty() ||
                 native_tokens.empty() ||
                 tokens_overlap(native_tokens, launched_tokens))) {
                window.desktop_file = launched_desktop_file;
            }
        }

        auto states = atom_list(xid, state_atom);
        window.skip_tasklist = atom_in(states, skip_tasklist_atom);
        window.minimized = atom_in(states, hidden_atom) || attrs.map_state != IsViewable;
        window.maximized = atom_in(states, max_horz_atom) && atom_in(states, max_vert_atom);
        window.fullscreen = atom_in(states, fullscreen_atom);
        window.desktop = cardinal(xid, desktop_atom, -1);
        window.identity_tokens = tokens_for_values({
            window.wm_class,
            window.wm_instance,
            window.desktop_file,
            window.gtk_application_id,
            path_basename(window.appimage_path)
        });
        window.tokens = tokens_for_values({
            window.wm_class,
            window.wm_instance,
            window.desktop_file,
            window.gtk_application_id,
            path_basename(window.appimage_path),
            window.name
        });
        result.push_back(std::move(window));
    }

    if (gdk_display && GDK_IS_X11_DISPLAY(gdk_display)) {
        gdk_x11_display_error_trap_pop_ignored(gdk_display);
    }
    return result;
}

void WindowTracker::send_client_message(Window target, Atom message, long data0, long data1, long data2) const {
    if (!available() || message == None) {
        return;
    }

    XEvent event;
    std::memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.window = target;
    event.xclient.message_type = message;
    event.xclient.format = 32;
    event.xclient.data.l[0] = data0;
    event.xclient.data.l[1] = data1;
    event.xclient.data.l[2] = data2;
    event.xclient.data.l[3] = 0;
    event.xclient.data.l[4] = 0;
    XSendEvent(
        display_,
        root_,
        False,
        SubstructureRedirectMask | SubstructureNotifyMask,
        &event);
    XFlush(display_);
}

void WindowTracker::activate(Window xid) const {
    if (!available() || xid == 0) {
        return;
    }
    GdkDisplay* gdk_display = gdk_display_get_default();
    if (gdk_display && GDK_IS_X11_DISPLAY(gdk_display)) {
        gdk_x11_display_error_trap_push(gdk_display);
    }
    send_client_message(xid, atom("_NET_ACTIVE_WINDOW"), 2, CurrentTime, 0);
    XMapRaised(display_, xid);
    XFlush(display_);
    if (gdk_display && GDK_IS_X11_DISPLAY(gdk_display)) {
        gdk_x11_display_error_trap_pop_ignored(gdk_display);
    }
}

void WindowTracker::close(Window xid) const {
    if (!available() || xid == 0) {
        return;
    }
    GdkDisplay* gdk_display = gdk_display_get_default();
    if (gdk_display && GDK_IS_X11_DISPLAY(gdk_display)) {
        gdk_x11_display_error_trap_push(gdk_display);
    }
    send_client_message(xid, atom("_NET_CLOSE_WINDOW"), CurrentTime, 2, 0);
    if (gdk_display && GDK_IS_X11_DISPLAY(gdk_display)) {
        gdk_x11_display_error_trap_pop_ignored(gdk_display);
    }
}
