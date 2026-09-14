#ifndef GUI2_INPUT_H
#define GUI2_INPUT_H

#include "lvgl.h"

enum class gui2_key_action {
  TOGGLE_SCREEN,
  SCREENSHOT,
  BACK,
};

lv_indev_t* gui2_input_init(void);
bool gui2_input_take_activity(void);

// Consume wake-up touches without activating the control underneath.
void gui2_input_set_screen_off(bool screen_off);

// Returns one pending hardware-key action.
bool gui2_input_take_key_action(gui2_key_action* action);

// Returns and clears accumulated mouse-wheel steps.
int gui2_input_take_wheel(void);

#endif
