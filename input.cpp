#include "input.h"
#include "renderer.h"
#include "loader.h"
#include "pointer-gestures-unstable-v1-client-protocol.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

static void pinch_begin(void *data, struct zwp_pointer_gesture_pinch_v1 *pinch, uint32_t serial, uint32_t time, struct wl_surface *surface, uint32_t fingers) {
  (void)pinch; (void)serial; (void)time; (void)surface; (void)fingers;
  struct app_state *app = static_cast<struct app_state*>(data);
  app->base_zoom = app->zoom;
}

static void redraw(struct app_state *app) {
  app->redraw_pending = true;
  app->last_interaction_time = std::chrono::steady_clock::now();
}

static void pinch_update(void *data, struct zwp_pointer_gesture_pinch_v1 *pinch, uint32_t time, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t scale, wl_fixed_t rotation) {
  (void)pinch; (void)time; (void)rotation;
  struct app_state *app = static_cast<struct app_state*>(data);
  float s = wl_fixed_to_double(scale);
  // Allow slightly more than limits for rubber-band effect (snap back happens in main.cpp)
  app->zoom = std::clamp(app->base_zoom * s, 0.03f, 15.0f);
  
  app->pan_x += wl_fixed_to_double(dx);
  app->pan_y += wl_fixed_to_double(dy);
  
  redraw(app);
}

static void pinch_end(void *data, struct zwp_pointer_gesture_pinch_v1 *pinch, uint32_t serial, uint32_t time, int32_t cancelled) {
  (void)data; (void)pinch; (void)serial; (void)time; (void)cancelled;
}

static const struct zwp_pointer_gesture_pinch_v1_listener pinch_listener = {
  .begin = pinch_begin,
  .update = pinch_update,
  .end = pinch_end,
};

// Keyboard handler for shortcuts and continuous zoom
static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
  (void)keyboard; (void)serial; (void)time;
  struct app_state *app = static_cast<struct app_state*>(data);
  bool pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);

  if (pressed) {
    if (key == KEY_Q) {
      app->running = 0;
    } else if (key == KEY_RIGHT) {
      if (app->modifiers & (1 << 2)) { // Ctrl + Right
        app->pan_x -= 30 / app->zoom;
        redraw(app);
      } else {
        app->pan_x = app->pan_y = 0; // Reset pan on switch
        load_image(app, (app->current_index + 1) % app->images.size());
      }
    } else if (key == KEY_LEFT) {
      if (app->modifiers & (1 << 2)) { // Ctrl + Left
        app->pan_x += 30 / app->zoom;
        redraw(app);
      } else {
        app->pan_x = app->pan_y = 0;
        load_image(app, (app->current_index + app->images.size() - 1) % app->images.size());
      }
    } else if (key == KEY_UP) {
      if (app->modifiers & (1 << 2)) { // Ctrl + Up
        app->pan_y += 30 / app->zoom;
        redraw(app);
      }
    } else if (key == KEY_DOWN) {
      if (app->modifiers & (1 << 2)) { // Ctrl + Down
        app->pan_y -= 30 / app->zoom;
        redraw(app);
      }
    } else if (key == KEY_I) {
      app->show_info = !app->show_info;
      redraw(app);
    } else if (key == KEY_EQUAL || key == KEY_KPPLUS) {
      // Discrete Zoom Step In
      app->zoom = std::min(app->zoom + 0.1f, 10.0f);
      redraw(app);
    } else if (key == KEY_MINUS || key == KEY_KPMINUS) {
      // Discrete Zoom Step Out
      app->zoom = std::max(app->zoom - 0.1f, 0.05f);
      redraw(app);
    }
  }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
  (void)keyboard; (void)serial; (void)mods_latched; (void)mods_locked; (void)group;
  struct app_state *app = static_cast<struct app_state*>(data);
  app->modifiers = mods_depressed;
}

static const struct wl_keyboard_listener keyboard_listener = {
  .keymap = [](void*, struct wl_keyboard*, uint32_t, int, uint32_t) {},
  .enter = [](void*, struct wl_keyboard*, uint32_t, struct wl_surface*, struct wl_array*) {},
  .leave = [](void*, struct wl_keyboard*, uint32_t, struct wl_surface*) {},
  .key = keyboard_key,
  .modifiers = keyboard_modifiers,
  .repeat_info = [](void*, struct wl_keyboard*, int32_t, int32_t) {},
};

// Pointer handler for mouse clicks, tray UI, and panning
static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
  (void)pointer; (void)serial; (void)time;
  struct app_state *app = static_cast<struct app_state*>(data);
  bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);

  if (button == BTN_LEFT) {
    int btn_w = 40, btn_h = 40, spacing = 20;
    int tray_w = 3 * btn_w + 4 * spacing;
    int tray_h = btn_h + 20;
    double tray_x = (app->width - tray_w) / 2.0;
    double tray_y = app->height - tray_h - 20;

    double start_x = tray_x + spacing;
    double start_y = tray_y + 10;

    bool on_tray = (app->mouse_x >= tray_x && app->mouse_x <= tray_x + tray_w &&
                    app->mouse_y >= tray_y && app->mouse_y <= tray_y + tray_h);

    if (pressed) {
      if (on_tray) {
        // UI Tray interaction
        if (app->mouse_y >= start_y && app->mouse_y <= start_y + btn_h) {
          if (app->mouse_x >= start_x && app->mouse_x <= start_x + btn_w) {
            app->pan_x = app->pan_y = 0;
            load_image(app, (app->current_index + app->images.size() - 1) % app->images.size());
          } else if (app->mouse_x >= start_x + btn_w + spacing && app->mouse_x <= start_x + 2 * btn_w + spacing) {
            app->show_info = !app->show_info;
            redraw(app);
          } else if (app->mouse_x >= start_x + 2 * (btn_w + spacing) && app->mouse_x <= start_x + 3 * btn_w + 2 * spacing) {
            app->pan_x = app->pan_y = 0;
            load_image(app, (app->current_index + 1) % app->images.size());
          }
        }
      } else {
        // Start click-and-drag panning
        app->is_panning = true;
        app->last_mouse_x = app->mouse_x;
        app->last_mouse_y = app->mouse_y;
      }
    } else {
      app->is_panning = false;
    }
  }
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
  (void)pointer; (void)time;
  struct app_state *app = static_cast<struct app_state*>(data);
  double old_y = app->mouse_y;
  app->mouse_x = wl_fixed_to_double(surface_x);
  app->mouse_y = wl_fixed_to_double(surface_y);

  // Throttling: only redraw if panning or if mouse crosses UI tray area
  int tray_h = 60; // Approximate tray area height at bottom
  bool redraw_needed = app->is_panning;
  
  if (!redraw_needed) {
      // Check if mouse entered or left the potential tray area
      bool was_near_bottom = (old_y > app->height - tray_h);
      bool is_near_bottom = (app->mouse_y > app->height - tray_h);
      if (was_near_bottom != is_near_bottom) redraw_needed = true;
  }

  if (app->is_panning) {
    app->pan_x += (app->mouse_x - app->last_mouse_x);
    app->pan_y += (app->mouse_y - app->last_mouse_y);
    app->last_mouse_x = app->mouse_x;
    app->last_mouse_y = app->mouse_y;
  }

  if (redraw_needed) redraw(app);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
  (void)pointer; (void)time;
  struct app_state *app = static_cast<struct app_state*>(data);
  double v = wl_fixed_to_double(value);
  
  if (app->modifiers & (1 << 0)) { // Shift for zooming maybe? Or just use wheel
      // Keep wheel for zoom if preferred, but user wants panning
  }

  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
    if (app->modifiers & (1 << 0)) { // Shift + Scroll = Zoom
        if (v < 0) app->zoom = std::min(app->zoom * 1.1f, 10.0f);
        else app->zoom = std::max(app->zoom * 0.9f, 0.1f);
    } else {
        app->pan_y -= v;
    }
    redraw(app);
  } else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
    app->pan_x -= v;
    redraw(app);
  }
}

static const struct wl_pointer_listener pointer_listener = {
  .enter = [](void*, struct wl_pointer*, uint32_t, struct wl_surface*, wl_fixed_t, wl_fixed_t) {},
  .leave = [](void*, struct wl_pointer*, uint32_t, struct wl_surface*) {},
  .motion = pointer_motion,
  .button = pointer_button,
  .axis = pointer_axis,
  .frame = [](void*, struct wl_pointer*) {},
  .axis_source = [](void*, struct wl_pointer*, uint32_t) {},
  .axis_stop = [](void*, struct wl_pointer*, uint32_t, uint32_t) {},
  .axis_discrete = [](void*, struct wl_pointer*, uint32_t, int32_t) {},
  .axis_value120 = [](void*, struct wl_pointer*, uint32_t, int32_t) {},
  .axis_relative_direction = [](void*, struct wl_pointer*, uint32_t, uint32_t) {},
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
  struct app_state *app = static_cast<struct app_state*>(data);
  if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
    app->keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
  }
  if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
    app->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    
    if (app->gestures) {
      struct zwp_pointer_gesture_pinch_v1 *pinch = zwp_pointer_gestures_v1_get_pinch_gesture(app->gestures, app->pointer);
      zwp_pointer_gesture_pinch_v1_add_listener(pinch, &pinch_listener, app);
    }
  }
}

const struct wl_seat_listener seat_listener = {
  .capabilities = seat_capabilities,
  .name = [](void*, struct wl_seat*, const char*) {},
};

void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
  (void)xdg_toplevel; (void)states;
  struct app_state *app = static_cast<struct app_state*>(data);
  if (width > 0 && height > 0) {
    if (app->width != width || app->height != height) {
        app->width = width;
        app->height = height;
        app->configured = true;
        redraw(app);
    }
  }
}

void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
  (void)xdg_toplevel;
  struct app_state *app = static_cast<struct app_state*>(data);
  app->running = 0;
}

const struct xdg_toplevel_listener xdg_toplevel_listener = {
  .configure = xdg_toplevel_configure,
  .close = xdg_toplevel_close,
  .configure_bounds = [](void*, struct xdg_toplevel*, int32_t, int32_t) {},
  .wm_capabilities = [](void*, struct xdg_toplevel*, struct wl_array*) {},
};

void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
  struct app_state *app = static_cast<struct app_state*>(data);
  xdg_surface_ack_configure(xdg_surface, serial);
  app->configured = true;
  redraw(app);
}

const struct xdg_surface_listener xdg_surface_listener = {
  .configure = xdg_surface_configure,
};

void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
  (void)data;
  xdg_wm_base_pong(xdg_wm_base, serial);
}

const struct xdg_wm_base_listener xdg_wm_base_listener = {
  .ping = xdg_wm_base_ping,
};
