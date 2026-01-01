#include "loader.h"
#include <Imlib2.h>
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
          for (Imlib_Image f : it->second.frames) {
              imlib_context_set_image(f);
              imlib_free_image();
          }
          it = app->cache.erase(it);
      } else {
          ++it;
      }
  }

  // Helper to load any single image or GIF
  auto perform_load = [&](size_t idx) -> bool {
      if (app->cache.find(idx) != app->cache.end()) return true;

      std::string path = app->images[idx];
      Imlib_Image img = imlib_load_image(path.c_str());
      if (!img) return false;

      imlib_context_set_image(img);
      int w = imlib_image_get_width();
      int h = imlib_image_get_height();

      CachedImage ci = {};
      ci.width = w;
      ci.height = h;

      // Store the Imlib_Image directly
      ci.frames.push_back(img);
      ci.delays.push_back(0);
      
      // Do not free img here, we cache it

      // Extract EXIF metadata once during load

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

  perform_load(index);

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
