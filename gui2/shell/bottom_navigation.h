#ifndef GUI2_SHELL_BOTTOM_NAVIGATION_H
#define GUI2_SHELL_BOTTOM_NAVIGATION_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_shell {

enum class navigation_action {
  BACK,
  HOME,
  LOG,
  POWER,
};

struct bottom_navigation_view {
  lv_obj_t* root = nullptr;
  lv_obj_t* home_icon = nullptr;
  lv_obj_t* console_icon = nullptr;
  lv_obj_t* power_icon = nullptr;
};

bottom_navigation_view create_bottom_navigation(lv_obj_t* screen,
                                                const gui2_core::ui_metrics& metrics,
                                                bool home_active, lv_event_cb_t event_callback,
                                                lv_event_cb_t press_guard_callback);
void refresh_bottom_navigation(const bottom_navigation_view& view, bool home_active);

}  // namespace gui2_shell

#endif
