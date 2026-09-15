#include "components/setting_card.h"

#include <algorithm>

#include "components/icon.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"

namespace gui2_components {

namespace {

int card_inner_padding(const gui2_core::ui_metrics& metrics) {
  return std::clamp(metrics.outer_margin, gui2_core::ui_px(32), gui2_core::ui_px(56));
}

}  // namespace

lv_obj_t* create_setting_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                              const char* title, const char* detail, lv_event_cb_t event_callback,
                              const void* user_data, lv_event_cb_t press_guard_callback) {
  int card_height = std::max(metrics.card_height, gui2_core::ui_px(132));
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, metrics.content_width, card_height);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(card, card_height / 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, metrics.card_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 18),
                            LV_STATE_PRESSED);
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

  const int left = card_inner_padding(metrics);
  const int gap = gui2_core::ui_px(8);
  const int right = left + gui2_core::ui_px(56);
  const int text_width = std::max(1, metrics.content_width - left - right);

  lv_obj_t* text_block = lv_obj_create(card);
  lv_obj_set_width(text_block, text_width);
  lv_obj_set_height(text_block, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(text_block, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(text_block, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(text_block, gap, LV_PART_MAIN);
  lv_obj_set_layout(text_block, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(text_block, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(text_block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(text_block, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(text_block);

  lv_obj_t* title_label = lv_label_create(text_block);
  lv_label_set_text(title_label, title);
  lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(title_label, text_width);
  lv_obj_set_style_text_color(title_label, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title_label, metrics.text_font, LV_PART_MAIN);

  lv_obj_t* detail_label = lv_label_create(text_block);
  lv_label_set_text(detail_label, detail);
  lv_label_set_long_mode(detail_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(detail_label, text_width);
  lv_obj_set_style_text_color(detail_label, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(detail_label, metrics.status_font, LV_PART_MAIN);

  lv_obj_update_layout(text_block);
  card_height = std::max(card_height, lv_obj_get_height(text_block) + gui2_core::ui_px(32));
  lv_obj_set_height(card, card_height);
  lv_obj_set_style_radius(card, card_height / 4, LV_PART_MAIN);
  lv_obj_align(text_block, LV_ALIGN_LEFT_MID, left, 0);

  lv_obj_t* arrow =
      create_svg_image(card, &kGui2IconArrowRight, gui2_core::ui_px(32), gui2_core::ui_px(32));
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -card_inner_padding(metrics), 0);
  return card;
}

}  // namespace gui2_components
