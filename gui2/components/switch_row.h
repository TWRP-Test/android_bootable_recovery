#ifndef GUI2_COMPONENTS_SWITCH_ROW_H
#define GUI2_COMPONENTS_SWITCH_ROW_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

// Card row with a leading label and a trailing switch.
lv_obj_t* create_switch_row(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                            const char* label, bool checked, lv_event_cb_t event_callback,
                            void* user_data);

}  // namespace gui2_components

#endif
