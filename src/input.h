#ifndef INPUT_H
#define INPUT_H

#include "app.h"

extern const struct wl_registry_listener registry_listener;
extern const struct wl_seat_listener seat_listener;
extern const struct xdg_wm_base_listener xdg_wm_base_listener;
extern const struct xdg_surface_listener xdg_surface_listener;
extern const struct xdg_toplevel_listener xdg_toplevel_listener;

#endif
