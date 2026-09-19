#ifndef GUI2_COMPONENTS_KEYBOARD_MAPS_H
#define GUI2_COMPONENTS_KEYBOARD_MAPS_H

#include "lvgl.h"

namespace gui2_components {

// Replaces the stock keyboard layouts and the handler that goes with them. The
// stock rows carry a keyboard glyph in one corner that reads as "show" rather
// than "hide"; these carry a hide arrow in both bottom corners, and the number
// pad gets one too.
//
// LVGL holds one map table shared by every keyboard, so the layouts land on all
// of them; the handler is swapped per keyboard.
void install_keyboard_maps(lv_obj_t* keyboard);

}  // namespace gui2_components

#endif  // GUI2_COMPONENTS_KEYBOARD_MAPS_H
