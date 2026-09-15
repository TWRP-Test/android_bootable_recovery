#ifndef GUI2_COMPONENTS_SETTING_CARD_H
#define GUI2_COMPONENTS_SETTING_CARD_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

lv_obj_t* create_setting_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                              const char* title, const char* detail, lv_event_cb_t event_callback,
                              const void* user_data, lv_event_cb_t press_guard_callback);

}  // namespace gui2_components

#endif
