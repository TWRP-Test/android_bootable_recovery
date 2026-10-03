#include "components/apply_button.h"

#include "core/ui_helpers.h"

lv_obj_t* gui2_components::create_apply_button(lv_obj_t* page_layer,
                                               const gui2_core::ui_metrics& metrics,
                                               lv_event_cb_t callback, const char* text,
                                               lv_event_cb_t press_guard_callback) {
  const int height = gui2_core::single_line_card_height();
  const int padding = metrics.cards_top_gap;
  const int page_height = metrics.height - metrics.status_height - metrics.nav_height;
  lv_obj_t* button = lv_obj_create(page_layer);
  lv_obj_set_size(button, metrics.content_width, height);
  lv_obj_set_pos(button, metrics.content_left, page_height - height - padding);
  lv_obj_set_clickable(button, true);
  lv_obj_set_style_radius(button, height / 3, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, metrics.accent, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, gui2_core::tinted(metrics.accent, 18), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  gui2_core::set_card_shadow(button);
  gui2_core::disable_scrolling(button);
  if (press_guard_callback != nullptr)
    lv_obj_add_event_cb(button, press_guard_callback, LV_EVENT_ALL, nullptr);
  if (callback != nullptr) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_obj_set_style_text_color(label, metrics.on_accent, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.text_font, LV_PART_MAIN);
  lv_obj_center(label);
  return button;
}
