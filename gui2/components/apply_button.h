#ifndef GUI2_COMPONENTS_APPLY_BUTTON_H
#define GUI2_COMPONENTS_APPLY_BUTTON_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

lv_obj_t* create_apply_button(lv_obj_t* page_layer, const gui2_core::ui_metrics& metrics,
                              lv_event_cb_t callback, const char* text,
                              lv_event_cb_t press_guard_callback);

}  // namespace gui2_components

#endif
