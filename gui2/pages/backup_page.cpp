#include "pages/backup_page.h"

#include <algorithm>

#include "components/check_row.h"
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
  if (keyboard != nullptr) lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void hide_keyboard_cb(lv_event_t* event) {
  auto* keyboard = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (keyboard != nullptr) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
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

  view.options_pane = create_column(view.body, metrics);

  gui2_components::create_section_label(view.options_pane, metrics, strings.backup_name_label);

  const int input_height = gui2_core::single_line_card_height() * 11 / 10;
  const int input_pad = std::max(0, (input_height - metrics.text_font->line_height) / 2);
  view.name_input = lv_textarea_create(view.options_pane);
  lv_textarea_set_one_line(view.name_input, true);
  lv_obj_set_size(view.name_input, metrics.content_width, input_height);
  lv_obj_set_style_pad_top(view.name_input, input_pad, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(view.name_input, input_pad, LV_PART_MAIN);
  lv_textarea_set_max_length(view.name_input, 64);
  lv_obj_set_scrollbar_mode(view.name_input, LV_SCROLLBAR_MODE_OFF);
  gui2_core::set_surface_style(view.name_input, metrics.card_color);
  lv_obj_set_style_radius(view.name_input, input_height / 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(view.name_input, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(view.name_input, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.name_input, metrics.text_font, LV_PART_MAIN);

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
  if (options.encrypt_target != nullptr) {
    gui2_components::create_check_row(
        view.options_pane, metrics, strings.backup_encrypt, options.encrypt,
        options.option_callback,
        const_cast<void*>(static_cast<const void*>(options.encrypt_target)));

    view.password_block = create_column(view.options_pane, metrics);
    gui2_components::create_section_label(view.password_block, metrics, strings.backup_password);
    view.password_input = lv_textarea_create(view.password_block);
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
  }

  const int keyboard_height = std::min(metrics.height / 3, metrics.width * 2 / 3);
  lv_obj_t* parent = options.overlay_layer != nullptr ? options.overlay_layer : view.options_pane;
  view.keyboard = lv_keyboard_create(parent);
  lv_obj_set_size(view.keyboard, metrics.width, keyboard_height);
  lv_keyboard_set_mode(view.keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
  if (options.overlay_layer != nullptr) {
    lv_obj_set_align(view.keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(view.keyboard, 0, metrics.height - keyboard_height);
    lv_obj_add_flag(view.keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(view.name_input, show_keyboard_cb, LV_EVENT_CLICKED, view.keyboard);
    if (view.password_input != nullptr)
      lv_obj_add_event_cb(view.password_input, show_keyboard_cb, LV_EVENT_CLICKED, view.keyboard);
    lv_obj_add_event_cb(view.keyboard, hide_keyboard_cb, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(view.keyboard, hide_keyboard_cb, LV_EVENT_CANCEL, nullptr);
  }
  lv_obj_set_style_radius(view.keyboard, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(view.keyboard, gui2_core::ui_px(72), LV_PART_MAIN);
  lv_keyboard_set_textarea(view.keyboard, view.name_input);
  lv_obj_set_style_text_font(view.keyboard, &lv_font_montserrat_48, LV_PART_ITEMS);
  if (options.keyboard_event_callback != nullptr)
    lv_obj_add_event_cb(view.keyboard, options.keyboard_event_callback, LV_EVENT_PRESSED, nullptr);

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
    lv_obj_remove_flag(view.password_block, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(view.password_block, LV_OBJ_FLAG_HIDDEN);
}

void show_backup_tab(const backup_page_view& view, size_t index) {
  lv_obj_t* panes[2] = { view.partitions_pane, view.options_pane };
  for (size_t i = 0; i < 2; ++i) {
    if (panes[i] == nullptr) continue;
    if (i == index)
      lv_obj_remove_flag(panes[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(panes[i], LV_OBJ_FLAG_HIDDEN);
  }
  if (index != 1 && view.keyboard != nullptr) lv_obj_add_flag(view.keyboard, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace gui2_pages
