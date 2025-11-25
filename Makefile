# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -pthread -I.

# Linker flags
LDFLAGS = -lwayland-client -lm -lpthread

# Update source files to exclude async.c, async.h, stb_image_impl.c
SRCS = main.cpp xdg-shell-protocol.c

# Object files in build directory
OBJDIR = build
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(filter %.c,$(SRCS))) $(patsubst %.cpp,$(OBJDIR)/%.o,$(filter %.cpp,$(SRCS)))

# Target executable
TARGET = execthis

# Default target to build executable
all: $(TARGET)

# Link object files to produce executable
$(TARGET): $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

# Compile C++ source files to object files in build directory
$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile C source files to object files in build directory
$(OBJDIR)/%.o: %.c | $(OBJDIR)
	gcc $(CFLAGS) -c $< -o $@

# Create build directory if it doesn't exist
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Clean build artifacts
clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all clean

