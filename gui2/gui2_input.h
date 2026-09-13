#ifndef GUI2_INPUT_H
#define GUI2_INPUT_H

#include "lvgl.h"

lv_indev_t* gui2_input_init(void);
bool gui2_input_take_activity(void);

// Return and clear accumulated mouse-wheel steps received since the last
// call. Positive values correspond to wheel-up events from Linux input.
int gui2_input_take_wheel(void);

#endif
