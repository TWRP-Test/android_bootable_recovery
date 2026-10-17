#include "pages/fastbootd_page.h"

#include <algorithm>

#include "components/section_label.h"
#include "core/ui_helpers.h"
#include "pages/progress_page.h"

namespace gui2_pages {

fastbootd_page_view build_fastbootd_page(const fastbootd_page_options& options) {
  fastbootd_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  lv_obj_t* body = lv_obj_create(options.content);
  lv_obj_set_pos(body, metrics.content_left, 0);
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
  lv_obj_update_layout(body);
  const int top = lv_obj_get_height(body) + metrics.card_gap;
  progress_page_options progress;
  progress.content = options.content;
  progress.metrics = &metrics;
  progress.strings = &strings;
  progress.console_font = options.console_font;
  progress.task_layout = true;
  progress.log_title = strings.console_tab_output;
  auto task = build_progress_page(progress);
  lv_obj_set_y(task.body, top);
  task.available_height -= top;
  task.bottom_reserved = gui2_core::navigation_safe_area();
  operation_status status;
  status.items = {strings.fastbootd_summary};
  update_progress(&task, {}, status);
  view.console = task.console;
  return view;
}

}  // namespace gui2_pages
