#include "pages/timezone_page.h"

#include <algorithm>

#include "components/choice_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

lv_obj_t* create_section_label(lv_obj_t* parent, const gui2_core::ui_metrics& metrics,
                               const char* text) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_width(label, metrics.content_width);
  lv_obj_set_style_text_color(label, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.status_font, LV_PART_MAIN);
  return label;
}

void configure_row(lv_obj_t* row, const gui2_core::ui_metrics& metrics, int height, int padding,
                   bool wrap) {
  lv_obj_set_size(row, metrics.content_width, height);
  gui2_core::set_surface_style(row, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(row, padding, LV_PART_MAIN);
  lv_obj_set_style_pad_column(row, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, wrap ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (wrap) lv_obj_set_style_pad_row(row, metrics.card_gap, LV_PART_MAIN);
  gui2_core::disable_scrolling(row);
  lv_obj_add_flag(row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
}

}  // namespace

timezone_page_view build_timezone_page(const timezone_page_options& options) {
  timezone_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.timezone_indices == nullptr || options.offset_indices == nullptr ||
      options.format_indices == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  const int choice_height = std::clamp(metrics.card_height * 2 / 3,
                                       gui2_core::ui_px(96), gui2_core::ui_px(148));
  const int padding = gui2_core::ui_px(10);
  const int row_width = std::max(1, metrics.content_width - padding * 2);

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

  create_section_label(view.body, metrics, strings.time_format);
  lv_obj_t* format_row = lv_obj_create(view.body);
  configure_row(format_row, metrics, choice_height + padding * 2, padding, false);
  const int first_width = (row_width - metrics.card_gap) / 2;
  view.format_cards[0] = gui2_components::create_choice_card(
      format_row, metrics, strings.twelve_hour, first_width, choice_height,
      options.format_event_callback, &options.format_indices[0], options.press_guard_callback);
  view.format_cards[1] = gui2_components::create_choice_card(
      format_row, metrics, strings.twenty_four_hour,
      row_width - first_width - metrics.card_gap, choice_height, options.format_event_callback,
      &options.format_indices[1], options.press_guard_callback);

  create_section_label(view.body, metrics, strings.select_timezone);
  for (int i = 0; i < 24; ++i) {
    view.timezone_cards[i] = gui2_components::create_choice_card(
        view.body, metrics, strings.timezone_names[i], metrics.content_width, choice_height,
        options.timezone_event_callback, &options.timezone_indices[i],
        options.press_guard_callback);
  }

  create_section_label(view.body, metrics, strings.timezone_offset);
  lv_obj_t* offset_row = lv_obj_create(view.body);
  configure_row(offset_row, metrics, choice_height * 2 + metrics.card_gap + padding * 2, padding,
                true);
  const int offset_width = (row_width - metrics.card_gap) / 2;
  const char* offset_labels[4] = { strings.offset_none, strings.offset_15, strings.offset_30,
                                   strings.offset_45 };
  for (int i = 0; i < 4; ++i) {
    view.offset_cards[i] = gui2_components::create_choice_card(
        offset_row, metrics, offset_labels[i], offset_width, choice_height,
        options.offset_event_callback, &options.offset_indices[i], options.press_guard_callback);
  }
  view.dst_card = gui2_components::create_choice_card(
      view.body, metrics, strings.use_dst, metrics.content_width, choice_height,
      options.dst_event_callback, nullptr, options.press_guard_callback);
  view.current_timezone_label = create_section_label(view.body, metrics, "");
  if (options.current_timezone_text != nullptr)
    lv_label_set_text(view.current_timezone_label, options.current_timezone_text);
  return view;
}

}  // namespace gui2_pages
