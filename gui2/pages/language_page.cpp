#include "pages/language_page.h"

#include <algorithm>

#include "components/icon.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

int card_inner_padding(const gui2_core::ui_metrics& metrics) {
  return std::clamp(metrics.outer_margin, gui2_core::ui_px(32), gui2_core::ui_px(56));
}

}  // namespace

language_page_view build_language_page(const language_page_options& options) {
  language_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.languages == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.outer_margin, 0);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.body);
  lv_obj_set_style_pad_bottom(view.body, gui2_core::navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  const size_t count = std::min(options.language_count, size_t(3));
  for (size_t i = 0; i < count; ++i) {
    const auto language = options.languages[i];
    const int height = std::max(metrics.card_height, gui2_core::ui_px(118));
    lv_obj_t* option = lv_obj_create(view.body);
    lv_obj_set_size(option, metrics.content_width, height);
    lv_obj_add_flag(option, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(option, height / 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(option, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(option, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(option, gui2_core::ui_px(10), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(option, 45, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(option, gui2_core::ui_px(3), LV_PART_MAIN);
    gui2_core::disable_scrolling(option);
    if (options.press_guard_callback != nullptr)
      lv_obj_add_event_cb(option, options.press_guard_callback, LV_EVENT_ALL, nullptr);
    if (options.option_event_callback != nullptr)
      lv_obj_add_event_cb(option, options.option_event_callback, LV_EVENT_CLICKED,
                          const_cast<gui2_i18n::language_id*>(&options.languages[i]));

    const bool selected = options.pending_language == language;
    const lv_color_t color = selected ? lv_color_hex(0x347FF1) : metrics.card_color;
    lv_obj_set_style_bg_color(option, color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(option, lv_color_mix(lv_color_hex(0xFFFFFF), color, 18),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(option, LV_OPA_COVER, LV_PART_MAIN);
    view.option_cards[i] = option;

    lv_obj_t* label = lv_label_create(option);
    lv_label_set_text(label, gui2_i18n::get_language_pack(language).native_name);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, card_inner_padding(metrics), 0);
    lv_obj_set_style_text_color(label, metrics.primary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, metrics.text_font, LV_PART_MAIN);

    view.check_labels[i] = lv_label_create(option);
    lv_obj_align(view.check_labels[i], LV_ALIGN_RIGHT_MID, -card_inner_padding(metrics), 0);
    lv_obj_set_style_text_color(view.check_labels[i], metrics.primary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(view.check_labels[i], &lv_font_montserrat_48, LV_PART_MAIN);
    gui2_components::scale_icon_font(view.check_labels[i], metrics.scale);
    lv_label_set_text(view.check_labels[i], selected ? LV_SYMBOL_OK : "");
  }
  return view;
}

}  // namespace gui2_pages
