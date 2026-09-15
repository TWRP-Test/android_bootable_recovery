#ifndef GUI2_SHELL_STATUS_BAR_H
#define GUI2_SHELL_STATUS_BAR_H

#include "backend/status_backend.h"
#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_shell {

struct status_bar_view {
  lv_obj_t* root = nullptr;
  lv_obj_t* time_label = nullptr;
  lv_obj_t* battery_value = nullptr;
  lv_obj_t* battery_icon = nullptr;
  lv_obj_t* recording_indicator = nullptr;
};

status_bar_view create_status_bar(lv_obj_t* screen, const gui2_core::ui_metrics& metrics,
                                  const char* recording_text, lv_event_cb_t gesture_callback);
void layout_status_bar(const status_bar_view& view, const gui2_core::ui_metrics& metrics);
void update_status_bar(const status_bar_view& view, const gui2_core::ui_metrics& metrics,
                       const gui2_backend::status_snapshot& snapshot);

}  // namespace gui2_shell

#endif
