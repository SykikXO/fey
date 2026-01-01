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
  int draw_width = app->width * app->buffer_scale;
  int draw_height = app->height * app->buffer_scale;
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, draw_width);
  size_t size = (size_t)stride * draw_height;

  if (app->shm_fd == -1 || app->shm_size != size) {
    if (app->shm_data) munmap(app->shm_data, app->shm_size);
    if (app->shm_fd != -1) close(app->shm_fd);

    app->shm_fd = create_shm_file(size);
    if (app->shm_fd == -1) die("create_shm_file failed");
    app->shm_data = (uint8_t*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, app->shm_fd, 0);
    app->shm_size = size;

    if (app->buffer) wl_buffer_destroy(app->buffer);
    struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, app->shm_fd, size);
    app->buffer = wl_shm_pool_create_buffer(pool, 0, draw_width, draw_height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
  }

  // Draw directly to SHM
  cairo_surface_t *surface = cairo_image_surface_create_for_data((unsigned char*)app->shm_data, CAIRO_FORMAT_ARGB32, draw_width, draw_height, stride);
  cairo_t *cr = cairo_create(surface);
  
  // Scale for HiDPI
  cairo_scale(cr, app->buffer_scale, app->buffer_scale);

  // Background
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_paint(cr);

  // Render Image
  auto it = app->cache.find(app->current_index);
  if (it != app->cache.end() && !it->second.frames.empty()) {
    Imlib_Image src_img = it->second.frames[app->current_frame_index % it->second.frames.size()];
    
    // Check if we are in "Active" mode (Performance critical) or "Idle" mode (Quality critical)
    bool fast_mode = (app->zooming_in || app->zooming_out || app->is_panning || app->is_animating);
    
    // Also include the 100ms debounce
    if (!fast_mode) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - app->last_interaction_time).count();
        if (elapsed_ms < 100) fast_mode = true;
    }

    if (fast_mode) {
        // --- FAST PATH (Cairo) ---
        // Imlib2 images are ARGB32, compatible with Cairo
        imlib_context_set_image(src_img);
        int w = imlib_image_get_width();
        int h = imlib_image_get_height();
        uint32_t *data = imlib_image_get_data_for_reading_only();
        
        cairo_surface_t *img_surface = cairo_image_surface_create_for_data(
            (unsigned char*)data, CAIRO_FORMAT_ARGB32, w, h, w * 4);
        
        double window_aspect = (double)app->width / app->height;
        double image_aspect = (double)w / h;

        double draw_w, draw_h;
        if (window_aspect > image_aspect) {
          draw_h = app->height * app->zoom;
          draw_w = draw_h * image_aspect;
        } else {
          draw_w = app->width * app->zoom;
          draw_h = draw_w / image_aspect;
        }

        double scale_x = draw_w / w;
        double scale_y = draw_h / h;
        double offset_x = (app->width - draw_w) / 2.0 + app->pan_x;
        double offset_y = (app->height - draw_h) / 2.0 + app->pan_y;

        cairo_save(cr);
        cairo_translate(cr, offset_x, offset_y);
        cairo_scale(cr, scale_x, scale_y);
        cairo_set_source_surface(cr, img_surface, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_FAST);
        cairo_paint(cr);
        cairo_restore(cr);
        
        cairo_surface_destroy(img_surface);
        
    } else {
        // --- QUALITY PATH (Imlib2) ---
        Imlib_Image dest_img = imlib_create_image_using_data(draw_width, draw_height, (unsigned int*)app->shm_data);
        if (dest_img) {
            imlib_context_set_image(dest_img);
            
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

            double final_x = (app->width - draw_w) / 2.0 + app->pan_x;
            double final_y = (app->height - draw_h) / 2.0 + app->pan_y;

            int target_x = (int)(final_x * app->buffer_scale);
            int target_y = (int)(final_y * app->buffer_scale);
            int target_w = (int)(draw_w * app->buffer_scale);
            int target_h = (int)(draw_h * app->buffer_scale);

            imlib_context_set_anti_alias(1);
            imlib_blend_image_onto_image(src_img, 0, 0, 0, it->second.width, it->second.height, 
                                         target_x, target_y, target_w, target_h);
            imlib_free_image();
        }
    }
  }

  // Draw UI with Cairo (on top of what Imlib2 just drew)
  // Re-establish Cairo scaling for UI elements
  cairo_identity_matrix(cr);
  cairo_scale(cr, app->buffer_scale, app->buffer_scale);

  // Overlay UI
  if (app->show_info) {
    std::vector<std::string> lines;
    int w = 0, h = 0;
    if (it != app->cache.end()) { w = it->second.width; h = it->second.height; }
    lines.push_back(app->images[app->current_index]);
    lines.push_back("Res: " + std::to_string(w) + "x" + std::to_string(h));
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

    cairo_set_source_rgba(cr, 1, 1, 1, 1.0); // white    
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

  // Only show tray if mouse is near bottom (75%) and app is not zooming or panning
  if (app->mouse_y > app->height * 0.75 && !(app->zooming_in || app->zooming_out || app->is_panning)) {
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
