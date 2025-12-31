#include "loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "renderer.h"
#include <dirent.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

void load_image(struct app_state *app, size_t index) {
  if (index >= app->images.size()) return;

  app->current_index = index;
  app->current_frame_index = 0;
  app->last_frame_time = std::chrono::steady_clock::now();

  int window = 3;
  int start = (int)index - window;
  int end = (int)index + window;

  // Unload images outside window
  for (auto it = app->cache.begin(); it != app->cache.end(); ) {
      if ((int)it->first < start || (int)it->first > end) {
          for (uint8_t *f : it->second.frames) stbi_image_free(f);
          it = app->cache.erase(it);
      } else {
          ++it;
      }
  }

  // Helper to load any single image or GIF
  auto perform_load = [&](size_t idx) -> bool {
      if (app->cache.find(idx) != app->cache.end()) return true;

      int w, h, n;
      std::string path = app->images[idx];
      bool is_gif = (path.size() > 4 && path.substr(path.size() - 4) == ".gif");

      CachedImage ci = {};
      
      if (is_gif) {
          FILE *f = fopen(path.c_str(), "rb");
          if (!f) return false;
          fseek(f, 0, SEEK_END);
          size_t size = ftell(f);
          fseek(f, 0, SEEK_SET);
          uint8_t *buffer = (uint8_t*)malloc(size);
          fread(buffer, 1, size, f);
          fclose(f);

          int *delays = nullptr;
          int frames_count = 0;
          uint8_t *data = stbi_load_gif_from_memory(buffer, (int)size, &delays, &w, &h, &frames_count, &n, 4);
          free(buffer);

          if (data) {
              size_t frame_size = w * h * 4;
              for (int i = 0; i < frames_count; ++i) {
                  uint8_t *frame = (uint8_t*)malloc(frame_size);
                  memcpy(frame, data + i * frame_size, frame_size);
                  // Swizzle
                  for (int j = 0; j < w * h; ++j) {
                      uint8_t *p = &frame[j * 4];
                      uint8_t r = p[0], b = p[2];
                      p[0] = b; p[2] = r;
                  }
                  ci.frames.push_back(frame);
                  ci.delays.push_back(delays[i]);
              }
              stbi_image_free(data);
              free(delays);
              ci.width = w; ci.height = h;
              app->cache[idx] = ci;
              return true;
          }
      } else {
          uint8_t *data = stbi_load(path.c_str(), &w, &h, &n, 4);
          if (data) {
              for (int j = 0; j < w * h; ++j) {
                  uint8_t *p = &data[j * 4];
                  uint8_t r = p[0], b = p[2];
                  p[0] = b; p[2] = r;
              }
              ci.frames.push_back(data);
              ci.delays.push_back(0);
              ci.width = w; ci.height = h;
          }
      }

      // Extract EXIF metadata once during load
      if (!ci.frames.empty()) {
          std::string cmd = "exiv2 -pt \"" + path + "\" 2>/dev/null";
          FILE *fp = popen(cmd.c_str(), "r");
          if (fp) {
              char buf[512];
              while (fgets(buf, sizeof(buf), fp)) {
                  std::string line(buf);
                  if (line.find("Make") != std::string::npos || 
                      line.find("Model") != std::string::npos || 
                      line.find("ExposureTime") != std::string::npos || 
                      line.find("FNumber") != std::string::npos || 
                      line.find("ISOSpeedRatings") != std::string::npos ||
                      line.find("DateTimeOriginal") != std::string::npos) {
                      
                      int spaces = 0;
                      size_t val_pos = 0;
                      for(size_t i=0; i<line.length(); ++i) {
                          if (isspace(line[i])) {
                              while(i < line.length() && isspace(line[i])) i++;
                              spaces++;
                              if (spaces == 3) { val_pos = i; break; }
                              i--;
                          }
                      }

                      if (val_pos > 0) {
                          std::string key = line.substr(0, line.find(" "));
                          size_t dot = key.find_last_of('.');
                          if (dot != std::string::npos) key = key.substr(dot + 1);
                          std::string val = line.substr(val_pos);
                          if (!val.empty() && val.back() == '\n') val.pop_back();
                          ci.exif_data.push_back(key + ": " + val);
                      }
                  }
              }
              pclose(fp);
          }
          if (ci.exif_data.empty()) {
              ci.exif_data.push_back("No photographic EXIF data found");
          }

          app->cache[idx] = ci;
          return true;
      }
      return false;
  };

  if (perform_load(index)) {
      app->orig_data = app->cache[index].frames[0];
      app->orig_width = app->cache[index].width;
      app->orig_height = app->cache[index].height;
  }

  // Proactively load neighbors
  for (int i = start; i <= end; ++i) {
      if (i < 0 || i >= (int)app->images.size() || i == (int)index) continue;
      perform_load((size_t)i);
  }

  if (app->configured) app->redraw_pending = true;
}

void scan_directory(struct app_state *app, const char *filepath) {
  std::string full_path;
  if (filepath[0] == '/') {
      full_path = filepath;
  } else {
      char cwd[1024];
      if (getcwd(cwd, sizeof(cwd))) {
          full_path = std::string(cwd) + "/" + filepath;
      } else {
          full_path = filepath;
      }
  }

  size_t last_slash = full_path.find_last_of("/");
  std::string dir = (last_slash == std::string::npos) ? "." : full_path.substr(0, last_slash);
  std::string filename = (last_slash == std::string::npos) ? full_path : full_path.substr(last_slash + 1);

  DIR *dp = opendir(dir.c_str());
  if (!dp) return;

  app->images.clear();
  struct dirent *entry;
  while ((entry = readdir(dp))) {
    std::string name = entry->d_name;
    std::string ext = "";
    size_t last_dot = name.find_last_of(".");
    if (last_dot != std::string::npos) ext = name.substr(last_dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" || ext == ".gif") {
      app->images.push_back(dir + "/" + name);
    }
  }
  closedir(dp);
  std::sort(app->images.begin(), app->images.end());

  for (size_t i = 0; i < app->images.size(); ++i) {
    if (app->images[i].find(filename) != std::string::npos) {
      app->current_index = i;
      break;
    }
  }
}
