#ifndef GUI2_COMPONENTS_OPTION_CARD_H
#define GUI2_COMPONENTS_OPTION_CARD_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

// One choice in a pick-one list: label on the left, a check on the right when selected.
struct option_card_view {
  lv_obj_t* card = nullptr;
  lv_obj_t* check = nullptr;
};

option_card_view create_option_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                                    const char* label, bool selected,
                                    lv_event_cb_t event_callback, const void* user_data,
                                    lv_event_cb_t press_guard_callback);

}  // namespace gui2_components

#endif
