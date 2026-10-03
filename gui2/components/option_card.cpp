#include "components/option_card.h"

#include <algorithm>

#include "components/icon.h"
#include "core/ui_helpers.h"

namespace gui2_components {

option_card_view create_option_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                                    const char* label, bool selected,
                                    lv_event_cb_t event_callback, const void* user_data,
                                    lv_event_cb_t press_guard_callback) {
  option_card_view view;
  if (parent == nullptr) return view;
  const int padding = gui2_core::card_inner_padding();
  const int height = std::max(metrics.card_height, gui2_core::ui_px(118));
  lv_obj_t* option = lv_obj_create(parent);
  lv_obj_set_size(option, gui2_core::card_width_in(parent), height);
  lv_obj_set_clickable(option, true);
  lv_obj_set_style_radius(option, height / 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(option, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(option, 0, LV_PART_MAIN);
  gui2_core::set_card_shadow(option);
  gui2_core::disable_scrolling(option);
  if (press_guard_callback != nullptr)
    lv_obj_add_event_cb(option, press_guard_callback, LV_EVENT_ALL, nullptr);
  if (event_callback != nullptr)
    lv_obj_add_event_cb(option, event_callback, LV_EVENT_CLICKED, const_cast<void*>(user_data));
  lv_obj_set_style_bg_opa(option, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_t* text = lv_label_create(option);
  lv_label_set_text(text, label == nullptr ? "" : label);
  lv_obj_align(text, LV_ALIGN_LEFT_MID, padding, 0);
  lv_obj_set_style_text_color(text, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(text, metrics.text_font, LV_PART_MAIN);

  view.check = lv_label_create(option);
  lv_obj_align(view.check, LV_ALIGN_RIGHT_MID, -padding, 0);
  lv_obj_set_style_text_color(view.check, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.check, &lv_font_montserrat_48, LV_PART_MAIN);
  scale_icon_font(view.check, metrics.scale);
  lv_label_set_text(view.check, selected ? LV_SYMBOL_OK : "");
  gui2_core::set_selected_fill(option, selected, metrics.card_color);
  view.card = option;
  return view;
}

}  // namespace gui2_components
