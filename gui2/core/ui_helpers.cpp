#include "core/ui_helpers.h"

#include "core/ui_metrics.h"

namespace gui2_core {

void set_surface_style(lv_obj_t* object, lv_color_t color, lv_opa_t opa) {
  if (object == nullptr) return;
  lv_obj_set_style_bg_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, opa, LV_PART_MAIN);
  lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
}

void style_scrollbar(lv_obj_t* object, lv_color_t color) {
  if (object == nullptr) return;
  lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(object, color, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(object, 110, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(object, ui_px(8), LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(object, ui_px(4), LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(object, ui_px(4), LV_PART_SCROLLBAR);
  lv_obj_set_style_border_width(object, 0, LV_PART_SCROLLBAR);
}

void disable_scrolling(lv_obj_t* object) {
  if (object == nullptr) return;
  lv_obj_set_scrollable(object, false);
  lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

}  // namespace gui2_core
