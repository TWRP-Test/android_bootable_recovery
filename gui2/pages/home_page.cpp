#include "pages/home_page.h"

#include <algorithm>

#include "components/icon.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"

namespace gui2_pages {

namespace {

int card_inner_padding(const gui2_core::ui_metrics& metrics) {
  return std::clamp(metrics.outer_margin, gui2_core::ui_px(32), gui2_core::ui_px(56));
}

lv_obj_t* create_action_card(const home_page_options& options, lv_obj_t* parent,
                            const action_definition& definition, int card_width, int card_height,
                            int icon_size) {
  const auto& metrics = *options.metrics;
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, card_width, card_height);
  gui2_core::disable_scrolling(card);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(card, card_height / 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, metrics.card_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card,
                            lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 18),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, gui2_core::ui_px(3), LV_PART_MAIN);
  if (options.press_guard_callback != nullptr)
    lv_obj_add_event_cb(card, options.press_guard_callback, LV_EVENT_ALL, nullptr);
  if (options.action_event_callback != nullptr)
    lv_obj_add_event_cb(card, options.action_event_callback, LV_EVENT_CLICKED,
                        const_cast<action_definition*>(&definition));

  const int side_padding = card_inner_padding(metrics);
  const int title_gap = std::clamp(card_height / 8, gui2_core::ui_px(20), gui2_core::ui_px(28));
  lv_obj_t* icon = lv_obj_create(card);
  lv_obj_set_size(icon, icon_size, icon_size);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, side_padding, 0);
  lv_obj_set_style_radius(icon, icon_size / 4, LV_PART_MAIN);
  gui2_core::set_surface_style(icon, lv_color_hex(definition.color));
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(icon);

  const int art_size = gui2_components::action_icon_art_size(icon_size);
  lv_obj_t* icon_image = gui2_components::create_svg_image(icon, definition.icon, art_size, art_size);
  lv_obj_center(icon_image);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, options.strings->actions[static_cast<int>(definition.id)].title);
  lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
  const int title_left = side_padding + icon_size + title_gap;
  const int title_width = std::max(1, card_width - title_left - side_padding - gui2_core::ui_px(36));
  lv_obj_set_width(title, title_width);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, title_left, 0);
  lv_obj_set_style_text_color(title, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, metrics.text_font, LV_PART_MAIN);

  lv_obj_t* arrow = gui2_components::create_svg_image(
      card, &kGui2IconArrowRight, gui2_core::ui_px(48), gui2_core::ui_px(48));
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -side_padding, 0);
  return card;
}

}  // namespace

void build_home_page(const home_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.actions == nullptr)
    return;

  const auto& metrics = *options.metrics;
  const bool landscape = metrics.width > metrics.height;
  const int columns = landscape && metrics.width >= 800 ? 2 : 1;
  const int card_width =
      (metrics.content_width - metrics.card_gap * (columns - 1)) / columns;

  int cards_top = 0;
  if (options.notice_text != nullptr) {
    const int notice_pad = gui2_core::card_inner_padding();
    lv_point_t size;
    lv_text_get_size(&size, options.notice_text, metrics.status_font, 0, 0,
                     std::max(1, metrics.content_width - notice_pad * 2), LV_TEXT_FLAG_NONE);
    const int notice_height = std::max<int32_t>(1, size.y) + notice_pad * 2;

    lv_obj_t* notice = lv_obj_create(options.content);
    lv_obj_set_size(notice, metrics.content_width, notice_height);
    lv_obj_set_pos(notice, metrics.outer_margin, 0);
    gui2_core::set_surface_style(notice, lv_color_hex(0x2A1010));
    lv_obj_set_style_radius(notice, gui2_core::single_line_card_height() / 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(notice, notice_pad, LV_PART_MAIN);
    lv_obj_set_style_border_width(notice, 0, LV_PART_MAIN);
    lv_obj_add_flag(notice, LV_OBJ_FLAG_CLICKABLE);
    gui2_core::disable_scrolling(notice);
    if (options.press_guard_callback != nullptr)
      lv_obj_add_event_cb(notice, options.press_guard_callback, LV_EVENT_ALL, nullptr);
    if (options.notice_event_callback != nullptr)
      lv_obj_add_event_cb(notice, options.notice_event_callback, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label = lv_label_create(notice);
    lv_label_set_text(label, options.notice_text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, std::max(1, metrics.content_width - notice_pad * 2));
    lv_obj_set_style_text_color(label, lv_color_hex(0xF0443E), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, metrics.status_font, LV_PART_MAIN);

    cards_top = notice_height + metrics.card_gap;
  }

  lv_obj_t* cards = lv_obj_create(options.content);
  lv_obj_set_pos(cards, metrics.outer_margin, cards_top);
  lv_obj_set_width(cards, metrics.content_width);
  lv_obj_set_height(cards, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(cards, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(cards, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(cards, gui2_core::navigation_safe_area(), LV_PART_MAIN);
  lv_obj_set_style_pad_row(cards, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_style_pad_column(cards, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(cards, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(cards, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(cards);

  for (size_t i = 0; i < options.action_count; ++i) {
    create_action_card(options, cards, options.actions[i], card_width, metrics.card_height,
                       metrics.icon_size);
  }
}

}  // namespace gui2_pages
