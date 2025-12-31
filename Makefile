# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -pthread -I. $(shell pkg-config --cflags cairo)
CFLAGS = -Wall -Wextra $(shell pkg-config --cflags cairo)

# Linker flags
LDFLAGS = -lwayland-client -lrt -lm -lpthread $(shell pkg-config --libs cairo)

# Source files
SRCS = main.cpp renderer.cpp loader.cpp input.cpp xdg-shell-protocol.c pointer-gestures-unstable-v1-protocol.c

# Object files
OBJDIR = build
OBJS = $(OBJDIR)/main.o $(OBJDIR)/renderer.o $(OBJDIR)/loader.o $(OBJDIR)/input.o $(OBJDIR)/xdg-shell-protocol.o $(OBJDIR)/pointer-gestures-unstable-v1-protocol.o

# Target
TARGET = execthis

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: %.cpp app.h renderer.h loader.h input.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/xdg-shell-protocol.o: xdg-shell-protocol.c | $(OBJDIR)
	gcc $(CFLAGS) -c $< -o $@

$(OBJDIR)/pointer-gestures-unstable-v1-protocol.o: pointer-gestures-unstable-v1-protocol.c | $(OBJDIR)
	gcc $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all clean
