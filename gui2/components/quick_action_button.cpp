#include "components/quick_action_button.h"

#include <algorithm>

#include "components/icon.h"
#include "core/ui_helpers.h"

namespace gui2_components {

lv_obj_t* create_quick_action_button(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                                     const lv_image_dsc_t* icon_source, const char* text, int x,
                                     int y, int width, const void* user_data,
                                     lv_event_cb_t click_callback,
                                     lv_event_cb_t press_guard_callback,
                                     lv_event_cb_t gesture_callback) {
  const int height = std::clamp(metrics.height / 14, gui2_core::ui_px(124), gui2_core::ui_px(148));
  lv_obj_t* button = lv_obj_create(parent);
  lv_obj_set_size(button, width, height);
  lv_obj_set_pos(button, x, y);
  gui2_core::set_surface_style(button, metrics.background);
  lv_obj_set_style_radius(button, height / 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.background, 18),
                            LV_STATE_PRESSED);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(button);
  if (press_guard_callback != nullptr)
    lv_obj_add_event_cb(button, press_guard_callback, LV_EVENT_ALL, nullptr);
  if (click_callback != nullptr)
    lv_obj_add_event_cb(button, click_callback, LV_EVENT_CLICKED, const_cast<void*>(user_data));
  if (gesture_callback != nullptr) {
    lv_obj_add_event_cb(button, gesture_callback, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(button, gesture_callback, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(button, gesture_callback, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(button, gesture_callback, LV_EVENT_PRESS_LOST, nullptr);
  }

  lv_obj_t* icon =
      create_svg_image(button, icon_source, gui2_core::ui_px(48), gui2_core::ui_px(48));
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, std::max(1, width - gui2_core::ui_px(12)));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.status_font, LV_PART_MAIN);

  lv_obj_update_layout(icon);
  lv_obj_update_layout(label);
  const int group_gap = std::clamp(metrics.card_gap / 2, gui2_core::ui_px(8), gui2_core::ui_px(12));
  const int group_height = lv_obj_get_height(icon) + group_gap + lv_obj_get_height(label);
  const int group_top = std::max(0, (height - group_height) / 2);
  lv_obj_align(icon, LV_ALIGN_TOP_LEFT, (width - lv_obj_get_width(icon)) / 2, group_top);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, gui2_core::ui_px(6),
               group_top + lv_obj_get_height(icon) + group_gap);
  return button;
}

}  // namespace gui2_components
