#include "shell/page_scaffold.h"

#include <algorithm>

#include "core/ui_helpers.h"

namespace gui2_shell {

namespace {

constexpr const char* kVersionText = "4.0.0";

}  // namespace

page_scaffold_result build_page_scaffold(lv_obj_t* page_layer, const gui2_core::ui_metrics& metrics,
                                         const char* title, const char* summary,
                                         int bottom_reserved) {
  page_scaffold_result result;
  if (page_layer == nullptr) return result;

  result.heading = lv_obj_create(page_layer);
  lv_obj_set_pos(result.heading, metrics.outer_margin, metrics.heading_top);
  lv_obj_set_size(result.heading, metrics.content_width, metrics.heading_height);
  gui2_core::set_surface_style(result.heading, metrics.background, LV_OPA_TRANSP);
  const int heading_pad_left = gui2_core::ui_px(10);
  lv_obj_set_style_pad_all(result.heading, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(result.heading, heading_pad_left, LV_PART_MAIN);
  gui2_core::disable_scrolling(result.heading);

  lv_obj_t* title_label = lv_label_create(result.heading);
  lv_label_set_text(title_label, title);
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, gui2_core::ui_px(2));
  lv_obj_set_style_text_color(title_label, metrics.primary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(title_label, metrics.brand_font, LV_PART_MAIN);

  lv_obj_t* version = lv_label_create(result.heading);
  lv_label_set_text(version, kVersionText);
  lv_label_set_long_mode(version, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_color(version, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(version, metrics.status_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
  {
    const int32_t letter_space = lv_obj_get_style_text_letter_space(version, LV_PART_MAIN);
    const int32_t line_space = lv_obj_get_style_text_line_space(version, LV_PART_MAIN);
    lv_point_t size;
    lv_text_get_size(&size, kVersionText, metrics.status_font, letter_space, line_space,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int box_width = size.x + gui2_core::ui_px(24);
    const int usable_width = metrics.content_width - heading_pad_left;
    lv_obj_set_size(version, box_width, size.y);
    lv_obj_set_pos(version, std::max(0, usable_width - box_width - gui2_core::ui_px(4)),
                   gui2_core::ui_px(10));
  }

  result.summary = lv_label_create(result.heading);
  lv_label_set_text(result.summary, summary);
  lv_obj_align(result.summary, LV_ALIGN_BOTTOM_LEFT, 0, -gui2_core::ui_px(6));
  lv_obj_set_style_text_color(result.summary, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(result.summary, metrics.status_font, LV_PART_MAIN);

  const int scroll_top = metrics.heading_top + metrics.heading_height + metrics.cards_top_gap;
  result.content = lv_obj_create(page_layer);
  lv_obj_set_pos(result.content, 0, scroll_top);
  // The reserve used to shorten the box, which cut the list off in a hard line
  // above whatever floats at the bottom. Keep the box full height and spend the
  // reserve as scroll room instead, so the content slides under the control and
  // fades out behind the gradient.
  lv_obj_set_size(result.content, metrics.width,
                  std::max(1, metrics.height - metrics.status_height - scroll_top));
  gui2_core::set_surface_style(result.content, metrics.background);
  lv_obj_set_scrollable(result.content, true);
  lv_obj_set_scroll_dir(result.content, LV_DIR_VER);
  gui2_core::style_scrollbar(result.content, metrics.secondary_text);
  lv_obj_set_scroll_elastic(result.content, true);
  lv_obj_set_style_pad_all(result.content, 0, LV_PART_MAIN);
  // After pad_all, which would otherwise wipe it. One card gap past the
  // control, or the last row stops flush against it and still reads as cut off.
  lv_obj_set_style_pad_bottom(result.content,
                              bottom_reserved > 0 ? bottom_reserved + metrics.cards_top_gap : 0,
                              LV_PART_MAIN);
  return result;
}

}  // namespace gui2_shell
