CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra `pkg-config --cflags gtk+-3.0 gdk-x11-3.0 gio-2.0 gio-unix-2.0 x11`
LDFLAGS = `pkg-config --libs gtk+-3.0 gdk-x11-3.0 gio-2.0 gio-unix-2.0 x11`

TARGET = milodock-bin

SRCS = main.cpp settings.cpp launcher.cpp window_tracker.cpp dock_window.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
