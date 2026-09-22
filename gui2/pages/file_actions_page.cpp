#include "pages/file_actions_page.h"

#include "components/setting_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

void build_file_actions_page(const file_actions_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
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

  const auto card = [&](const char* title, const void* target) {
    gui2_components::create_setting_card(body, metrics, title, nullptr, options.callback, target,
                                         options.press_guard_callback);
  };

  card(strings.fm_open_terminal, options.terminal_target);
  card(strings.fm_copy, options.copy_target);
  card(strings.fm_move, options.move_target);
  card(strings.fm_chmod755, options.chmod755_target);
  card(strings.fm_chmod, options.chmod_target);
  card(strings.fm_rename, options.rename_target);
  card(strings.fm_delete, options.delete_target);
}

}  // namespace gui2_pages
