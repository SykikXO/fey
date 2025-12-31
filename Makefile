# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -pthread -Isrc -Isrc/protocols $(shell pkg-config --cflags cairo)
CFLAGS = -Wall -Wextra -Isrc/protocols $(shell pkg-config --cflags cairo)

# Linker flags
LDFLAGS = -lwayland-client -lrt -lm -lpthread $(shell pkg-config --libs cairo)

# Project paths
SRCDIR = src
PROTODIR = $(SRCDIR)/protocols
OBJDIR = build

# Source files
SRCS_CPP = $(SRCDIR)/main.cpp $(SRCDIR)/renderer.cpp $(SRCDIR)/loader.cpp $(SRCDIR)/input.cpp
SRCS_C = $(PROTODIR)/xdg-shell-protocol.c $(PROTODIR)/pointer-gestures-unstable-v1-protocol.c

# Object files
OBJS = $(SRCS_CPP:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o) \
       $(SRCS_C:$(PROTODIR)/%.c=$(OBJDIR)/%.o)

# Target
TARGET = fey

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(PROTODIR)/%.c | $(OBJDIR)
	gcc $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)

.PHONY: all clean install
