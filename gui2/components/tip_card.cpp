#include "components/tip_card.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_components {

lv_obj_t* create_tip_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, const char* text,
                          lv_color_t text_color, lv_color_t surface_color) {
  if (parent == nullptr) return nullptr;

  const int padding = gui2_core::card_inner_padding();
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_width(card, metrics.content_width);
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(card, surface_color);
  lv_obj_set_style_radius(card, gui2_core::single_line_card_height() / 4, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, padding, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(card);

  lv_obj_t* label = lv_label_create(card);
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, std::max(1, metrics.content_width - padding * 2));
  lv_obj_set_style_text_color(label, text_color, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.status_font, LV_PART_MAIN);
  return card;
}

}  // namespace gui2_components
