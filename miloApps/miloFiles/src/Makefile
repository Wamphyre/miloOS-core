CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra `pkg-config --cflags gtk+-3.0 gio-2.0`
LDFLAGS = `pkg-config --libs gtk+-3.0 gio-2.0` -lpthread

TARGET = milofiles-bin

SRCS = main.cpp utils.cpp progress_dialog.cpp sidebar.cpp file_view.cpp app_window.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
