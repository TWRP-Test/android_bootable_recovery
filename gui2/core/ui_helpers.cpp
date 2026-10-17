#include "core/ui_helpers.h"

#include "core/ui_metrics.h"

#include <algorithm>

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

void set_card_shadow(lv_obj_t* card) {
  if (card == nullptr || !ui.dark) return;
  lv_obj_set_style_shadow_width(card, ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, ui_px(3), LV_PART_MAIN);
}

void set_selected_fill(lv_obj_t* card, bool selected, lv_color_t idle) {
  if (card == nullptr) return;
  const lv_color_t color = selected ? ui.accent : idle;
  lv_obj_set_style_bg_color(card, color, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, tinted(color, 18), LV_STATE_PRESSED);
  for (uint32_t i = 0; i < lv_obj_get_child_count(card); ++i) {
    lv_obj_t* child = lv_obj_get_child(card, i);
    if (lv_obj_check_type(child, &lv_label_class))
      lv_obj_set_style_text_color(child, selected ? ui.on_accent : ui.primary_text, LV_PART_MAIN);
  }
}

void set_card_grid(lv_obj_t* body) {
  if (body == nullptr || grid_columns() < 2) return;
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(body, ui.card_gap, LV_PART_MAIN);
  lv_obj_add_flag(body, LV_OBJ_FLAG_USER_1);
}

int card_width_in(lv_obj_t* parent) {
  const bool grid = parent != nullptr && lv_obj_get_style_flex_flow(parent, LV_PART_MAIN) ==
                                             LV_FLEX_FLOW_ROW_WRAP;
  return grid ? grid_column_width() : ui.content_width;
}

void equalize_card_grids(lv_obj_t* root) {
  if (root == nullptr) return;
  const uint32_t count = lv_obj_get_child_count(root);
  if (lv_obj_has_flag(root, LV_OBJ_FLAG_USER_1)) {
    lv_obj_update_layout(root);
    for (uint32_t first = 0; first < count;) {
      const int32_t y = lv_obj_get_y(lv_obj_get_child(root, first));
      uint32_t end = first;
      int32_t tallest = 0;
      for (; end < count; ++end) {
        lv_obj_t* child = lv_obj_get_child(root, end);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_get_y(child) != y) break;
        tallest = std::max(tallest, lv_obj_get_height(child));
      }
      for (uint32_t i = first; end - first > 1 && i < end; ++i) {
        lv_obj_t* child = lv_obj_get_child(root, i);
        if (!lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) lv_obj_set_height(child, tallest);
      }
      first = std::max(end, first + 1);
    }
  }
  for (uint32_t i = 0; i < count; ++i) equalize_card_grids(lv_obj_get_child(root, i));
}

}  // namespace gui2_core
