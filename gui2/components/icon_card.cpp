#include "components/icon_card.h"

#include <algorithm>

#include "components/icon.h"
#include "core/ui_helpers.h"
#include "gui2_svg_assets.h"

namespace gui2_components {

int icon_card_padding(const gui2_core::ui_metrics& metrics) {
  return std::clamp(metrics.outer_margin, gui2_core::ui_px(32), gui2_core::ui_px(56));
}

lv_obj_t* create_icon_card(lv_obj_t* parent, const icon_card_options& options) {
  if (parent == nullptr || options.metrics == nullptr) return nullptr;
  const auto& metrics = *options.metrics;
  const int card_height = options.height;

  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, options.width, card_height);
  gui2_core::disable_scrolling(card);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(card, card_height / 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, metrics.card_color, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 18),
                            LV_STATE_PRESSED);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, gui2_core::ui_px(3), LV_PART_MAIN);
  if (options.press_guard_callback != nullptr)
    lv_obj_add_event_cb(card, options.press_guard_callback, LV_EVENT_ALL, nullptr);
  if (options.event_callback != nullptr)
    lv_obj_add_event_cb(card, options.event_callback, LV_EVENT_CLICKED,
                        const_cast<void*>(options.user_data));

  const int side_padding = icon_card_padding(metrics);
  const int title_gap = std::clamp(card_height / 8, gui2_core::ui_px(20), gui2_core::ui_px(28));

  lv_obj_t* icon = lv_obj_create(card);
  lv_obj_set_size(icon, options.icon_size, options.icon_size);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, side_padding, 0);
  lv_obj_set_style_radius(icon, options.icon_size / 4, LV_PART_MAIN);
  gui2_core::set_surface_style(icon, lv_color_hex(options.icon_color));
  lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  gui2_core::disable_scrolling(icon);

  if (options.icon != nullptr) {
    const int art_size =
        options.art_size > 0 ? options.art_size : action_icon_art_size(options.icon_size);
    lv_obj_t* art = create_svg_image(icon, options.icon, art_size, art_size);
    lv_obj_center(art);
  }

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, options.title == nullptr ? "" : options.title);
  lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
  const int title_left = side_padding + options.icon_size + title_gap;
  const int trailing = options.show_arrow ? gui2_core::ui_px(36) : 0;
  const int title_width = std::max(1, options.width - title_left - side_padding - trailing);
  lv_obj_set_width(title, title_width);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, title_left, 0);
  lv_obj_set_style_text_color(title, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, metrics.text_font, LV_PART_MAIN);

  if (options.show_arrow) {
    lv_obj_t* arrow =
        create_svg_image(card, &kGui2IconArrowRight, gui2_core::ui_px(48), gui2_core::ui_px(48));
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -side_padding, 0);
  }
  return card;
}

}  // namespace gui2_components
