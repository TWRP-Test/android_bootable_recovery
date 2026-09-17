#include "pages/hardware_page.h"

#include <algorithm>
#include <iterator>

#include "components/slider_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

lv_obj_t* create_section_label(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                               const char* text) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_obj_set_width(label, metrics.content_width);
  lv_obj_set_style_text_color(label, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.status_font, LV_PART_MAIN);
  return label;
}

}  // namespace

hardware_page_view build_hardware_page(const hardware_page_options& options) {
  hardware_page_view view;
  if (options.content == nullptr || options.metrics == nullptr ||
      (options.slider_count != 0 && options.sliders == nullptr))
    return view;

  const auto& metrics = *options.metrics;
  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.outer_margin, 0);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_left(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_right(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_top(view.body, gui2_core::ui_px(8), LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(view.body, gui2_core::navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_add_flag(view.body, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  gui2_core::disable_scrolling(view.body);

  view.error_label = create_section_label(view.body, metrics, options.error_text);
  lv_obj_set_style_text_color(view.error_label, lv_color_hex(0xF0443E), LV_PART_MAIN);
  lv_obj_add_flag(view.error_label, LV_OBJ_FLAG_HIDDEN);

  for (size_t i = 0; i < options.slider_count; ++i) {
    const auto& spec = options.sliders[i];
    gui2_components::slider_card_switch toggle;
    if (options.toggle != nullptr && options.toggle->slider_index == i) {
      toggle.present = true;
      toggle.label = options.toggle->label;
      toggle.checked = options.toggle->enabled;
      toggle.event_callback = options.toggle_event_callback;
      toggle.user_data = options.toggle->user_data;
    }
    lv_obj_t* card = gui2_components::create_slider_card(
        view.body, metrics, spec.label, spec.minimum, spec.maximum, spec.value, spec.visual,
        spec.value_label, options.value_changed_callback, options.pressed_callback,
        spec.user_data, toggle.present ? &toggle : nullptr,
        toggle.present ? &view.toggle_switch : nullptr);
    if (i < std::size(view.slider_cards)) view.slider_cards[i] = card;
  }
  return view;
}

}  // namespace gui2_pages
