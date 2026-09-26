#include "pages/fastbootd_page.h"

#include <algorithm>

#include "components/section_label.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

fastbootd_page_view build_fastbootd_page(const fastbootd_page_options& options) {
  fastbootd_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  lv_obj_t* body = lv_obj_create(options.content);
  lv_obj_set_pos(body, metrics.outer_margin, 0);
  lv_obj_set_size(body, metrics.content_width, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  gui2_core::disable_scrolling(body);

  gui2_components::create_section_label(body, metrics, strings.usb_mode);
  if (options.usb_tabs != nullptr) {
    const char* labels[2] = { "Fastbootd", "ADB" };
    options.usb_tabs->create(body, metrics, labels, 2, options.usb_fastboot ? 0 : 1,
                             options.usb_callback, nullptr);
  }
  gui2_components::create_section_label(body, metrics, strings.console_tab_output);

  // The console takes whatever height is left above the navigation.
  lv_obj_update_layout(body);
  const int scroll_top = metrics.heading_top + metrics.heading_height + metrics.cards_top_gap;
  const int available = metrics.height - metrics.status_height - scroll_top -
                        gui2_core::navigation_safe_area() - lv_obj_get_height(body) -
                        metrics.card_gap;

  console_page_options console;
  console.content = body;
  console.metrics = &metrics;
  console.empty_text = "";
  console.font = options.console_font;
  console.self_scrolling = true;
  view.console = build_console_page(console);
  if (view.console.body != nullptr) {
    const int height = std::max(gui2_core::ui_px(300), available);
    lv_obj_set_height(view.console.body, height);
    view.console.minimum_height = height;
  }
  return view;
}

}  // namespace gui2_pages
