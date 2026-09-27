#include "pages/file_manager_page.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "components/icon.h"
#include "gui2_svg_assets.h"
#include "core/ui_helpers.h"

namespace gui2_pages {

namespace {

constexpr uint32_t kAccent = 0x347FF1;

lv_obj_t* add_crumb(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, const char* text,
                    bool current, lv_event_cb_t callback, const void* target,
                    lv_event_cb_t press_guard) {
  const int height = gui2_core::single_line_card_height() * 3 / 5;
  lv_obj_t* pill = lv_obj_create(parent);
  lv_obj_set_height(pill, height);
  lv_obj_set_width(pill, LV_SIZE_CONTENT);
  lv_obj_set_clickable(pill, true);
  lv_obj_set_style_radius(pill, height / 2, LV_PART_MAIN);
  lv_obj_set_style_border_width(pill, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(pill, gui2_core::card_inner_padding() * 3 / 4, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(pill, 0, LV_PART_MAIN);
  gui2_core::set_surface_style(pill, current ? lv_color_hex(kAccent) : metrics.card_color);
  gui2_core::disable_scrolling(pill);
  if (press_guard != nullptr) lv_obj_add_event_cb(pill, press_guard, LV_EVENT_ALL, nullptr);
  if (callback != nullptr)
    lv_obj_add_event_cb(pill, callback, LV_EVENT_CLICKED, const_cast<void*>(target));

  lv_obj_t* label = lv_label_create(pill);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, current ? lv_color_hex(0xFFFFFF) : metrics.primary_text,
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.status_font, LV_PART_MAIN);
  lv_obj_center(label);
  return pill;
}

std::string human_size(uint64_t bytes) {
  static const char* const kUnits[] = { "B", "KB", "MB", "GB", "TB" };
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < std::size(kUnits)) {
    value /= 1024.0;
    ++unit;
  }
  char text[32];
  std::snprintf(text, sizeof(text), unit == 0 || value >= 10.0 ? "%.0f %s" : "%.1f %s", value,
                kUnits[unit]);
  return text;
}

lv_obj_t* create_icon(lv_obj_t* parent, const lv_image_dsc_t* asset, int size, lv_color_t tint,
                      lv_color_t surface) {
  lv_obj_t* holder = lv_obj_create(parent);
  lv_obj_set_size(holder, size, size);
  gui2_core::set_surface_style(holder, surface, LV_OPA_TRANSP);
  lv_obj_set_style_border_width(holder, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(holder, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(holder);
  lv_obj_remove_flag(holder, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* image = gui2_components::create_svg_image(holder, asset, size, size);
  if (image != nullptr) {
    lv_obj_center(image);
    lv_obj_set_style_image_recolor(image, tint, LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, LV_PART_MAIN);
  }
  return holder;
}

void set_opa(void* target, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t*>(target), static_cast<lv_opa_t>(value),
                       LV_PART_MAIN);
}

void set_translate_y(void* target, int32_t value) {
  lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(target), value, LV_PART_MAIN);
}

void animate(lv_obj_t* target, lv_anim_exec_xcb_t exec, int32_t from, int32_t to,
             uint32_t duration, lv_anim_completed_cb_t done = nullptr) {
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, target);
  lv_anim_set_exec_cb(&anim, exec);
  lv_anim_set_values(&anim, from, to);
  lv_anim_set_duration(&anim, duration);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
  lv_anim_set_early_apply(&anim, true);
  if (done != nullptr) lv_anim_set_completed_cb(&anim, done);
  lv_anim_start(&anim);
}

}  // namespace

file_manager_page_view build_file_manager_page(const file_manager_page_options& options) {
  file_manager_page_view view;
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;

  const auto& metrics = *options.metrics;

  view.body = lv_obj_create(options.content);
  lv_obj_set_pos(view.body, metrics.outer_margin, 0);
  lv_obj_set_width(view.body, metrics.content_width);
  lv_obj_set_height(view.body, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.body, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.body, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.body, metrics.cards_top_gap, LV_PART_MAIN);
  lv_obj_set_layout(view.body, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(view.body);

  // The trail scrolls sideways: a deep path is longer than the screen. It is
  // parented outside the body when the page wants it to stay put.
  const bool fixed_crumbs = options.crumb_parent != nullptr;
  view.crumbs = lv_obj_create(fixed_crumbs ? options.crumb_parent : view.body);
  if (fixed_crumbs) lv_obj_set_pos(view.crumbs, metrics.outer_margin, options.crumb_y);
  lv_obj_set_width(view.crumbs, metrics.content_width);
  lv_obj_set_height(view.crumbs, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.crumbs, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.crumbs, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(view.crumbs, metrics.cards_top_gap / 2, LV_PART_MAIN);
  lv_obj_set_style_border_width(view.crumbs, 0, LV_PART_MAIN);
  lv_obj_set_layout(view.crumbs, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.crumbs, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(view.crumbs, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(view.crumbs, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(view.crumbs, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t* last_crumb = nullptr;
  const int chevron = gui2_core::single_line_card_height() * 2 / 5;
  for (size_t i = 0; i < options.crumb_count; ++i) {
    const bool current = i + 1 == options.crumb_count;
    last_crumb = add_crumb(view.crumbs, metrics, options.crumbs[i], current,
                           options.crumb_callback,
                           options.crumb_indices == nullptr ? nullptr : &options.crumb_indices[i],
                           options.press_guard_callback);
    if (current) continue;

    // The separator is the project's own art: the text font is a TTF with no
    // LV_SYMBOL_* code points, so a label of LV_SYMBOL_RIGHT draws nothing.
    lv_obj_t* holder = lv_obj_create(view.crumbs);
    lv_obj_set_size(holder, chevron, chevron);
    gui2_core::set_surface_style(holder, metrics.background, LV_OPA_TRANSP);
    lv_obj_set_style_border_width(holder, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(holder, 0, LV_PART_MAIN);
    gui2_core::disable_scrolling(holder);
    lv_obj_t* arrow =
        gui2_components::create_svg_image(holder, &kGui2IconArrowRight, chevron, chevron);
    if (arrow != nullptr) {
      lv_obj_center(arrow);
      lv_obj_set_style_image_recolor(arrow, metrics.secondary_text, LV_PART_MAIN);
      lv_obj_set_style_image_recolor_opa(arrow, LV_OPA_COVER, LV_PART_MAIN);
    }
  }

  // Going one folder deeper puts the new crumb off the right edge; bring it
  // back into view so the trail always ends where the user just tapped.
  if (last_crumb != nullptr) {
    lv_obj_update_layout(view.crumbs);
    lv_obj_scroll_to_view(last_crumb, LV_ANIM_ON);
  }

  view.list = lv_obj_create(view.body);
  lv_obj_set_width(view.list, metrics.content_width);
  lv_obj_set_height(view.list, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.list, metrics.card_color);
  lv_obj_set_style_radius(view.list, gui2_core::single_line_card_height() / 4, LV_PART_MAIN);
  // The rows are square; without this the first and last one paint their
  // pressed background over the card's rounded corners.
  lv_obj_set_style_clip_corner(view.list, true, LV_PART_MAIN);
  lv_obj_set_style_border_width(view.list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.list, 0, LV_PART_MAIN);
  lv_obj_set_layout(view.list, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  gui2_core::disable_scrolling(view.list);

  const int row_height = gui2_core::single_line_card_height();
  const int icon_size = row_height * 5 / 9;

  const auto add_row = [&](const char* text, const lv_image_dsc_t* glyph_asset, lv_color_t tint,
                           lv_event_cb_t callback, const void* target) {
    lv_obj_t* row = lv_obj_create(view.list);
    lv_obj_set_size(row, metrics.content_width, row_height);
    lv_obj_set_clickable(row, true);
    gui2_core::set_surface_style(row, metrics.card_color, LV_OPA_TRANSP);
    lv_obj_set_style_bg_color(row, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 24),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, gui2_core::card_inner_padding(), LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, gui2_core::card_inner_padding() * 3 / 4, LV_PART_MAIN);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    gui2_core::disable_scrolling(row);
    if (options.press_guard_callback != nullptr)
      lv_obj_add_event_cb(row, options.press_guard_callback, LV_EVENT_ALL, nullptr);
    if (callback != nullptr)
      lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, const_cast<void*>(target));

    // The text font is a TTF without LVGL's symbol code points, so these have
    // to be the project's own art rather than LV_SYMBOL_*.
    lv_obj_t* glyph_holder = lv_obj_create(row);
    lv_obj_set_size(glyph_holder, icon_size, icon_size);
    gui2_core::set_surface_style(glyph_holder, metrics.card_color, LV_OPA_TRANSP);
    lv_obj_set_style_border_width(glyph_holder, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(glyph_holder, 0, LV_PART_MAIN);
    gui2_core::disable_scrolling(glyph_holder);
    lv_obj_t* glyph =
        gui2_components::create_svg_image(glyph_holder, glyph_asset, icon_size, icon_size);
    if (glyph != nullptr) {
      lv_obj_center(glyph);
      lv_obj_set_style_image_recolor(glyph, tint, LV_PART_MAIN);
      lv_obj_set_style_image_recolor_opa(glyph, LV_OPA_COVER, LV_PART_MAIN);
    }

    lv_obj_t* name = lv_label_create(row);
    lv_label_set_text(name, text == nullptr ? "" : text);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_style_text_color(name, metrics.primary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(name, metrics.text_font, LV_PART_MAIN);
    return row;
  };

  if (options.show_parent_row)
    add_row(options.parent_label, &kGui2IconBack, metrics.secondary_text,
            options.parent_callback, nullptr);
  for (size_t i = 0; i < options.entry_count; ++i) {
    const gui2_backend::file_entry& entry = options.entries[i];
    lv_obj_t* row =
        add_row(entry.name.c_str(), entry.directory ? &kGui2IconFolder : &kGui2IconFile,
                entry.directory ? lv_color_hex(kAccent) : metrics.primary_text,
                options.entry_indices == nullptr ? nullptr : options.entry_callback,
                options.entry_indices == nullptr ? nullptr : &options.entry_indices[i]);
    if (options.show_sizes && !entry.directory) {
      lv_obj_set_style_pad_right(row, gui2_core::card_inner_padding(), LV_PART_MAIN);
      lv_obj_t* size = lv_label_create(row);
      lv_label_set_text(size, human_size(entry.size).c_str());
      lv_obj_set_style_text_color(size, metrics.secondary_text, LV_PART_MAIN);
      lv_obj_set_style_text_font(size, metrics.status_font, LV_PART_MAIN);
    }
  }
  if (options.entry_count == 0 && options.empty_text != nullptr) {
    lv_obj_t* empty = lv_label_create(view.list);
    lv_label_set_text(empty, options.empty_text);
    lv_obj_set_width(empty, metrics.content_width);
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(empty, row_height / 3, LV_PART_MAIN);
    lv_obj_set_style_text_color(empty, metrics.secondary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, metrics.status_font, LV_PART_MAIN);
  }
  return view;
}

int crumb_bar_height(const gui2_core::ui_metrics& metrics) {
  return gui2_core::single_line_card_height() * 3 / 5 + metrics.cards_top_gap;
}

void animate_file_list(const file_manager_page_view& view,
                       const gui2_core::ui_metrics& metrics, bool deeper) {
  if (view.list == nullptr) return;

  const int from = deeper ? metrics.content_width : -metrics.content_width;
  lv_obj_set_x(view.list, from);
  lv_anim_t slide;
  lv_anim_init(&slide);
  lv_anim_set_var(&slide, view.list);
  lv_anim_set_exec_cb(&slide, [](void* target, int32_t value) {
    lv_obj_set_x(static_cast<lv_obj_t*>(target), value);
  });
  lv_anim_set_values(&slide, from, 0);
  lv_anim_set_duration(&slide, 180);
  lv_anim_set_path_cb(&slide, lv_anim_path_ease_out);
  lv_anim_start(&slide);

  lv_obj_set_style_opa(view.list, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_fade_in(view.list, 180, 0);
}

void fade_file_list(const file_manager_page_view& view) {
  if (view.list == nullptr) return;
  animate(view.list, set_opa, LV_OPA_TRANSP, LV_OPA_COVER, 160);
}

int file_toolbar_height(const gui2_core::ui_metrics& metrics) {
  return gui2_core::single_line_card_height() * 17 / 20 + metrics.cards_top_gap;
}

file_toolbar_view build_file_toolbar(const file_toolbar_options& options) {
  file_toolbar_view view;
  if (options.parent == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return view;
  const auto& metrics = *options.metrics;
  const int height = gui2_core::single_line_card_height() * 17 / 20;
  const int gap = metrics.cards_top_gap / 2;
  const int icon = height * 9 / 20;

  view.root = lv_obj_create(options.parent);
  lv_obj_set_pos(view.root, metrics.outer_margin, options.y);
  lv_obj_set_size(view.root, metrics.content_width, height);
  gui2_core::set_surface_style(view.root, metrics.background, LV_OPA_TRANSP);
  lv_obj_set_style_border_width(view.root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(view.root, gap, LV_PART_MAIN);
  lv_obj_set_layout(view.root, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.root, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(view.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  gui2_core::disable_scrolling(view.root);

  // The accent border fades in while the keyboard is up.
  static const lv_style_prop_t kProps[] = { LV_STYLE_BORDER_OPA, LV_STYLE_PROP_INV };
  static lv_style_transition_dsc_t transition;
  lv_style_transition_dsc_init(&transition, kProps, lv_anim_path_ease_out, 150, 0, nullptr);

  view.field = lv_obj_create(view.root);
  lv_obj_set_height(view.field, height);
  lv_obj_set_flex_grow(view.field, 1);
  gui2_core::set_surface_style(view.field, metrics.card_color);
  lv_obj_set_style_radius(view.field, height / 2, LV_PART_MAIN);
  lv_obj_set_style_border_width(view.field, gui2_core::ui_px(4), LV_PART_MAIN);
  lv_obj_set_style_border_color(view.field, lv_color_hex(kAccent), LV_PART_MAIN);
  lv_obj_set_style_border_opa(view.field, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_opa(view.field, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
  lv_obj_set_style_transition(view.field, &transition, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(view.field, height / 3, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(view.field, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(view.field, gap, LV_PART_MAIN);
  lv_obj_set_layout(view.field, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.field, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(view.field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  gui2_core::disable_scrolling(view.field);
  create_icon(view.field, &kGui2IconSearch, icon, metrics.secondary_text, metrics.card_color);

  view.input = lv_textarea_create(view.field);
  lv_textarea_set_one_line(view.input, true);
  lv_textarea_set_placeholder_text(view.input, options.strings->search_hint);
  lv_textarea_set_text(view.input, options.query == nullptr ? "" : options.query);
  lv_obj_set_height(view.input, height);
  lv_obj_set_flex_grow(view.input, 1);
  gui2_core::set_surface_style(view.input, metrics.card_color, LV_OPA_TRANSP);
  lv_obj_set_style_border_width(view.input, 0, LV_PART_MAIN);
  lv_obj_set_style_text_color(view.input, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(view.input, metrics.text_font, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.input, 0, LV_PART_MAIN);
  const int line = metrics.text_font != nullptr ? metrics.text_font->line_height : 0;
  lv_obj_set_style_pad_top(view.input, std::max(0, (height - line) / 2), LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(view.input, LV_SCROLLBAR_MODE_OFF);
  if (options.input_callback != nullptr)
    lv_obj_add_event_cb(view.input, options.input_callback, LV_EVENT_VALUE_CHANGED, nullptr);

  const int clear_size = height * 3 / 5;
  view.clear = lv_obj_create(view.field);
  lv_obj_set_size(view.clear, clear_size, clear_size);
  gui2_core::set_surface_style(view.clear, lv_color_mix(lv_color_hex(0xFFFFFF),
                                                        metrics.card_color, 40));
  lv_obj_set_style_radius(view.clear, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_width(view.clear, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.clear, 0, LV_PART_MAIN);
  lv_obj_set_ext_click_area(view.clear, gap);
  gui2_core::disable_scrolling(view.clear);
  lv_obj_set_clickable(view.clear, true);
  if (options.press_guard_callback != nullptr)
    lv_obj_add_event_cb(view.clear, options.press_guard_callback, LV_EVENT_ALL, nullptr);
  if (options.clear_callback != nullptr)
    lv_obj_add_event_cb(view.clear, options.clear_callback, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(create_icon(view.clear, &kGui2IconClose, clear_size * 3 / 5,
                            metrics.primary_text, metrics.card_color));
  show_file_toolbar_clear(view, options.query != nullptr && options.query[0] != '\0');

  if (options.sort_label != nullptr) {
    view.sort_button = lv_obj_create(view.root);
    lv_obj_set_size(view.sort_button, LV_SIZE_CONTENT, height);
    gui2_core::set_surface_style(view.sort_button, metrics.card_color);
    lv_obj_set_style_bg_color(view.sort_button,
                              lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 24),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(view.sort_button, height / 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(view.sort_button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(view.sort_button, height / 3, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(view.sort_button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(view.sort_button, gap, LV_PART_MAIN);
    lv_obj_set_layout(view.sort_button, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view.sort_button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(view.sort_button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    gui2_core::disable_scrolling(view.sort_button);
    lv_obj_set_clickable(view.sort_button, true);
    if (options.press_guard_callback != nullptr)
      lv_obj_add_event_cb(view.sort_button, options.press_guard_callback, LV_EVENT_ALL, nullptr);
    if (options.sort_callback != nullptr)
      lv_obj_add_event_cb(view.sort_button, options.sort_callback, LV_EVENT_CLICKED, nullptr);
    create_icon(view.sort_button, &kGui2IconSort, icon, metrics.primary_text, metrics.card_color);
    view.sort_label = lv_label_create(view.sort_button);
    lv_label_set_text(view.sort_label, options.sort_label);
    lv_obj_set_style_text_color(view.sort_label, metrics.primary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(view.sort_label, metrics.text_font, LV_PART_MAIN);
  }
  return view;
}

void set_file_toolbar_active(const file_toolbar_view& view, bool active) {
  if (view.field == nullptr) return;
  if (active)
    lv_obj_add_state(view.field, LV_STATE_CHECKED);
  else
    lv_obj_remove_state(view.field, LV_STATE_CHECKED);
}

void show_file_toolbar_clear(const file_toolbar_view& view, bool shown) {
  if (view.clear != nullptr) lv_obj_set_hidden(view.clear, !shown);
}

namespace {

void menu_closed(lv_anim_t* anim) {
  lv_obj_delete_async(static_cast<lv_obj_t*>(lv_anim_get_user_data(anim)));
}

void set_backdrop_opa(void* target, int32_t value) {
  lv_obj_set_style_bg_opa(static_cast<lv_obj_t*>(target), static_cast<lv_opa_t>(value),
                          LV_PART_MAIN);
  lv_obj_t* menu = lv_obj_get_child(static_cast<lv_obj_t*>(target), 0);
  if (menu == nullptr) return;
  // The menu follows the backdrop, fully shown once the backdrop reaches 90.
  lv_obj_set_style_opa(menu, static_cast<lv_opa_t>(value * 255 / 90), LV_PART_MAIN);
}

}  // namespace

lv_obj_t* open_sort_menu(const sort_menu_options& options) {
  if (options.anchor == nullptr || options.metrics == nullptr || options.labels == nullptr ||
      options.indices == nullptr)
    return nullptr;
  const auto& metrics = *options.metrics;

  lv_obj_t* backdrop = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(backdrop, 0, 0);
  lv_obj_set_size(backdrop, metrics.width, metrics.height);
  gui2_core::set_surface_style(backdrop, lv_color_hex(0x000000), LV_OPA_TRANSP);
  lv_obj_set_style_radius(backdrop, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(backdrop, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(backdrop, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(backdrop);
  lv_obj_set_clickable(backdrop, true);
  if (options.dismiss_callback != nullptr)
    lv_obj_add_event_cb(backdrop, options.dismiss_callback, LV_EVENT_CLICKED, nullptr);

  const int row_height = gui2_core::single_line_card_height() * 3 / 4;
  const int padding = gui2_core::card_inner_padding();
  const int width = metrics.content_width * 11 / 20;
  lv_obj_t* menu = lv_obj_create(backdrop);
  lv_obj_set_width(menu, width);
  lv_obj_set_height(menu, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(menu, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 10));
  lv_obj_set_style_radius(menu, gui2_core::single_line_card_height() / 4, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(menu, true, LV_PART_MAIN);
  lv_obj_set_style_border_width(menu, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(menu, gui2_core::ui_px(60), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(menu, LV_OPA_60, LV_PART_MAIN);
  lv_obj_set_style_pad_all(menu, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_ver(menu, gui2_core::ui_px(10), LV_PART_MAIN);
  lv_obj_set_layout(menu, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
  gui2_core::disable_scrolling(menu);
  lv_obj_set_clickable(menu, true);

  for (size_t i = 0; i < options.count; ++i) {
    if (i > 0 && options.group_size > 0 && i % options.group_size == 0) {
      lv_obj_t* line = lv_obj_create(menu);
      lv_obj_set_size(line, width, std::max(1, gui2_core::ui_px(2)));
      gui2_core::set_surface_style(line, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color,
                                                      22));
      lv_obj_set_style_radius(line, 0, LV_PART_MAIN);
      lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    }
    lv_obj_t* row = lv_obj_create(menu);
    lv_obj_set_size(row, width, row_height);
    gui2_core::set_surface_style(row, metrics.card_color, LV_OPA_TRANSP);
    lv_obj_set_style_bg_color(row, lv_color_mix(lv_color_hex(0xFFFFFF), metrics.card_color, 24),
                              LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, padding, LV_PART_MAIN);
    gui2_core::disable_scrolling(row);
    lv_obj_set_clickable(row, true);
    if (options.choice_callback != nullptr)
      lv_obj_add_event_cb(row, options.choice_callback, LV_EVENT_CLICKED,
                          const_cast<int*>(&options.indices[i]));
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, options.labels[i]);
    lv_obj_set_style_text_color(
        label, i == options.selected ? lv_color_hex(kAccent) : metrics.primary_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, metrics.status_font, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
  }

  lv_area_t anchor;
  lv_obj_get_coords(options.anchor, &anchor);
  lv_obj_update_layout(menu);
  lv_obj_set_pos(menu, anchor.x2 - width + 1, anchor.y2 + gui2_core::ui_px(12));

  animate(backdrop, set_backdrop_opa, 0, 90, 160);
  animate(menu, set_translate_y, -gui2_core::ui_px(24), 0, 160);
  return backdrop;
}

void close_sort_menu(lv_obj_t* backdrop, bool animate_out) {
  if (backdrop == nullptr) return;
  lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
  if (!animate_out) {
    lv_obj_delete(backdrop);
    return;
  }
  lv_anim_delete(backdrop, nullptr);
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, backdrop);
  lv_anim_set_user_data(&anim, backdrop);
  lv_anim_set_exec_cb(&anim, set_backdrop_opa);
  lv_anim_set_values(&anim, lv_obj_get_style_bg_opa(backdrop, LV_PART_MAIN), 0);
  lv_anim_set_duration(&anim, 120);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
  lv_anim_set_completed_cb(&anim, menu_closed);
  lv_anim_start(&anim);
}

}  // namespace gui2_pages
