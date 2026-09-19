#include "pages/mount_page.h"

#include "components/check_row.h"
#include "components/section_label.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

void build_mount_page(const mount_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.targets == nullptr || options.target_indices == nullptr)
    return;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;

  lv_obj_t* body = lv_obj_create(options.content);
  lv_obj_set_width(body, metrics.content_width);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(body);

  for (size_t i = 0; i < options.target_count; ++i) {
    gui2_components::create_check_row(
        body, metrics, options.targets[i].name.c_str(), options.targets[i].mounted,
        options.mount_callback,
        const_cast<void*>(static_cast<const void*>(&options.target_indices[i])));
  }

  if (options.has_system && options.system_target != nullptr) {
    gui2_components::create_section_label(body, metrics, strings.mount_system_note);
    gui2_components::create_check_row(
        body, metrics, strings.mount_system_writable, options.system_writable,
        options.system_callback,
        const_cast<void*>(static_cast<const void*>(options.system_target)));
  }
}

}  // namespace gui2_pages
