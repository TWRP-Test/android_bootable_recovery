#include "pages/language_page.h"

#include <algorithm>

#include "components/option_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

language_page_view build_language_page(const language_page_options& options) {
  language_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.languages == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.content_left, 0);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.body);
  lv_obj_set_style_pad_row(view.body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  const size_t count = std::min(options.language_count, size_t(3));
  for (size_t i = 0; i < count; ++i) {
    const auto language = options.languages[i];
    const auto option = gui2_components::create_option_card(
        view.body, metrics, gui2_i18n::get_language_pack(language).native_name,
        options.pending_language == language, options.option_event_callback,
        &options.languages[i], options.press_guard_callback);
    view.option_cards[i] = option.card;
    view.check_labels[i] = option.check;
  }
  return view;
}

}  // namespace gui2_pages
