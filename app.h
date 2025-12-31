#ifndef APP_H
#define APP_H

#include <linux/input-event-codes.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "pointer-gestures-unstable-v1-client-protocol.h"

// Forward declarations for Wayland listener structs
extern const struct wl_registry_listener registry_listener;
extern const struct wl_seat_listener seat_listener;
extern const struct xdg_wm_base_listener xdg_wm_base_listener;
extern const struct xdg_surface_listener xdg_surface_listener;
extern const struct xdg_toplevel_listener xdg_toplevel_listener;

#include <map>
#include <chrono>

struct CachedImage {
  std::vector<uint8_t*> frames;
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
  bool configured;
  int running;

  std::map<size_t, CachedImage> cache;
  int32_t orig_width, orig_height;
  uint8_t *orig_data;
  
  // Animation state
  int current_frame_index;
  std::chrono::steady_clock::time_point last_frame_time;

  struct wl_seat *seat;
  struct wl_keyboard *keyboard;
  struct wl_pointer *pointer;
  uint32_t modifiers;

  std::vector<std::string> images;
  size_t current_index;
  bool show_info;
  
  double mouse_x, mouse_y;
  float zoom;
  float target_zoom;
  float base_zoom;
  bool zooming_in, zooming_out;
  struct zwp_pointer_gestures_v1 *gestures;

  int shm_fd;
  void *shm_data;
  size_t shm_size;
  bool redraw_pending;
  struct wl_callback *frame_callback;
  float pan_x, pan_y;
  float target_pan_x, target_pan_y;
  bool is_panning;
  double last_mouse_x, last_mouse_y;

  std::chrono::steady_clock::time_point last_interaction_time;
};

void die(const char *msg);

#endif
