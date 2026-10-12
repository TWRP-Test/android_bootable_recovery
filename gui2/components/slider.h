#pragma once

#include "lvgl.h"

namespace gui2_components {

struct slider {
  lv_obj_t* object = nullptr;
};

lv_obj_t* create_slider(lv_obj_t* parent, int x, int y, int width, int height, int minimum,
                        int maximum, int value, lv_color_t background, lv_color_t foreground,
                        lv_color_t thumb, slider* component);

int get_value(const slider* component);

void set_enabled(slider* component, bool enabled);
void set_value(slider* component, int value);
void refresh_slider(slider* component);

}  // namespace gui2_components
