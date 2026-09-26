#include "pages/system_read_only_page.h"

#include "components/check_row.h"
#include "components/choice_card.h"
#include "components/tip_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

void build_system_read_only_page(const system_read_only_page_options& options) {
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

  gui2_components::create_tip_card(body, metrics, strings.sys_ro_body, lv_color_hex(0x9BC5E9),
                                   lv_color_hex(0x0E1B2E));
  if (options.show_never_show)
    gui2_components::create_check_row(body, metrics, strings.sys_ro_never_show,
                                      options.never_show, options.never_show_callback, nullptr);
  gui2_components::create_choice_card(body, metrics, strings.sys_ro_keep, metrics.content_width,
                                      gui2_core::single_line_card_height(), options.keep_callback,
                                      nullptr, options.press_guard_callback);
}

}  // namespace gui2_pages
