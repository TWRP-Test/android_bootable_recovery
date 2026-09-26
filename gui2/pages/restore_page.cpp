#include "pages/restore_page.h"

#include <algorithm>

#include "components/check_row.h"
#include "components/section_label.h"
#include "components/tip_card.h"
#include "components/setting_card.h"
#include "core/ui_helpers.h"
#include "pages/wipe_page.h"

namespace gui2_pages {

namespace {

constexpr uint32_t kWarning = 0xF0A73E;
constexpr uint32_t kAccent = 0x347FF1;
// The same treatment as the warning banners, in the accent instead: a notice,
// not a problem.
constexpr uint32_t kAccentSurface = 0x0E1B2E;

lv_obj_t* create_column(lv_obj_t* parent, const gui2_core::ui_metrics& metrics) {
  lv_obj_t* column = lv_obj_create(parent);
  lv_obj_set_width(column, metrics.content_width);
  lv_obj_set_height(column, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(column, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(column, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(column, metrics.card_gap, LV_PART_MAIN);
  lv_obj_set_layout(column, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(column);
  return column;
}

// The detail page hangs straight off the page content, which is not inset.
lv_obj_t* create_body(lv_obj_t* parent, const gui2_core::ui_metrics& metrics) {
  lv_obj_t* body = create_column(parent, metrics);
  lv_obj_set_pos(body, metrics.outer_margin, 0);
  return body;
}

}  // namespace

void build_restore_list_page(const restore_list_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;
  lv_obj_t* body = create_column(options.content, metrics);

  if (options.backup_count == 0 || options.backups == nullptr) {
    gui2_components::create_tip_card(body, metrics, strings.restore_none, lv_color_hex(kAccent),
                                     lv_color_hex(kAccentSurface));
    return;
  }

  gui2_components::create_section_label(body, metrics, strings.restore_choose);
  for (size_t i = 0; i < options.backup_count; ++i) {
    gui2_components::create_setting_card(
        body, metrics, options.backups[i].name.c_str(), options.backups[i].detail.c_str(),
        options.select_callback,
        options.backup_indices == nullptr
            ? nullptr
            : static_cast<const void*>(&options.backup_indices[i]),
        options.press_guard_callback);
  }
}

restore_page_view build_restore_page(const restore_page_options& options) {
  restore_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;

  view.body = create_body(options.content, metrics);

  if (options.tabs != nullptr) {
    const char* labels[2] = { strings.backup_partitions_tab, strings.backup_options_tab };
    options.tabs->create(view.body, metrics, labels, 2, options.active_tab, options.tab_callback,
                         options.tab_user_data);
  }

  view.partitions_pane = create_column(view.body, metrics);
  if (options.targets != nullptr && options.selected != nullptr &&
      options.target_indices != nullptr) {
    for (size_t i = 0; i < options.target_count; ++i) {
      gui2_components::create_check_row(
          view.partitions_pane, metrics, options.targets[i].name.c_str(), options.selected[i],
          options.selection_callback,
          const_cast<void*>(static_cast<const void*>(&options.target_indices[i])));
    }
  }

  view.options_pane = create_column(view.body, metrics);

  if (options.check_digest_target != nullptr) {
    gui2_components::create_check_row(
        view.options_pane, metrics, strings.restore_verify_digest, options.check_digest,
        options.option_callback,
        const_cast<void*>(static_cast<const void*>(options.check_digest_target)));
  }

  if (options.manage_callback != nullptr) {
    gui2_components::create_section_label(view.options_pane, metrics, strings.manage_backup);
    gui2_components::create_setting_card(view.options_pane, metrics, strings.rename_backup,
                                         nullptr, options.manage_callback, options.rename_target,
                                         options.press_guard_callback);
    lv_obj_t* remove = gui2_components::create_setting_card(
        view.options_pane, metrics, strings.delete_backup, nullptr, options.manage_callback,
        options.delete_target, options.press_guard_callback);
    lv_obj_set_style_text_color(lv_obj_get_child(lv_obj_get_child(remove, 0), 0),
                                lv_color_hex(0xF0443E), LV_PART_MAIN);
  }

  // A password is only ever asked for when the folder turned out to be
  // encrypted, which is something the backup itself says.
  if (options.encrypted) {
    lv_obj_t* notice =
        gui2_components::create_section_label(view.options_pane, metrics,
                                              strings.restore_encrypted_notice);
    lv_obj_set_style_text_color(notice, lv_color_hex(kWarning), LV_PART_MAIN);

    gui2_components::create_section_label(view.options_pane, metrics, strings.backup_password);
    const int input_height = gui2_core::single_line_card_height() * 11 / 10;
    const int input_pad = std::max(0, (input_height - metrics.text_font->line_height) / 2);
    view.password_input = lv_textarea_create(view.options_pane);
    lv_textarea_set_one_line(view.password_input, true);
    lv_obj_set_size(view.password_input, metrics.content_width, input_height);
    lv_obj_set_style_pad_top(view.password_input, input_pad, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(view.password_input, input_pad, LV_PART_MAIN);
    lv_textarea_set_password_mode(view.password_input, true);
    lv_textarea_set_max_length(view.password_input, 64);
    lv_obj_set_scrollbar_mode(view.password_input, LV_SCROLLBAR_MODE_OFF);
    gui2_core::set_surface_style(view.password_input, metrics.card_color);
    lv_obj_set_style_radius(view.password_input, input_height / 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(view.password_input, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(view.password_input, metrics.primary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(view.password_input, metrics.text_font, LV_PART_MAIN);

    if (options.wrong_password) {
      lv_obj_t* error = gui2_components::create_section_label(
          view.options_pane, metrics, strings.restore_wrong_password);
      lv_obj_set_style_text_color(error, lv_color_hex(0xF0443E), LV_PART_MAIN);
    }
  }

  if (options.keyboard != nullptr && view.password_input != nullptr) {
    gui2_components::keyboard_options keyboard;
    keyboard.parent = options.overlay_layer != nullptr ? options.overlay_layer : view.options_pane;
    keyboard.metrics = &metrics;
    keyboard.strings = &strings;
    keyboard.textarea = view.password_input;
    keyboard.start_hidden = options.overlay_layer != nullptr;
    keyboard.key_callback = options.key_callback;
    keyboard.user_data = options.keyboard_user_data;
    view.keyboard = options.keyboard->create(keyboard);
    if (view.keyboard != nullptr && options.overlay_layer != nullptr) {
      lv_obj_set_align(view.keyboard, LV_ALIGN_TOP_LEFT);
      lv_obj_set_pos(view.keyboard, 0,
                     metrics.height - gui2_components::keyboard_height(
                                          metrics, gui2_components::keyboard_layout::LETTERS));
      options.keyboard->bind(view.password_input);
    }
  }

  show_restore_tab(view, options.active_tab);

  if (options.confirm != nullptr && options.page_layer != nullptr) {
    const int track_height = wipe_track_height();
    const int page_height = metrics.height - metrics.status_height - metrics.nav_height;
    view.slider_track = options.confirm->create(
        options.page_layer, metrics, metrics.outer_margin,
        page_height - track_height - metrics.cards_top_gap, metrics.content_width, track_height,
        strings.swipe_restore, options.confirm_callback, options.confirm_user_data);
  }
  return view;
}

void show_restore_tab(const restore_page_view& view, size_t index) {
  if (view.partitions_pane == nullptr || view.options_pane == nullptr) return;
  lv_obj_t* panes[2] = { view.partitions_pane, view.options_pane };
  for (size_t i = 0; i < 2; ++i) {
    if (i == index)
      lv_obj_set_hidden(panes[i], false);
    else
      lv_obj_set_hidden(panes[i], true);
  }
}

}  // namespace gui2_pages
