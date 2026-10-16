#include "pages/mount_page.h"

#include <algorithm>
#include <cstdio>

#include "components/check_row.h"
#include "components/section_label.h"
#include "components/setting_card.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

mount_page_view build_mount_page(const mount_page_options& options) {
  mount_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr ||
      options.targets == nullptr || options.target_indices == nullptr)
    return view;

  view.rows.resize(options.target_count);
  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;

  lv_obj_t* body = lv_obj_create(options.content);
  lv_obj_set_width(body, metrics.content_width);
  lv_obj_set_height(body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(body, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::set_card_grid(body);
  gui2_core::disable_scrolling(body);

  if (options.storage_name != nullptr && options.storage_name[0] != '\0') {
    gui2_components::create_section_label(body, metrics, strings.mount_storage_section);
    gui2_components::create_setting_card(body, metrics, options.storage_name, options.storage_free,
                                         options.storage_callback, options.storage_target,
                                         options.press_guard_callback);
  }

  const auto add_mount_row = [&](size_t i) {
    lv_obj_t* toggle = gui2_components::create_check_row(
        body, metrics, options.targets[i].name.c_str(), options.targets[i].mounted,
        options.mount_callback,
        const_cast<void*>(static_cast<const void*>(&options.target_indices[i])));
    // Keep every row and its notice alive for in-place status updates.
    lv_obj_t* card = lv_obj_get_parent(toggle);
    lv_obj_t* title = lv_obj_get_child(card, 0);
    lv_obj_t* text = lv_obj_create(card);
    const int text_width = gui2_core::card_width_in(body) -
                           gui2_core::card_inner_padding() * 2 -
                           lv_obj_get_style_width(toggle, LV_PART_MAIN) - gui2_core::ui_px(16);
    lv_obj_set_size(text, std::max(1, text_width), LV_SIZE_CONTENT);
    lv_obj_set_x(text, gui2_core::card_inner_padding());
    gui2_core::set_surface_style(text, metrics.background, LV_OPA_TRANSP);
    lv_obj_set_style_pad_all(text, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(text, gui2_core::ui_px(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    gui2_core::disable_scrolling(text);
    lv_obj_set_clickable(text, false);
    lv_obj_set_parent(title, text);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_size(title, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(card, lv_obj_get_style_height(card, LV_PART_MAIN), LV_PART_MAIN);
    lv_obj_t* label = lv_label_create(text);
    const bool has_feedback = options.feedback_path != nullptr && options.feedback_text != nullptr &&
                              options.feedback_text[0] != '\0' &&
                              options.targets[i].mount_point == options.feedback_path;
    lv_label_set_text(label, has_feedback ? options.feedback_text : "");
    lv_obj_set_hidden(label, !has_feedback);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, metrics.status_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, options.feedback_failed ? metrics.danger :
                                   (options.targets[i].mounted ? metrics.success : metrics.secondary_text),
                               LV_PART_MAIN);
    view.rows[i] = {toggle, label};
  };

  gui2_components::create_section_label(body, metrics, strings.mount_partitions_section);
  for (size_t i = 0; i < options.target_count; ++i) {
    if (gui2_backend::is_otg_path(options.targets[i].mount_point)) continue;
    add_mount_row(i);
  }

  if (options.has_system && options.system_target != nullptr) {
    gui2_components::create_section_label(body, metrics, strings.mount_system_note);
    gui2_components::create_check_row(
        body, metrics, strings.mount_system_writable, options.system_writable,
        options.system_callback,
        const_cast<void*>(static_cast<const void*>(options.system_target)));
  }

  gui2_components::create_section_label(body, metrics, strings.mount_other_section);
  for (size_t i = 0; i < options.target_count; ++i) {
    if (!gui2_backend::is_otg_path(options.targets[i].mount_point)) continue;
    add_mount_row(i);
  }
  if (options.mtp_target != nullptr) {
    gui2_components::create_check_row(
        body, metrics, strings.mount_mtp, options.mtp_enabled, options.toggle_callback,
        const_cast<void*>(static_cast<const void*>(options.mtp_target)));
  }
  if (options.has_usb_storage && options.usb_storage_target != nullptr) {
    gui2_components::create_check_row(
        body, metrics, strings.mount_usb_storage, options.usb_storage_enabled,
        options.toggle_callback,
        const_cast<void*>(static_cast<const void*>(options.usb_storage_target)));
  }
  if (options.has_decrypt) {
    gui2_components::create_setting_card(body, metrics, strings.mount_decrypt_data,
                                         strings.mount_decrypt_data_detail,
                                         options.decrypt_callback, options.decrypt_target,
                                         options.press_guard_callback);
  }
  return view;
}

}  // namespace gui2_pages
