#include "components/slider_card.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_components {

namespace {

int card_inner_padding(const gui2_core::ui_metrics& metrics) {
  return std::clamp(metrics.outer_margin, gui2_core::ui_px(32), gui2_core::ui_px(56));
}

}  // namespace

lv_obj_t* create_slider_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                             const char* label, int minimum, int maximum, int value, slider* visual,
                             lv_obj_t** value_label, lv_event_cb_t value_changed_callback,
                             lv_event_cb_t pressed_callback, void* user_data) {
  if (parent == nullptr || visual == nullptr || value_label == nullptr) return nullptr;
  const int side_padding = card_inner_padding(metrics);
  const int header_height = std::max(1, metrics.text_font->line_height);
  const int content_gap = std::clamp(metrics.card_gap, gui2_core::ui_px(12), gui2_core::ui_px(18));
  const int slider_height = std::clamp(std::min(metrics.width, metrics.height) / 19,
                                       gui2_core::ui_px(28), gui2_core::ui_px(56));
  const int content_height = header_height + content_gap + slider_height;
  const int card_height = std::max(metrics.card_height, side_padding * 2 + content_height);
  const int card_width = metrics.content_width;
  const int content_width = std::max(1, card_width - side_padding * 2);
  const int inner_height = std::max(1, card_height - side_padding * 2);
  const int content_top = side_padding + std::max(0, (inner_height - content_height) / 2);
  const int slider_top = content_top + header_height + content_gap;

  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, card_width, card_height);
  gui2_core::set_surface_style(card, metrics.card_color);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(card, card_height / 4, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, gui2_core::ui_px(3), LV_PART_MAIN);
  lv_obj_add_flag(card, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  gui2_core::disable_scrolling(card);

  if (label != nullptr && label[0] != '\0') {
    lv_obj_t* title_label = lv_label_create(card);
    lv_label_set_text(title_label, label);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(title_label, content_width * 65 / 100);
    lv_obj_set_pos(title_label, side_padding, content_top);
    lv_obj_set_style_text_color(title_label, metrics.primary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, metrics.text_font, LV_PART_MAIN);
  }

  const int value_width = content_width * 35 / 100;
  *value_label = lv_label_create(card);
  lv_obj_set_width(*value_label, value_width);
  lv_obj_set_style_text_align(*value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  lv_obj_set_pos(*value_label, card_width - side_padding - value_width, content_top);
  lv_obj_set_style_text_color(*value_label, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(*value_label, metrics.text_font, LV_PART_MAIN);

  const lv_color_t background = lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 38);
  lv_obj_t* slider_object =
      create_slider(card, side_padding, slider_top, content_width, slider_height, minimum, maximum,
                    value, background, lv_color_hex(0x347FF1), lv_color_hex(0xFFFFFF), visual);
  if (slider_object == nullptr) return card;
  if (value_changed_callback != nullptr) {
    lv_obj_add_event_cb(slider_object, value_changed_callback, LV_EVENT_VALUE_CHANGED, user_data);
    lv_obj_add_event_cb(slider_object, value_changed_callback, LV_EVENT_RELEASED, user_data);
    lv_obj_add_event_cb(slider_object, value_changed_callback, LV_EVENT_PRESS_LOST, user_data);
  }
  if (pressed_callback != nullptr) {
    lv_obj_add_event_cb(slider_object, pressed_callback, LV_EVENT_PRESSED, user_data);
    lv_obj_add_event_cb(slider_object, pressed_callback, LV_EVENT_PRESSING, user_data);
  }
  return card;
}

}  // namespace gui2_components
