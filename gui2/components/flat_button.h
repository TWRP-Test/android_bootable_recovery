#ifndef GUI2_COMPONENTS_FLAT_BUTTON_H
#define GUI2_COMPONENTS_FLAT_BUTTON_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

// A card-coloured button with its label in the accent colour; the legacy
// half-width buttons that sit under a list.
lv_obj_t* create_flat_button(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, int width,
                             const char* text, lv_event_cb_t callback,
                             lv_event_cb_t press_guard_callback);

// Two of them side by side across the content width.
lv_obj_t* create_flat_button_row(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                                 const char* left, lv_event_cb_t left_callback,
                                 const char* right, lv_event_cb_t right_callback,
                                 lv_event_cb_t press_guard_callback);

}  // namespace gui2_components

#endif
