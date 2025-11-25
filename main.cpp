#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "xdg-shell-client-protocol.h"

#include <fcntl.h>
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

struct app_state {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct xdg_wm_base *xdg_wm_base;

  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  struct wl_buffer *buffer;

  int32_t width, height;
  uint8_t *data;
  int configured;
};

static void die(const char *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(EXIT_FAILURE);
}

static int create_shm_file(off_t size) {
  char name[] = "/wl_shm_XXXXXX";
  int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd >= 0) {
    shm_unlink(name);
    if (ftruncate(fd, size) < 0) {
      close(fd);
      return -1;
    }
  }
  return fd;
}

static void create_buffer(struct app_state *app) {
  int stride = app->width * 4;
  int size = stride * app->height;
  int fd = create_shm_file(size);
  if (fd < 0) die("Creating SHM file failed");

  uint32_t *pixels = static_cast<uint32_t*>(mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (pixels == MAP_FAILED) die("mmap failed");

  // Copy and convert RGB to ARGB (premultiplied)
  // STBI loads as R, G, B, A (if 4 channels requested)
  // Wayland ARGB8888 is B, G, R, A (little endian) -> 0xAARRGGBB
  for (int i = 0; i < app->width * app->height; ++i) {
      uint8_t r = app->data[i*4 + 0];
      uint8_t g = app->data[i*4 + 1];
      uint8_t b = app->data[i*4 + 2];
      uint8_t a = app->data[i*4 + 3];
      pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
  }

  struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, fd, size);
  app->buffer = wl_shm_pool_create_buffer(pool, 0, app->width, app->height, stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  munmap(pixels, size);
  close(fd);
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
  struct app_state *app = static_cast<struct app_state*>(data);
  xdg_surface_ack_configure(xdg_surface, serial);

  if (!app->buffer) {
    create_buffer(app);
  }

  wl_surface_attach(app->surface, app->buffer, 0, 0);
  wl_surface_commit(app->surface);
  app->configured = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
  .configure = xdg_surface_configure,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
  // Unused parameter 'data'
  (void)data;
  xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  .ping = xdg_wm_base_ping,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
  struct app_state *app = static_cast<struct app_state*>(data);
  // Unused parameter 'version'
  (void)version;

  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    app->compositor = static_cast<struct wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 4));
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    app->shm = static_cast<struct wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    app->xdg_wm_base = static_cast<struct xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
    xdg_wm_base_add_listener(app->xdg_wm_base, &xdg_wm_base_listener, app);
  }
}

static const struct wl_registry_listener registry_listener = {
  .global = registry_handle_global,
  .global_remove = [](void*, struct wl_registry*, uint32_t) {}, // No-op lambda for C++ cleanliness
};

int main(int argc, char *argv[]) {
  if (argc != 2) die("Usage: ./execthis <image.jpg>");

  struct app_state app = {};
  
  // Load image first to fail early if invalid
  int n;
  app.data = stbi_load(argv[1], &app.width, &app.height, &n, 4);
  if (!app.data) die("Failed to load image");

  app.display = wl_display_connect(NULL);
  if (!app.display) die("Cannot connect to Wayland display");

  app.registry = wl_display_get_registry(app.display);
  wl_registry_add_listener(app.registry, &registry_listener, &app);
  wl_display_roundtrip(app.display);

  if (!app.compositor || !app.shm || !app.xdg_wm_base) die("Missing required Wayland globals");

  app.surface = wl_compositor_create_surface(app.compositor);
  app.xdg_surface = xdg_wm_base_get_xdg_surface(app.xdg_wm_base, app.surface);
  xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
  
  app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
  xdg_toplevel_set_title(app.xdg_toplevel, "Simple Image Viewer");
  
  // Initial commit to establish role and trigger configure
  wl_surface_commit(app.surface);

  while (wl_display_dispatch(app.display) != -1) {
    // Simple loop, just wait for events
  }

  stbi_image_free(app.data);
  return 0;
}
