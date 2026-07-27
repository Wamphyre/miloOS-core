CXX = g++
PKGS = gtk+-3.0 gdk-x11-3.0 gio-2.0 gio-unix-2.0 gmodule-2.0 x11
CXXFLAGS = -std=c++17 -Wall -Wextra -MMD -MP `pkg-config --cflags $(PKGS)`
LDFLAGS = `pkg-config --libs $(PKGS)`

TARGET = milopanel-bin

SRCS = main.cpp panel_settings.cpp menu_model.cpp system_tray_host.cpp status_notifier_host.cpp appmenu_registrar.cpp panel_window.cpp
OBJS = $(SRCS:.cpp=.o)
DEPS = $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

.PHONY: all clean

-include $(DEPS)
