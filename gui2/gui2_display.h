#ifndef GUI2_DISPLAY_H
#define GUI2_DISPLAY_H

#include <cstdint>

#include "lvgl.h"

#include "backend/screen_backend.h"

lv_display_t* gui2_display_init(void);
void gui2_display_deinit(void);

// Present the refresh and submit the complete capture frame.
bool gui2_display_present(gui2_backend::screen_backend* screen, uint64_t monotonic_ms);

#endif
