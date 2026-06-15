#pragma once

#include <X11/Xlib.h>

#include <set>
#include <string>
#include <vector>

struct TrackedWindow {
    Window xid = 0;
    std::string name;
    std::string wm_class;
    std::string wm_instance;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    long desktop = -1;
    bool skip_tasklist = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;
    std::set<std::string> identity_tokens;
    std::set<std::string> tokens;
};

class WindowTracker {
public:
    WindowTracker();

    bool available() const { return display_ != nullptr && root_ != 0; }
    int current_desktop() const;
    std::vector<TrackedWindow> windows() const;
    void activate(Window xid) const;
    void close(Window xid) const;

private:
    Display* display_ = nullptr;
    Window root_ = 0;

    Atom atom(const char* name) const;
    std::vector<Atom> atom_list(Window window, Atom property) const;
    std::vector<Window> window_list(Window window, Atom property) const;
    long cardinal(Window window, Atom property, long fallback) const;
    std::string text(Window window, Atom property) const;
    std::set<std::string> tokens_for_values(const std::vector<std::string>& values) const;
    void send_client_message(Window target, Atom message, long data0, long data1, long data2) const;
};
