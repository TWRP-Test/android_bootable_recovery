#include "components/switch_row.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_components {

lv_obj_t* create_switch_row(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                            const char* label, bool checked, lv_event_cb_t event_callback,
                            void* user_data) {
  if (parent == nullptr) return nullptr;

  const int side_padding = gui2_core::card_inner_padding();
  const int header_height = std::max(1, metrics.text_font->line_height);
  const int switch_height =
      std::clamp(header_height, gui2_core::ui_px(44), gui2_core::ui_px(64));
  const int switch_width = switch_height * 9 / 5;
  const int row_height = std::max(gui2_core::single_line_card_height(),
                                  std::max(header_height, switch_height) + side_padding);
  const int card_width = metrics.content_width;

  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, card_width, row_height);
  gui2_core::set_surface_style(card, metrics.card_color);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(card, row_height / 4, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, gui2_core::ui_px(3), LV_PART_MAIN);
  gui2_core::disable_scrolling(card);

  lv_obj_t* text = lv_label_create(card);
  lv_label_set_text(text, label == nullptr ? "" : label);
  lv_label_set_long_mode(text, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(text,
                  std::max(1, card_width - side_padding * 2 - switch_width - gui2_core::ui_px(16)),
                  header_height);
  lv_obj_set_pos(text, side_padding, (row_height - header_height) / 2);
  lv_obj_set_style_text_color(text, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(text, metrics.text_font, LV_PART_MAIN);

  lv_obj_t* toggle = lv_switch_create(card);
  lv_obj_set_size(toggle, switch_width, switch_height);
  lv_obj_set_pos(toggle, card_width - side_padding - switch_width,
                 (row_height - switch_height) / 2);
  lv_obj_set_style_bg_color(toggle, lv_color_hex(0x347FF1),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (checked) lv_obj_add_state(toggle, LV_STATE_CHECKED);
  if (event_callback != nullptr)
    lv_obj_add_event_cb(toggle, event_callback, LV_EVENT_VALUE_CHANGED, user_data);
  return toggle;
}

}  // namespace gui2_components
