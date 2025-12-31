#ifndef LOADER_H
#define LOADER_H

#include "app.h"

void load_image(struct app_state *app, size_t index);
void scan_directory(struct app_state *app, const char *filepath);

#endif
