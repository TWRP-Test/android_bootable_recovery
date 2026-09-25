#include "pages/file_manager_page.h"

#include <algorithm>

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
  };

  if (options.show_parent_row)
    add_row(options.parent_label, &kGui2IconBack, metrics.secondary_text,
            options.parent_callback, nullptr);
  for (size_t i = 0; i < options.entry_count; ++i) {
    const gui2_backend::file_entry& entry = options.entries[i];
    add_row(entry.name.c_str(), entry.directory ? &kGui2IconFolder : &kGui2IconFile,
            entry.directory ? lv_color_hex(kAccent) : metrics.primary_text,
            options.entry_indices == nullptr ? nullptr : options.entry_callback,
            options.entry_indices == nullptr ? nullptr : &options.entry_indices[i]);
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

}  // namespace gui2_pages
