#ifndef GUI2_COMPONENTS_SLIDER_CARD_H
#define GUI2_COMPONENTS_SLIDER_CARD_H

#include "components/slider.h"
#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

struct slider_card_switch {
  bool present = false;
  const char* label = nullptr;
  bool checked = false;
  lv_event_cb_t event_callback = nullptr;
  void* user_data = nullptr;
};

lv_obj_t* create_slider_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                             const char* label, int minimum, int maximum, int value, slider* visual,
                             lv_obj_t** value_label, lv_event_cb_t value_changed_callback,
                             lv_event_cb_t pressed_callback, void* user_data,
                             const slider_card_switch* toggle = nullptr,
                             lv_obj_t** toggle_object = nullptr);

}  // namespace gui2_components

#endif
