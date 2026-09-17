#include "pages/wipe_progress_page.h"

#include <algorithm>

#include "components/section_label.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

wipe_progress_page_view build_wipe_progress_page(const wipe_progress_page_options& options) {
  wipe_progress_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const int bar_height = std::clamp(gui2_core::ui_px(18), gui2_core::ui_px(12),
                                    gui2_core::ui_px(26));

  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.outer_margin, 0);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(view.body);

  view.state_label = gui2_components::create_section_label(view.body, metrics,
                                                           options.strings->wiping);
  lv_obj_set_style_text_color(view.state_label, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.state_label, metrics.text_font, LV_PART_MAIN);

  view.bar = lv_obj_create(view.body);
  lv_obj_set_size(view.bar, metrics.content_width, bar_height);
  gui2_core::set_surface_style(view.bar,
                               lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 38));
  lv_obj_set_style_radius(view.bar, bar_height / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.bar, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.bar);

  view.bar_fill = lv_obj_create(view.bar);
  lv_obj_set_size(view.bar_fill, 1, bar_height);
  lv_obj_set_pos(view.bar_fill, 0, 0);
  gui2_core::set_surface_style(view.bar_fill, lv_color_hex(0x347FF1));
  lv_obj_set_style_radius(view.bar_fill, bar_height / 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.bar_fill, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.bar_fill);

  console_page_options console_options;
  console_options.content = view.body;
  console_options.metrics = &metrics;
  console_options.empty_text = "";
  console_options.font = options.console_font;
  view.console = build_console_page(console_options);
  if (view.console.body != nullptr) {
    lv_obj_set_pos(view.console.body, 0, 0);
    lv_obj_set_height(view.console.body, view.console.minimum_height * 2 / 3);
    view.console.minimum_height = view.console.minimum_height * 2 / 3;
  }
  return view;
}

void update_wipe_progress(const wipe_progress_page_view& view,
                          const gui2_i18n::language_pack& strings,
                          const gui2_backend::wipe_status& status) {
  if (view.state_label != nullptr) {
    switch (status.state) {
      case gui2_backend::wipe_state::DONE:
        lv_label_set_text(view.state_label, strings.wipe_complete);
        lv_obj_set_style_text_color(view.state_label, lv_color_hex(0x18C935), LV_PART_MAIN);
        break;
      case gui2_backend::wipe_state::FAILED:
        lv_label_set_text(view.state_label, strings.wipe_failed);
        lv_obj_set_style_text_color(view.state_label, lv_color_hex(0xF0443E), LV_PART_MAIN);
        break;
      default:
        lv_label_set_text(view.state_label, strings.wiping);
        break;
    }
  }

  if (view.bar != nullptr && view.bar_fill != nullptr) {
    const int width = lv_obj_get_width(view.bar);
    const int total = std::max(1, status.total);
    const int done = std::clamp(status.done, 0, total);
    const int filled = status.state == gui2_backend::wipe_state::FAILED
                           ? lv_obj_get_width(view.bar_fill)
                           : std::max(1, width * done / total);
    lv_obj_set_width(view.bar_fill, filled);
    if (status.state == gui2_backend::wipe_state::FAILED)
      lv_obj_set_style_bg_color(view.bar_fill, lv_color_hex(0xF0443E), LV_PART_MAIN);
  }
}

}  // namespace gui2_pages
