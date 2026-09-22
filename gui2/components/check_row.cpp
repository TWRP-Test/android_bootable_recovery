#include "components/check_row.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_components {

namespace {

constexpr uint32_t kAccent = 0x347FF1;

int control_size(const gui2_core::ui_metrics& metrics) {
  const int header_height = std::max(1, metrics.text_font->line_height);
  return std::clamp(header_height * 3 / 2, gui2_core::ui_px(64), gui2_core::ui_px(88));
}

// The card and its label; the control goes at the right edge, `control_width`
// wide, and the row reports its height so the caller can centre it.
lv_obj_t* create_row_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                          const char* label, int control_width, int* row_height_out) {
  const int side_padding = gui2_core::card_inner_padding();
  const int header_height = std::max(1, metrics.text_font->line_height);
  const int row_height = std::max(gui2_core::single_line_card_height() * 7 / 6,
                                  control_size(metrics) + side_padding);
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
                  std::max(1, card_width - side_padding * 2 - control_width - gui2_core::ui_px(16)),
                  header_height);
  lv_obj_set_pos(text, side_padding, (row_height - header_height) / 2);
  lv_obj_set_style_text_color(text, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(text, metrics.text_font, LV_PART_MAIN);

  *row_height_out = row_height;
  return card;
}

}  // namespace

lv_obj_t* create_check_row(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                           const char* label, bool checked, lv_event_cb_t event_callback,
                           void* user_data) {
  if (parent == nullptr) return nullptr;

  const int side_padding = gui2_core::card_inner_padding();
  const int box_size = control_size(metrics);
  const int card_width = metrics.content_width;
  const int mark_inset = gui2_core::ui_px(12);
  const lv_font_t* mark_font =
      box_size - mark_inset >= 48 ? &lv_font_montserrat_48 : &lv_font_montserrat_24;

  int row_height = 0;
  lv_obj_t* card = create_row_card(parent, metrics, label, box_size, &row_height);

  lv_obj_t* toggle = lv_checkbox_create(card);
  lv_checkbox_set_text_static(toggle, "");
  lv_obj_set_size(toggle, box_size, box_size);
  lv_obj_set_pos(toggle, card_width - side_padding - box_size, (row_height - box_size) / 2);
  lv_obj_set_style_pad_all(toggle, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(toggle, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(toggle, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(toggle, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(toggle);

  lv_obj_set_style_width(toggle, box_size, LV_PART_INDICATOR);
  lv_obj_set_style_height(toggle, box_size, LV_PART_INDICATOR);
  lv_obj_set_style_radius(toggle, box_size / 4, LV_PART_INDICATOR);
  lv_obj_set_style_border_width(toggle, gui2_core::ui_px(3), LV_PART_INDICATOR);
  lv_obj_set_style_border_color(toggle, metrics.secondary_text, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(toggle, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_text_font(toggle, mark_font, LV_PART_INDICATOR);
  lv_obj_set_style_text_color(toggle, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(toggle, lv_color_hex(kAccent),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_border_color(toggle, lv_color_hex(kAccent),
                                LV_PART_INDICATOR | LV_STATE_CHECKED);

  if (checked) lv_obj_add_state(toggle, LV_STATE_CHECKED);
  if (event_callback != nullptr)
    lv_obj_add_event_cb(toggle, event_callback, LV_EVENT_VALUE_CHANGED, user_data);
  return toggle;
}

lv_obj_t* create_switch_row(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                            const char* label, bool checked, lv_event_cb_t event_callback,
                            void* user_data) {
  if (parent == nullptr) return nullptr;

  const int side_padding = gui2_core::card_inner_padding();
  const int track_height = control_size(metrics) * 3 / 4;
  const int track_width = track_height * 7 / 4;
  int row_height = 0;
  lv_obj_t* card = create_row_card(parent, metrics, label, track_width, &row_height);

  lv_obj_t* toggle = lv_switch_create(card);
  lv_obj_set_size(toggle, track_width, track_height);
  lv_obj_set_pos(toggle, metrics.content_width - side_padding - track_width,
                 (row_height - track_height) / 2);
  lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(toggle, lv_color_mix(metrics.secondary_text, metrics.card_color, 110),
                            LV_PART_MAIN);
  lv_obj_set_style_border_width(toggle, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(toggle, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(toggle, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(toggle, lv_color_hex(kAccent), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_radius(toggle, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(toggle, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_bg_color(toggle, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  // Negative padding shrinks the knob inside the track.
  lv_obj_set_style_pad_all(toggle, -gui2_core::ui_px(6), LV_PART_KNOB);
  lv_obj_set_style_shadow_width(toggle, 0, LV_PART_KNOB);

  if (checked) lv_obj_add_state(toggle, LV_STATE_CHECKED);
  if (event_callback != nullptr)
    lv_obj_add_event_cb(toggle, event_callback, LV_EVENT_VALUE_CHANGED, user_data);
  return toggle;
}

}  // namespace gui2_components
