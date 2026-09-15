#include "components/choice_card.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_components {

namespace {

int card_inner_padding(const gui2_core::ui_metrics& metrics) {
  return std::clamp(metrics.outer_margin, gui2_core::ui_px(32), gui2_core::ui_px(56));
}

}  // namespace

lv_obj_t* create_choice_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                             const char* label, int width, int height, lv_event_cb_t event_callback,
                             const void* user_data, lv_event_cb_t press_guard_callback) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, width, height);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(card, height / 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, gui2_core::ui_px(3), LV_PART_MAIN);
  gui2_core::disable_scrolling(card);
  if (press_guard_callback != nullptr)
    lv_obj_add_event_cb(card, press_guard_callback, LV_EVENT_ALL, nullptr);
  if (event_callback != nullptr)
    lv_obj_add_event_cb(card, event_callback, LV_EVENT_CLICKED, const_cast<void*>(user_data));

  lv_obj_t* text = lv_label_create(card);
  lv_label_set_text(text, label);
  lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(text, std::max(1, width - card_inner_padding(metrics) * 2));
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(text, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(text, metrics.text_font, LV_PART_MAIN);
  lv_obj_update_layout(text);
  lv_obj_center(text);
  return card;
}

}  // namespace gui2_components
