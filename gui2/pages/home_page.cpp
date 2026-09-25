#include "pages/home_page.h"

#include <algorithm>

#include "components/icon_card.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"

namespace gui2_pages {

namespace {

lv_obj_t* create_action_card(const home_page_options& options, lv_obj_t* parent,
                            const action_definition& definition, int card_width, int card_height,
                            int icon_size) {
  gui2_components::icon_card_options card;
  card.metrics = options.metrics;
  card.icon = definition.icon;
  card.icon_color = definition.color;
  card.title = options.strings->actions[static_cast<int>(definition.id)].title;
  card.width = card_width;
  card.height = card_height;
  card.icon_size = icon_size;
  card.event_callback = options.action_event_callback;
  card.user_data = &definition;
  card.press_guard_callback = options.press_guard_callback;
  return gui2_components::create_icon_card(parent, card);
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
    lv_obj_set_clickable(notice, true);
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
