#include "core/ui_helpers.h"

namespace gui2_core {

void set_surface_style(lv_obj_t* object, lv_color_t color, lv_opa_t opa) {
  if (object == nullptr) return;
  lv_obj_set_style_bg_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, opa, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
}

void disable_scrolling(lv_obj_t* object) {
  if (object == nullptr) return;
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

}  // namespace gui2_core
