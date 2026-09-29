#include "pages/backup_page.h"

#include <algorithm>

#include "components/check_row.h"
#include "components/flat_button.h"
#include "components/section_label.h"
#include "pages/wipe_page.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

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

void show_keyboard_cb(lv_event_t* event) {
  auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
  if (keyboard != nullptr) lv_obj_set_hidden(keyboard, false);
}

void hide_keyboard_cb(lv_event_t* event) {
  auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (keyboard != nullptr) lv_obj_set_hidden(keyboard, true);
}

// The <restrict> of the legacy name and password inputs.
constexpr const char* kNameChars =
    " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890-_.{}[]";
constexpr const char* kPasswordChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890_";

lv_obj_t* create_input(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, bool password,
                       uint32_t max_length, const char* accepted, const char* placeholder) {
  const int input_height = gui2_core::single_line_card_height() * 11 / 10;
  const int input_pad = std::max(0, (input_height - metrics.text_font->line_height) / 2);
  lv_obj_t* input = lv_textarea_create(parent);
  lv_textarea_set_one_line(input, true);
  lv_obj_set_size(input, metrics.content_width, input_height);
  lv_obj_set_style_pad_top(input, input_pad, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(input, input_pad, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(input, gui2_core::card_inner_padding(), LV_PART_MAIN);
  if (password) lv_textarea_set_password_mode(input, true);
  lv_textarea_set_max_length(input, max_length);
  lv_textarea_set_accepted_chars(input, accepted);
  if (placeholder != nullptr) lv_textarea_set_placeholder_text(input, placeholder);
  lv_obj_set_scrollbar_mode(input, LV_SCROLLBAR_MODE_OFF);
  gui2_core::set_surface_style(input, metrics.card_color);
  lv_obj_set_style_radius(input, input_height / 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(input, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(input, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_color(input, metrics.secondary_text, LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_text_font(input, metrics.text_font, LV_PART_MAIN);
  return input;
}

}  // namespace

backup_page_view build_backup_page(const backup_page_options& options) {
  backup_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  const auto& strings = *options.strings;

  view.body = create_column(options.content, metrics);

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
  if (options.select_storage_callback != nullptr || options.refresh_sizes_callback != nullptr)
    gui2_components::create_flat_button_row(
        view.partitions_pane, metrics, strings.select_storage_title,
        options.select_storage_callback, strings.refresh_sizes_button,
        options.refresh_sizes_callback, options.press_guard_callback);

  view.options_pane = create_column(view.body, metrics);

  gui2_components::create_section_label(view.options_pane, metrics, strings.backup_name_label);

  view.name_input = create_input(view.options_pane, metrics, false, 64, kNameChars,
                                 strings.backup_auto_name);
  if (options.name != nullptr) lv_textarea_set_text(view.name_input, options.name);
  if (options.name_focus_callback != nullptr)
    lv_obj_add_event_cb(view.name_input, options.name_focus_callback, LV_EVENT_FOCUSED, nullptr);
  if (options.append_date_callback != nullptr)
    gui2_components::create_flat_button(view.options_pane, metrics, metrics.content_width,
                                        strings.backup_append_date,
                                        options.append_date_callback,
                                        options.press_guard_callback);

  if (options.compress_target != nullptr) {
    gui2_components::create_check_row(
        view.options_pane, metrics, strings.backup_compress, options.compress,
        options.option_callback,
        const_cast<void*>(static_cast<const void*>(options.compress_target)));
  }
  if (options.skip_digest_target != nullptr) {
    gui2_components::create_check_row(
        view.options_pane, metrics, strings.backup_skip_digest, options.skip_digest,
        options.option_callback,
        const_cast<void*>(static_cast<const void*>(options.skip_digest_target)));
  }
  if (options.disable_free_space_target != nullptr) {
    gui2_components::create_check_row(
        view.options_pane, metrics, strings.general_disable_free_space, options.disable_free_space,
        options.option_callback,
        const_cast<void*>(static_cast<const void*>(options.disable_free_space_target)));
  }
  if (options.encrypt_target != nullptr) {
    gui2_components::create_check_row(
        view.options_pane, metrics, strings.backup_encrypt, options.encrypt,
        options.option_callback,
        const_cast<void*>(static_cast<const void*>(options.encrypt_target)));

    view.password_block = create_column(view.options_pane, metrics);
    gui2_components::create_section_label(view.password_block, metrics, strings.backup_password);
    view.password_input = create_input(view.password_block, metrics, true, 32, kPasswordChars,
                                       options.password_mismatch
                                           ? strings.backup_password_mismatch
                                           : nullptr);
    gui2_components::create_section_label(view.password_block, metrics,
                                          strings.backup_password_confirm);
    view.password_confirm_input =
        create_input(view.password_block, metrics, true, 32, kPasswordChars, nullptr);
  }

  if (options.slot_label != nullptr) {
    gui2_components::create_section_label(view.options_pane, metrics, options.slot_label);
    gui2_components::create_flat_button_row(view.options_pane, metrics, strings.backup_slot_a,
                                            options.slot_a_callback, strings.backup_slot_b,
                                            options.slot_b_callback,
                                            options.press_guard_callback);
  }

  if (options.keyboard != nullptr) {
    gui2_components::keyboard_options keyboard;
    keyboard.parent = options.overlay_layer != nullptr ? options.overlay_layer : view.options_pane;
    keyboard.metrics = &metrics;
    keyboard.strings = &strings;
    keyboard.textarea = view.name_input;
    keyboard.start_hidden = options.overlay_layer != nullptr;
    keyboard.key_callback = options.key_callback;
    keyboard.user_data = options.keyboard_user_data;
    view.keyboard = options.keyboard->create(keyboard);
    if (view.keyboard != nullptr && options.overlay_layer != nullptr) {
      lv_obj_set_align(view.keyboard, LV_ALIGN_TOP_LEFT);
      lv_obj_set_pos(view.keyboard, 0,
                     metrics.height - gui2_components::keyboard_height(
                                          metrics, gui2_components::keyboard_layout::LETTERS));
      options.keyboard->bind(view.name_input);
      options.keyboard->bind(view.password_input);
      options.keyboard->bind(view.password_confirm_input);
    }
  }

  show_backup_password(view, options.encrypt);
  show_backup_tab(view, options.active_tab);

  if (options.confirm != nullptr && options.page_layer != nullptr) {
    const int track_height = wipe_track_height();
    const int page_height = metrics.height - metrics.status_height - metrics.nav_height;
    view.slider_track = options.confirm->create(
        options.page_layer, metrics, metrics.outer_margin,
        page_height - track_height - metrics.cards_top_gap, metrics.content_width, track_height,
        strings.swipe_backup, options.confirm_callback, options.confirm_user_data);
  }
  return view;
}

void show_backup_password(const backup_page_view& view, bool visible) {
  if (view.password_block == nullptr) return;
  if (visible)
    lv_obj_set_hidden(view.password_block, false);
  else
    lv_obj_set_hidden(view.password_block, true);
}

void show_backup_tab(const backup_page_view& view, size_t index) {
  lv_obj_t* panes[2] = { view.partitions_pane, view.options_pane };
  for (size_t i = 0; i < 2; ++i) {
    if (panes[i] == nullptr) continue;
    if (i == index)
      lv_obj_set_hidden(panes[i], false);
    else
      lv_obj_set_hidden(panes[i], true);
  }
  if (index != 1 && view.keyboard != nullptr) lv_obj_set_hidden(view.keyboard, true);
}

}  // namespace gui2_pages
