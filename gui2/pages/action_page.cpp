#include "pages/action_page.h"

#include <algorithm>

#include "components/icon.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"

namespace gui2_pages {

namespace {

int card_inner_padding(const gui2_core::ui_metrics& metrics) {
  return std::clamp(metrics.outer_margin, gui2_core::ui_px(32), gui2_core::ui_px(56));
}

}  // namespace

lv_obj_t* build_action_page(const action_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.definition == nullptr)
    return nullptr;

  const auto& metrics = *options.metrics;
  const auto& action_text =
      options.strings->actions[static_cast<int>(options.definition->id)];
  lv_obj_t* body = lv_obj_create(options.content);
  lv_obj_set_pos(body, metrics.outer_margin, 0);
  lv_obj_set_width(body, metrics.content_width);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(body);
  lv_obj_set_style_pad_bottom(body, gui2_core::navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  int info_height = std::max(metrics.card_height, gui2_core::ui_px(168));
  const int side_padding = card_inner_padding(metrics);
  lv_obj_t* info = lv_obj_create(body);
  lv_obj_set_size(info, metrics.content_width, info_height);
  gui2_core::set_surface_style(info, metrics.card_color);
  lv_obj_set_style_radius(info, info_height / 4, LV_PART_MAIN);
  lv_obj_set_style_pad_all(info, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(info, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(info, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(info, gui2_core::ui_px(3), LV_PART_MAIN);
  gui2_core::disable_scrolling(info);

  const int icon_size = std::min(metrics.icon_size, info_height - gui2_core::ui_px(32));
  lv_obj_t* icon = lv_obj_create(info);
  lv_obj_set_size(icon, icon_size, icon_size);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, side_padding, 0);
  lv_obj_set_style_radius(icon, icon_size / 4, LV_PART_MAIN);
  gui2_core::set_surface_style(icon, lv_color_hex(options.definition->color));
  gui2_core::disable_scrolling(icon);

  const int art_size = gui2_components::action_icon_art_size(icon_size);
  lv_obj_t* icon_image = gui2_components::create_svg_image(
      icon, options.definition->icon, art_size, art_size);
  lv_obj_center(icon_image);

  const int text_left = side_padding + icon_size + metrics.cards_top_gap;
  const int text_width =
      std::max(1, metrics.content_width - text_left - side_padding);
  lv_obj_t* text_block = lv_obj_create(info);
  lv_obj_set_width(text_block, text_width);
  lv_obj_set_height(text_block, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(text_block, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(text_block, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(text_block, gui2_core::ui_px(12), LV_PART_MAIN);
  lv_obj_set_layout(text_block, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(text_block, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(text_block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(text_block, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(text_block);

  lv_obj_t* title = lv_label_create(text_block);
  lv_label_set_text(title, action_text.summary);
  lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(title, text_width);
  lv_obj_set_style_text_color(title, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, metrics.text_font, LV_PART_MAIN);

  lv_obj_t* detail = lv_label_create(text_block);
  lv_label_set_text(detail, action_text.detail);
  lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(detail, text_width);
  lv_obj_set_style_text_color(detail, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(detail, metrics.status_font, LV_PART_MAIN);

  lv_obj_update_layout(text_block);
  info_height = std::max(info_height, lv_obj_get_height(text_block) + gui2_core::ui_px(32));
  lv_obj_set_height(info, info_height);
  lv_obj_set_style_radius(info, info_height / 4, LV_PART_MAIN);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, side_padding, 0);
  lv_obj_align(text_block, LV_ALIGN_LEFT_MID, text_left, 0);
  lv_obj_update_layout(body);
  return body;
}

}  // namespace gui2_pages
