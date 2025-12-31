#include "renderer.h"
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <cairo.h>
#include "loader.h"

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

void draw_icon(cairo_t *cr, double x, double y, int type) {
  cairo_save(cr);
  cairo_translate(cr, x + 20, y + 20);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
  cairo_set_line_width(cr, 2.0);

  if (type == 0) { // Left arrow
    cairo_move_to(cr, 8, -10);
    cairo_line_to(cr, -7, 0);
    cairo_line_to(cr, 8, 10);
    cairo_stroke(cr);
  } else if (type == 1) { // Right arrow
    cairo_move_to(cr, -8, -10);
    cairo_line_to(cr, 7, 0);
    cairo_line_to(cr, -8, 10);
    cairo_stroke(cr);
  } else if (type == 2) { // Circled 'i'
    cairo_arc(cr, 0, 0, 12, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_set_line_width(cr, 3.0);
    cairo_move_to(cr, 0, -5);
    cairo_line_to(cr, 0, -4);
    cairo_stroke(cr);
    cairo_move_to(cr, 0, -1);
    cairo_line_to(cr, 0, 5);
    cairo_stroke(cr);
  }
  cairo_restore(cr);
}

void create_buffer(struct app_state *app) {
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, app->width);
  size_t size = stride * app->height;

  if (app->shm_fd == -1 || app->shm_size != size) {
    if (app->shm_data) munmap(app->shm_data, app->shm_size);
    if (app->shm_fd != -1) close(app->shm_fd);

    app->shm_fd = create_shm_file(size);
    if (app->shm_fd == -1) die("create_shm_file failed");
    app->shm_data = (uint8_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, app->shm_fd, 0);
    app->shm_size = size;

    if (app->buffer) wl_buffer_destroy(app->buffer);
    struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, app->shm_fd, size);
    app->buffer = wl_shm_pool_create_buffer(pool, 0, app->width, app->height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
  }

  // Draw directly to SHM
  cairo_surface_t *surface = cairo_image_surface_create_for_data((unsigned char*)app->shm_data, CAIRO_FORMAT_ARGB32, app->width, app->height, stride);
  cairo_t *cr = cairo_create(surface);

  // Background
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  // Render Image
  auto it = app->cache.find(app->current_index);
  if (it != app->cache.end() && !it->second.frames.empty()) {
    uint8_t *frame_data = it->second.frames[app->current_frame_index % it->second.frames.size()];
    cairo_surface_t *img_surface = cairo_image_surface_create_for_data(
        frame_data, CAIRO_FORMAT_ARGB32, it->second.width, it->second.height, it->second.width * 4);
    
    double window_aspect = (double)app->width / app->height;
    double image_aspect = (double)it->second.width / it->second.height;

    double draw_w, draw_h;
    if (window_aspect > image_aspect) {
      draw_h = app->height * app->zoom;
      draw_w = draw_h * image_aspect;
    } else {
      draw_w = app->width * app->zoom;
      draw_h = draw_w / image_aspect;
    }

    double scale_x = draw_w / it->second.width;
    double scale_y = draw_h / it->second.height;
    double offset_x = (app->width - draw_w) / 2.0 + app->pan_x;
    double offset_y = (app->height - draw_h) / 2.0 + app->pan_y;

    cairo_save(cr);
    cairo_translate(cr, offset_x, offset_y);
    cairo_scale(cr, scale_x, scale_y);
    cairo_set_source_surface(cr, img_surface, 0, 0);

    // Adaptive filtering: use FAST during active interaction, BILINEAR when idle
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - app->last_interaction_time).count();
    
    if (elapsed_ms < 100 || app->zooming_in || app->zooming_out) {
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
    } else {
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    }
    
    cairo_paint(cr);
    cairo_restore(cr);
    
    cairo_surface_destroy(img_surface);
  }

  // Overlay UI
  if (app->show_info) {
    std::vector<std::string> lines;
    lines.push_back(app->images[app->current_index]);
    lines.push_back("Res: " + std::to_string(app->orig_width) + "x" + std::to_string(app->orig_height));
    lines.push_back("Zoom: " + std::to_string(app->zoom).substr(0,4) + "x | Index: " + std::to_string(app->current_index + 1) + "/" + std::to_string(app->images.size()));

    // Use cached metadata
    if (it != app->cache.end()) {
        for (const auto& line : it->second.exif_data) {
            lines.push_back(line);
        }
    }

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18.0);

    // Calculate max width for dynamic background
    double max_w = 200;
    for (const auto& line : lines) {
      cairo_text_extents_t extents;
      cairo_text_extents(cr, line.c_str(), &extents);
      if (extents.width > max_w) max_w = extents.width;
    }
    
    double text_bg_w = max_w + 40;
    double text_bg_h = lines.size() * 25 + 30;

    // Draw background for info text
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.6); // Translucent black
    cairo_new_sub_path(cr);
    cairo_arc(cr, 20 + 10, 20 + 10, 10, M_PI, 1.5 * M_PI);
    cairo_arc(cr, 20 + text_bg_w - 10, 20 + 10, 10, 1.5 * M_PI, 2 * M_PI);
    cairo_arc(cr, 20 + text_bg_w - 10, 20 + text_bg_h - 10, 10, 0, 0.5 * M_PI);
    cairo_arc(cr, 20 + 10, 20 + text_bg_h - 10, 10, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.9, 0.9, 0.9, 1.0); // Dull white
    for (size_t i = 0; i < lines.size(); ++i) {
      cairo_move_to(cr, 40, 50 + i * 25);
      cairo_show_text(cr, lines[i].c_str());
    }
  }

  // Draw Floating UI Buttons (Tray)
  int btn_w = 40, btn_h = 40, spacing = 20;
  int tray_w = 3 * btn_w + 4 * spacing;
  int tray_h = btn_h + 20;
  double tray_x = (app->width - tray_w) / 2.0;
  double tray_y = app->height - tray_h - 20;

  // Only show tray if mouse is near bottom or we want it always visible
  // For now, let's keep it visible when mouse is in the lower 15% of screen
  if (app->mouse_y > app->height * 0.85) {
      cairo_save(cr);
      cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.7); // Dark translucent
      cairo_new_sub_path(cr);
      cairo_arc(cr, tray_x + 15, tray_y + 15, 15, M_PI, 1.5 * M_PI);
      cairo_arc(cr, tray_x + tray_w - 15, tray_y + 15, 15, 1.5 * M_PI, 2 * M_PI);
      cairo_arc(cr, tray_x + tray_w - 15, tray_y + tray_h - 15, 15, 0, 0.5 * M_PI);
      cairo_arc(cr, tray_x + 15, tray_y + tray_h - 15, 15, 0.5 * M_PI, M_PI);
      cairo_close_path(cr);
      cairo_fill(cr);

      // Draw Icons
      double start_x = tray_x + spacing;
      double start_y = tray_y + 10;
      draw_icon(cr, start_x, start_y, 0); // Prev
      draw_icon(cr, start_x + btn_w + spacing, start_y, 2); // Info
      draw_icon(cr, start_x + 2 * (btn_w + spacing), start_y, 1); // Next
      cairo_restore(cr);
  }

  cairo_destroy(cr);
  cairo_surface_destroy(surface);
}
