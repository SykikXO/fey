#include "app.h"
#include "renderer.h"
#include "loader.h"
#include "input.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <algorithm>

void die(const char *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(EXIT_FAILURE);
}

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
  (void)version;
  struct app_state *app = static_cast<struct app_state*>(data);

  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    app->compositor = static_cast<struct wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 4));
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    app->shm = static_cast<struct wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
    app->xdg_wm_base = static_cast<struct xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
    xdg_wm_base_add_listener(app->xdg_wm_base, &xdg_wm_base_listener, app);
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    app->seat = static_cast<struct wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 7));
    wl_seat_add_listener(app->seat, &seat_listener, app);
  } else if (strcmp(interface, zwp_pointer_gestures_v1_interface.name) == 0) {
    app->gestures = static_cast<struct zwp_pointer_gestures_v1*>(wl_registry_bind(registry, name, &zwp_pointer_gestures_v1_interface, 1));
  }
}

const struct wl_registry_listener registry_listener = {
  .global = registry_handle_global,
  .global_remove = [](void*, struct wl_registry*, uint32_t) {},
};

static void surface_frame_callback(void *data, struct wl_callback *callback, uint32_t time);

static const struct wl_callback_listener frame_listener = {
  .done = surface_frame_callback
};

static void surface_frame_callback(void *data, struct wl_callback *callback, uint32_t time) {
  (void)time;
  struct app_state *app = static_cast<struct app_state*>(data);
  wl_callback_destroy(callback);
  app->frame_callback = nullptr;

  // Rubber-band animation physics
  bool animating = false;
  float lerp_factor = 0.15f;
  
  // 1. Zoom limits (0.05x to 10.0x)
  if (app->zoom < 0.05f) app->target_zoom = 0.05f;
  else if (app->zoom > 10.0f) app->target_zoom = 10.0f;
  else app->target_zoom = app->zoom;

  // 2. Dynamic Panning Limits
  // Calculate current image size on screen
  float iw = app->orig_width * app->zoom;
  float ih = app->orig_height * app->zoom;
  
  // Hard limit: image should at least touch the screen center
  float limit_x = (app->width + iw) / 2.0f - 50; 
  float limit_y = (app->height + ih) / 2.0f - 50;

  // When zoomed out (image smaller than screen), target should be center (0,0)
  if (iw < app->width) app->target_pan_x = 0;
  else app->target_pan_x = std::clamp(app->pan_x, -limit_x, limit_x);

  if (ih < app->height) app->target_pan_y = 0;
  else app->target_pan_y = std::clamp(app->pan_y, -limit_y, limit_y);

  // 3. Apply Rebound Animation
  if (std::abs(app->zoom - app->target_zoom) > 0.001f) {
      app->zoom += (app->target_zoom - app->zoom) * lerp_factor;
      animating = true;
  }
  if (std::abs(app->pan_x - app->target_pan_x) > 0.1f) {
      app->pan_x += (app->target_pan_x - app->pan_x) * lerp_factor;
      animating = true;
  }
  if (std::abs(app->pan_y - app->target_pan_y) > 0.1f) {
      app->pan_y += (app->target_pan_y - app->pan_y) * lerp_factor;
      animating = true;
  }

  // GIF Animation Step
  auto it_cache = app->cache.find(app->current_index);
  if (it_cache != app->cache.end() && it_cache->second.frames.size() > 1) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - app->last_frame_time).count();
      int delay = it_cache->second.delays[app->current_frame_index % it_cache->second.frames.size()];
      if (delay <= 0) delay = 100; // Default fallback for GIFs with 0 delay

      if (elapsed >= delay) {
          app->current_frame_index = (app->current_frame_index + 1) % it_cache->second.frames.size();
          app->last_frame_time = now;
          app->redraw_pending = true;
      }
      animating = true; // Keep callback loop alive for next frame
  }

  // Trigger next frame if animating or state changes
  if (app->redraw_pending || app->zooming_in || app->zooming_out || animating) {
    create_buffer(app);
    wl_surface_attach(app->surface, app->buffer, 0, 0);
    wl_surface_damage(app->surface, 0, 0, app->width, app->height);

    app->frame_callback = wl_surface_frame(app->surface);
    wl_callback_add_listener(app->frame_callback, &frame_listener, app);
    wl_surface_commit(app->surface);
    
    app->redraw_pending = false;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) die("Usage: ./execthis <image.jpg>");

  struct app_state app = {};
  app.running = 1;
  app.zoom = 1.0f;
  app.target_zoom = 1.0f;
  app.pan_x = app.pan_y = 0;
  app.target_pan_x = app.target_pan_y = 0;
  app.shm_fd = -1;
  app.configured = false;

  scan_directory(&app, argv[1]);
  if (app.images.empty()) die("No images found");
  
  load_image(&app, app.current_index);
  if (!app.orig_data) die("Failed to load initial image");
  app.width = app.orig_width;
  app.height = app.orig_height;

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
  xdg_toplevel_add_listener(app.xdg_toplevel, &xdg_toplevel_listener, &app);
  xdg_toplevel_set_title(app.xdg_toplevel, "Hyper Image Viewer");
  
  wl_surface_commit(app.surface);

  int display_fd = wl_display_get_fd(app.display);

  while (app.running) {
    // Process continuous zooming state
    if (app.zooming_in) {
      app.zoom = std::min(app.zoom * 1.03f, 15.0f);
      app.redraw_pending = true;
    }
    if (app.zooming_out) {
      app.zoom = std::max(app.zoom * 0.97f, 0.05f);
      app.redraw_pending = true;
    }

    // Trigger redraw if ready
    if (app.redraw_pending && !app.frame_callback && app.configured) {
        create_buffer(&app);
        if (app.buffer) {
            wl_surface_attach(app.surface, app.buffer, 0, 0);
            wl_surface_damage(app.surface, 0, 0, app.width, app.height);
            app.frame_callback = wl_surface_frame(app.surface);
            wl_callback_add_listener(app.frame_callback, &frame_listener, &app);
            wl_surface_commit(app.surface);
        }
        app.redraw_pending = false;
    }

    // Flush and prepare for read
    while (wl_display_prepare_read(app.display) != 0) {
      wl_display_dispatch_pending(app.display);
    }
    wl_display_flush(app.display);

    // Poll for events
    struct pollfd pfd = {};
    pfd.fd = display_fd;
    pfd.events = POLLIN;
    // 4. Wait for events (with dynamic timeout for animations)
    int timeout = -1; // Wait indefinitely if no animation
    auto it_cache = app.cache.find(app.current_index); // Declare it_cache here

    if (it_cache != app.cache.end() && it_cache->second.frames.size() > 1) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - app.last_frame_time).count();
        int delay = it_cache->second.delays[app.current_frame_index % it_cache->second.frames.size()];
        if (delay <= 0) delay = 100;
        
        timeout = std::max(0, (int)(delay - elapsed));
    }
    // If a redraw is already pending or we are animating zoom/pan, keep it snappy
    if (app.redraw_pending || app.zooming_in || app.zooming_out) timeout = 0;

    int ret = poll(&pfd, 1, timeout);
    if (ret > 0) {
      wl_display_read_events(app.display);
    } else {
      wl_display_cancel_read(app.display);
    }
    wl_display_dispatch_pending(app.display);
  }

  return 0;
}
