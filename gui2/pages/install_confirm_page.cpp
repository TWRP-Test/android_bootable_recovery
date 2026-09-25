#include "pages/install_confirm_page.h"

#include <algorithm>

#include "components/check_row.h"
#include "components/section_label.h"
#include "components/setting_card.h"
#include "components/tip_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

constexpr uint32_t kAccent = 0x347FF1;
constexpr uint32_t kWarning = 0xE8A33D;
constexpr uint32_t kWarningSurface = 0x2A1F0D;

lv_obj_t* create_column(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, int width,
                        int gap) {
  lv_obj_t* column = lv_obj_create(parent);
  lv_obj_set_width(column, width);
  lv_obj_set_height(column, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(column, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(column, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(column, gap, LV_PART_MAIN);
  lv_obj_set_layout(column, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(column);
  return column;
}

// A card that only reads back what was picked, so it carries no arrow and
// takes no taps.
lv_obj_t* create_info_card(lv_obj_t* parent, const gui2_core::ui_metrics& metrics) {
  const int padding = gui2_core::card_inner_padding();
  lv_obj_t* card = create_column(parent, metrics, metrics.content_width, gui2_core::ui_px(8));
  gui2_core::set_surface_style(card, metrics.card_color);
  lv_obj_set_style_pad_all(card, padding, LV_PART_MAIN);
  lv_obj_set_style_radius(card, gui2_core::single_line_card_height() / 4, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, gui2_core::ui_px(3), LV_PART_MAIN);
  return card;
}

lv_obj_t* add_line(lv_obj_t* card, const gui2_core::ui_metrics& metrics, const char* text,
                   lv_color_t color, const lv_font_t* font) {
  lv_obj_t* label = lv_label_create(card);
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, std::max(1, metrics.content_width - gui2_core::card_inner_padding() * 2));
  lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  return label;
}

lv_obj_t* create_flat_button(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, int width,
                             const char* text, lv_event_cb_t callback,
                             lv_event_cb_t press_guard_callback) {
  const int height = gui2_core::single_line_card_height();
  lv_obj_t* button = lv_obj_create(parent);
  lv_obj_set_size(button, width, height);
  lv_obj_set_clickable(button, true);
  gui2_core::set_surface_style(button, metrics.card_color);
  lv_obj_set_style_radius(button, height / 3, LV_PART_MAIN);
  lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 18),
                            LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(button, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(button, 45, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(button, gui2_core::ui_px(3), LV_PART_MAIN);
  gui2_core::disable_scrolling(button);
  if (press_guard_callback != nullptr)
    lv_obj_add_event_cb(button, press_guard_callback, LV_EVENT_ALL, nullptr);
  if (callback != nullptr) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(label, lv_color_hex(kAccent), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.text_font, LV_PART_MAIN);
  lv_obj_center(label);
  return button;
}

void build_options(lv_obj_t* body, const install_confirm_page_options& options) {
  if (options.option_list == nullptr || options.option_count == 0 ||
      options.option_indices == nullptr)
    return;
  const auto& metrics = *options.metrics;
  gui2_components::create_section_label(body, metrics, options.strings->install_options);
  for (size_t i = 0; i < options.option_count; ++i)
    gui2_components::create_check_row(
        body, metrics, options.option_list[i].label, options.option_list[i].value,
        options.option_callback,
        const_cast<void*>(static_cast<const void*>(&options.option_indices[i])));
}

}  // namespace

void build_install_confirm_page(const install_confirm_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;

  lv_obj_t* body = create_column(options.content, metrics, metrics.content_width, metrics.card_gap);
  lv_obj_set_pos(body, metrics.outer_margin, 0);

  // What this is about to do, before anything that can change it.
  if (!options.image)
    gui2_components::create_tip_card(body, metrics, strings.install_warning,
                                     lv_color_hex(kWarning), lv_color_hex(kWarningSurface));

  lv_obj_t* preview = create_info_card(body, metrics);
  add_line(preview, metrics, strings.install_folder, metrics.secondary_text, metrics.status_font);
  add_line(preview, metrics, options.folder, metrics.primary_text, metrics.text_font);
  add_line(preview, metrics, strings.install_file, metrics.secondary_text, metrics.status_font);
  add_line(preview, metrics, options.file, metrics.primary_text, metrics.text_font);

  if (options.image) {
    gui2_components::create_section_label(body, metrics, strings.install_target);
    for (size_t i = 0; i < options.target_count; ++i) {
      lv_obj_t* card = gui2_components::create_setting_card(
          body, metrics, options.targets[i].name.c_str(), options.targets[i].mount_point.c_str(),
          options.target_callback,
          options.target_indices == nullptr ? nullptr : &options.target_indices[i],
          options.press_guard_callback);
      if (card != nullptr && i == options.selected_target) {
        lv_obj_set_style_border_width(card, gui2_core::ui_px(4), LV_PART_MAIN);
        lv_obj_set_style_border_color(card, lv_color_hex(kAccent), LV_PART_MAIN);
        lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_PART_MAIN);
      }
    }
    build_options(body, options);
    return;
  }

  // The count lives under the title, so the list only shows up once there is
  // more than the one zip the preview above already names.
  if (options.queue != nullptr && options.queue_count > 1) {
    gui2_components::create_section_label(body, metrics, strings.install_queue);
    lv_obj_t* queued = create_info_card(body, metrics);
    for (size_t i = 0; i < options.queue_count; ++i)
      add_line(queued, metrics, options.queue[i], metrics.primary_text, metrics.text_font);
  }

  build_options(body, options);

  if (options.add_zip_callback == nullptr && options.clear_queue_callback == nullptr) return;
  const int gap = metrics.card_gap;
  lv_obj_t* row = create_column(body, metrics, metrics.content_width, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, gap, LV_PART_MAIN);
  const int width = std::max(1, (metrics.content_width - gap) / 2);
  create_flat_button(row, metrics, width, strings.install_add_zip, options.add_zip_callback,
                     options.press_guard_callback);
  create_flat_button(row, metrics, width, strings.install_clear_queue,
                     options.clear_queue_callback, options.press_guard_callback);
}

}  // namespace gui2_pages
