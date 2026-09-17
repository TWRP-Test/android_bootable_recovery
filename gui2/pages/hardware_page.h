#ifndef GUI2_PAGES_HARDWARE_PAGE_H
#define GUI2_PAGES_HARDWARE_PAGE_H

#include <cstddef>

#include "components/slider.h"
#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_pages {

struct hardware_slider_spec {
  const char* label = nullptr;
  int minimum = 0;
  int maximum = 0;
  int value = 0;
  gui2_components::slider* visual = nullptr;
  lv_obj_t** value_label = nullptr;
  void* user_data = nullptr;
};

struct hardware_toggle_spec {
  const char* label = nullptr;
  bool enabled = false;
  void* user_data = nullptr;
  size_t slider_index = 0;
};

struct hardware_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const char* error_text = nullptr;
  const hardware_slider_spec* sliders = nullptr;
  size_t slider_count = 0;
  lv_event_cb_t value_changed_callback = nullptr;
  lv_event_cb_t pressed_callback = nullptr;
  const hardware_toggle_spec* toggle = nullptr;
  lv_event_cb_t toggle_event_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

struct hardware_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* error_label = nullptr;
  lv_obj_t* toggle_switch = nullptr;
  lv_obj_t* slider_cards[4] = {};
};

hardware_page_view build_hardware_page(const hardware_page_options& options);

}  // namespace gui2_pages

#endif
