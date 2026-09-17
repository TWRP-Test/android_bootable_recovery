#include "pages/export_log_page.h"

#include "components/section_label.h"
#include "components/switch_row.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

export_log_page_view build_export_log_page(const export_log_page_options& options) {
  export_log_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;

  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.outer_margin, 0);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(view.body, gui2_core::navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_add_flag(view.body, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  gui2_core::disable_scrolling(view.body);

  view.kernel_log_card = gui2_components::create_switch_row(
      view.body, metrics, strings.include_kernel_log, options.include_kernel_log,
      options.option_event_callback, const_cast<void*>(options.kernel_log_target));

  if (options.has_logcat) {
    view.logcat_card = gui2_components::create_switch_row(
        view.body, metrics, strings.include_logcat, options.include_logcat,
        options.option_event_callback, const_cast<void*>(options.logcat_target));
  }

  view.result_label = gui2_components::create_section_label(view.body, metrics, "");
  lv_obj_add_flag(view.result_label, LV_OBJ_FLAG_HIDDEN);
  return view;
}

}  // namespace gui2_pages
