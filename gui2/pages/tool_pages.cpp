#include "pages/tool_pages.h"

#include <algorithm>

#include "components/section_label.h"
#include "components/setting_card.h"
#include "components/tip_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

constexpr uint32_t kAccent = 0x347FF1;

lv_obj_t* create_body(lv_obj_t* content, const gui2_core::ui_metrics& metrics) {
  lv_obj_t* body = lv_obj_create(content);
  lv_obj_set_pos(body, metrics.outer_margin, 0);
  lv_obj_set_size(body, metrics.content_width, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  gui2_core::disable_scrolling(body);
  return body;
}

lv_obj_t* create_label(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, const char* text,
                       lv_color_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.text_font, LV_PART_MAIN);
  return label;
}

void add_row(lv_obj_t* card, const gui2_core::ui_metrics& metrics, const char* name,
             const std::string& value) {
  lv_obj_t* row = lv_obj_create(card);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  gui2_core::set_surface_style(row, metrics.card_color, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(row);
  lv_obj_t* left = create_label(row, metrics, name, metrics.secondary_text);
  lv_obj_align(left, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* right = create_label(row, metrics, value.c_str(), metrics.primary_text);
  lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
}

}  // namespace

void build_partition_options_page(const partition_options_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.details == nullptr)
    return;
  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  const auto& details = *options.details;
  lv_obj_t* body = create_body(options.content, metrics);

  const int padding = gui2_core::card_inner_padding();
  lv_obj_t* info = lv_obj_create(body);
  lv_obj_set_size(info, metrics.content_width, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(info, metrics.card_color);
  lv_obj_set_style_radius(info, gui2_core::single_line_card_height() / 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(info, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(info, padding, LV_PART_MAIN);
  lv_obj_set_style_pad_row(info, gui2_core::ui_px(14), LV_PART_MAIN);
  lv_obj_set_layout(info, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
  gui2_core::disable_scrolling(info);
  add_row(info, metrics, strings.part_mount_point, details.mount_point);
  add_row(info, metrics, strings.part_file_system, details.file_system);
  add_row(info, metrics, strings.part_present,
          details.present ? strings.answer_yes : strings.answer_no);
  add_row(info, metrics, strings.part_removable,
          details.removable ? strings.answer_yes : strings.answer_no);
  add_row(info, metrics, strings.part_size, details.size);
  add_row(info, metrics, strings.part_used, details.used);
  add_row(info, metrics, strings.part_free, details.free);
  add_row(info, metrics, strings.part_backup_size, details.backup_size);

  if (details.can_repair)
    gui2_components::create_setting_card(body, metrics, strings.repair_fs, nullptr,
                                         options.action_callback, options.repair_target,
                                         options.press_guard_callback);
  if (details.can_resize)
    gui2_components::create_setting_card(body, metrics, strings.resize_fs, nullptr,
                                         options.action_callback, options.resize_target,
                                         options.press_guard_callback);
  if (!details.file_systems.empty())
    gui2_components::create_setting_card(body, metrics, strings.change_fs, nullptr,
                                         options.action_callback, options.change_target,
                                         options.press_guard_callback);
}

const char* file_system_label(const std::string& value) {
  if (value == "ext2") return "EXT2";
  if (value == "ext3") return "EXT3";
  if (value == "ext4") return "EXT4";
  if (value == "vfat") return "FAT";
  if (value == "exfat") return "exFAT";
  if (value == "f2fs") return "F2FS";
  return value.c_str();
}

void build_change_fs_page(const change_fs_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.details == nullptr || options.choice_indices == nullptr)
    return;
  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  const auto& details = *options.details;
  lv_obj_t* body = create_body(options.content, metrics);

  gui2_components::create_tip_card(body, metrics, strings.change_fs_warning,
                                   lv_color_hex(0xFFC46B), lv_color_hex(0x2E2412));
  gui2_components::create_section_label(body, metrics, strings.change_fs_new);

  const int height = gui2_core::single_line_card_height();
  const int padding = gui2_core::card_inner_padding();
  for (size_t i = 0; i < details.file_systems.size(); ++i) {
    const bool selected = static_cast<int>(i) == options.selected;
    lv_obj_t* card = lv_obj_create(body);
    lv_obj_set_size(card, metrics.content_width, height);
    lv_obj_set_style_radius(card, height / 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        card, selected ? lv_color_mix(lv_color_hex(kAccent), metrics.card_color, 30)
                       : metrics.card_color,
        LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 18),
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, selected ? gui2_core::ui_px(5) : 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(kAccent), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(card, padding, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(card, 0, LV_PART_MAIN);
    gui2_core::disable_scrolling(card);
    lv_obj_set_clickable(card, true);
    if (options.press_guard_callback != nullptr)
      lv_obj_add_event_cb(card, options.press_guard_callback, LV_EVENT_ALL, nullptr);
    if (options.choice_callback != nullptr)
      lv_obj_add_event_cb(card, options.choice_callback, LV_EVENT_CLICKED,
                          const_cast<int*>(&options.choice_indices[i]));

    lv_obj_t* name = create_label(card, metrics, file_system_label(details.file_systems[i]),
                                  metrics.primary_text);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
    if (details.file_systems[i] == details.file_system) {
      lv_obj_t* current = create_label(card, metrics, strings.fs_current, metrics.secondary_text);
      lv_obj_set_style_text_font(current, metrics.status_font, LV_PART_MAIN);
      lv_obj_align(current, LV_ALIGN_RIGHT_MID, 0, 0);
    }
  }
}

void build_confirm_page(const confirm_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr) return;
  const auto& metrics = *options.metrics;
  lv_obj_t* body = create_body(options.content, metrics);
  uint32_t text = 0x9BC5E9;
  uint32_t surface = 0x0E1B2E;
  if (options.tone == confirm_tone::WARNING) {
    text = 0xFFC46B;
    surface = 0x2E2412;
  } else if (options.tone == confirm_tone::DANGER) {
    text = 0xFF8A80;
    surface = 0x2E1414;
  }
  gui2_components::create_tip_card(body, metrics, options.text, lv_color_hex(text),
                                   lv_color_hex(surface));
}

}  // namespace gui2_pages
