#ifndef GUI2_SHELL_QUICK_PANEL_H
#define GUI2_SHELL_QUICK_PANEL_H

#include <cstddef>

#include "components/slider.h"
#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_shell {

struct quick_action_item {
  const lv_image_dsc_t* icon = nullptr;
  const char* text = nullptr;
  const void* user_data = nullptr;
};

struct quick_panel_options {
  const gui2_core::ui_metrics* metrics = nullptr;
  const char* title = nullptr;
  bool brightness_available = false;
  const char* brightness_label = nullptr;
  int brightness_value = 0;
  gui2_components::slider* brightness_visual = nullptr;
  lv_obj_t** brightness_value_label = nullptr;
  void* brightness_user_data = nullptr;
  const quick_action_item* actions = nullptr;
  size_t action_count = 0;
  const void* recording_action_user_data = nullptr;
  lv_event_cb_t dismiss_event_callback = nullptr;
  lv_event_cb_t panel_gesture_callback = nullptr;
  lv_event_cb_t brightness_event_callback = nullptr;
  lv_event_cb_t brightness_state_callback = nullptr;
  lv_event_cb_t action_event_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

struct quick_panel_view {
  lv_obj_t* dismiss = nullptr;
  lv_obj_t* menu = nullptr;
  lv_obj_t* recording_button = nullptr;
  lv_obj_t* recording_label = nullptr;
  lv_obj_t* feedback = nullptr;
  lv_obj_t* screenshot_flash = nullptr;
  int menu_height = 0;
  int menu_open_y = 0;
  int menu_closed_y = 0;
};

quick_panel_view create_quick_panel(const quick_panel_options& options);

}  // namespace gui2_shell

#endif
