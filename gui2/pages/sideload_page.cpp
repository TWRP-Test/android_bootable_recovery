#include "pages/sideload_page.h"

#include "components/check_row.h"
#include "components/section_label.h"
#include "components/tip_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

const int kOptionIndices[2] = { 0, 1 };

}  // namespace

void build_sideload_page(const sideload_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return;

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

  gui2_components::create_tip_card(body, metrics, strings.sideload_hint, lv_color_hex(0x9BC5E9),
                                   lv_color_hex(0x0E1B2E));
  gui2_components::create_section_label(body, metrics, strings.install_options);
  gui2_components::create_check_row(body, metrics, strings.sideload_wipe_dalvik,
                                    options.wipe_dalvik, options.option_callback,
                                    const_cast<int*>(&kOptionIndices[0]));
  gui2_components::create_check_row(body, metrics, strings.sideload_wipe_cache, options.wipe_cache,
                                    options.option_callback, const_cast<int*>(&kOptionIndices[1]));
}

}  // namespace gui2_pages
