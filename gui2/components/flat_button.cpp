#include "components/flat_button.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_components {

namespace {

constexpr uint32_t kAccent = 0x347FF1;

}  // namespace

lv_obj_t* create_flat_button(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, int width,
                             const char* text, lv_event_cb_t callback,
                             lv_event_cb_t press_guard_callback) {
  const int height = gui2_core::single_line_card_height();
  lv_obj_t* button = lv_obj_create(parent);
  lv_obj_set_size(button, width, height);
  lv_obj_set_clickable(button, true);
  gui2_core::set_surface_style(button, metrics.card_color);
  lv_obj_set_style_radius(button, height / 3, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 18),
                            LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(button, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(button, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(button, gui2_core::ui_px(3), LV_PART_MAIN);
  gui2_core::disable_scrolling(button);
  if (press_guard_callback != nullptr)
    lv_obj_add_event_cb(button, press_guard_callback, LV_EVENT_ALL, nullptr);
  if (callback != nullptr) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(label, lv_color_hex(kAccent), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.text_font, LV_PART_MAIN);
  lv_obj_center(label);
  return button;
}

lv_obj_t* create_flat_button_row(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                                 const char* left, lv_event_cb_t left_callback,
                                 const char* right, lv_event_cb_t right_callback,
                                 lv_event_cb_t press_guard_callback) {
  const int gap = metrics.card_gap;
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, metrics.content_width, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(row, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(row, gap, LV_PART_MAIN);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  gui2_core::disable_scrolling(row);
  const int width = std::max(1, (metrics.content_width - gap) / 2);
  create_flat_button(row, metrics, width, left, left_callback, press_guard_callback);
  create_flat_button(row, metrics, width, right, right_callback, press_guard_callback);
  return row;
}

}  // namespace gui2_components
