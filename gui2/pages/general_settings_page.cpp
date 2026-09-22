#include "pages/general_settings_page.h"

#include "components/check_row.h"
#include "components/section_label.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

void build_general_settings_page(const general_settings_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.items == nullptr ||
      options.item_indices == nullptr)
    return;

  const auto& metrics = *options.metrics;

  lv_obj_t* body = lv_obj_create(options.content);
  // Hung straight off the scroll area, so the side margin is this page's job.
  lv_obj_set_pos(body, metrics.outer_margin, 0);
  lv_obj_set_width(body, metrics.content_width);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(body);

  for (size_t i = 0; i < options.item_count; ++i) {
    const general_setting& item = options.items[i];
    if (item.group != nullptr) gui2_components::create_section_label(body, metrics, item.group);
    gui2_components::create_check_row(
        body, metrics, item.label, item.value, options.toggle_callback,
        const_cast<void*>(static_cast<const void*>(&options.item_indices[i])));
  }
}

}  // namespace gui2_pages
