#ifndef GUI2_DISPLAY_H
#define GUI2_DISPLAY_H

#include "lvgl.h"

lv_display_t* gui2_display_init(void);
void gui2_display_deinit(void);

// Submit the completed LVGL frame to minui.  LVGL may call flush_cb multiple
// times for one frame, so presentation must happen only after the handler has
// finished all of its flushes.
bool gui2_display_present(void);

#endif
