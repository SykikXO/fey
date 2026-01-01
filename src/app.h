#ifndef APP_H
#define APP_H

#include <linux/input-event-codes.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <wayland-client.h>
#include "protocols/xdg-shell-client-protocol.h"
#include "protocols/pointer-gestures-unstable-v1-client-protocol.h"

// Forward declarations for Wayland listener structs
extern const struct wl_registry_listener registry_listener;
extern const struct wl_seat_listener seat_listener;
extern const struct xdg_wm_base_listener xdg_wm_base_listener;
extern const struct xdg_surface_listener xdg_surface_listener;
extern const struct xdg_toplevel_listener xdg_toplevel_listener;

#include <map>
#include <chrono>
#include <Imlib2.h>

struct CachedImage {
  std::vector<Imlib_Image> frames;
  std::vector<int> delays; // in milliseconds
  std::vector<std::string> exif_data;
  int width, height;
};

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
  int32_t buffer_scale; // HiDPI scale factor
  bool configured;
  int running;

  std::map<size_t, CachedImage> cache;
  
  // Animation state
  int current_frame_index;
  std::chrono::steady_clock::time_point last_frame_time;
  bool is_animating; // actively animating (physics/rebound)

  struct wl_seat *seat;
  struct wl_keyboard *keyboard;
  struct wl_pointer *pointer;
  uint32_t modifiers;

  std::vector<std::string> images;
  size_t current_index;
  bool show_info;
  
  double mouse_x, mouse_y;
  float zoom;
  float target_zoom; // Target zoom for lerp animation
  float base_zoom;   // Used for pinch gesture reference
  bool zooming_in, zooming_out; // Flags for continuous keyboard zoom
  struct zwp_pointer_gestures_v1 *gestures;

  int shm_fd;
  void *shm_data;
  size_t shm_size;
  bool redraw_pending;
  bool needs_hq_update; // Flag to ensure we trigger a final high-quality redraw
  struct wl_callback *frame_callback;
  float pan_x, pan_y;
  float target_pan_x, target_pan_y; // Target pan for rebound animation
  bool is_panning; // Active mouse-drag panning flag
  double last_mouse_x, last_mouse_y;

  std::chrono::steady_clock::time_point last_interaction_time;
  bool fullscreen;
};

void die(const char *msg);

#endif
