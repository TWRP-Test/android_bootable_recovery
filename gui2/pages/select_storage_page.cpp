#include "pages/select_storage_page.h"

#include "components/setting_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

void build_select_storage_page(const select_storage_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.storages == nullptr || options.storage_indices == nullptr)
    return;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;

  lv_obj_t* body = lv_obj_create(options.content);
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

  for (size_t i = 0; i < options.storage_count; ++i) {
    const gui2_backend::storage_device& storage = options.storages[i];
    lv_obj_t* card = gui2_components::create_setting_card(
        body, metrics, storage.name.c_str(),
        storage.selected ? strings.select_storage_current : storage.path.c_str(),
        options.select_callback, &options.storage_indices[i], options.press_guard_callback);
    // The current one is marked the same way the reboot page marks its target.
    if (card != nullptr && storage.selected) {
      lv_obj_set_style_border_width(card, gui2_core::ui_px(4), LV_PART_MAIN);
      lv_obj_set_style_border_color(card, lv_color_hex(0x347FF1), LV_PART_MAIN);
      lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    }
  }
}

}  // namespace gui2_pages
