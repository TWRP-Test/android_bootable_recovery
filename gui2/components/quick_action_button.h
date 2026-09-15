#ifndef GUI2_COMPONENTS_QUICK_ACTION_BUTTON_H
#define GUI2_COMPONENTS_QUICK_ACTION_BUTTON_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

lv_obj_t* create_quick_action_button(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                                     const lv_image_dsc_t* icon_source, const char* text, int x,
                                     int y, int width, const void* user_data,
                                     lv_event_cb_t click_callback,
                                     lv_event_cb_t press_guard_callback,
                                     lv_event_cb_t gesture_callback);

}  // namespace gui2_components

#endif
