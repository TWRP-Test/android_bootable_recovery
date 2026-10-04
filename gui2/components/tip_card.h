#ifndef GUI2_COMPONENTS_TIP_CARD_H
#define GUI2_COMPONENTS_TIP_CARD_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

// The banner the format-data warning introduced: a tinted card holding one
// paragraph. The colour says what kind of notice it is.
lv_obj_t* create_tip_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, const char* text,
                          lv_color_t text_color, lv_color_t surface_color);

}  // namespace gui2_components

#endif
