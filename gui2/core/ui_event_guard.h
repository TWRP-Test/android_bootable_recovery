#ifndef GUI2_CORE_UI_EVENT_GUARD_H
#define GUI2_CORE_UI_EVENT_GUARD_H

#include "backend/hardware_settings.h"
#include "lvgl.h"

namespace gui2_core {

void configure_click_guard(lv_indev_t* pointer_indev,
                           gui2_backend::hardware_settings* hardware);
void press_cancel_guard_cb(lv_event_t* event);
void add_press_cancel_guard(lv_obj_t* object);
bool accept_click(lv_event_t* event);
// A confirmed gesture is not a click, so it never reaches accept_click. The
// guard already owns the hardware handle, so components borrow it from here.
void vibrate_action();
void clear_click_guard();
void reset_click_guard();

}  // namespace gui2_core

#endif
