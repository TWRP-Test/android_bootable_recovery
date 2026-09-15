#include "shell/gui_shell_base.h"

#include <algorithm>

#include "core/ui_helpers.h"
#include "twrpminui/minui.h"

namespace gui2_shell {

using gui2_core::ui;
using gui2_core::ui_px;
using gui2_core::ui_scale_for;

gui_shell_base_view create_gui_shell_base(const gui_shell_base_options& options) {
  gui_shell_base_view view;
  if (options.screen == nullptr || options.text_font == nullptr || options.status_font == nullptr ||
      options.brand_font == nullptr)
    return view;

  const int width = gr_fb_width();
  const int height = gr_fb_height();
  const bool landscape = width > height;
  const lv_color_t background = lv_color_hex(0x000000);
  const lv_color_t card_color = lv_color_hex(0x252525);
  const lv_color_t nav_color = lv_color_hex(0x252525);
  const lv_color_t primary_text = lv_color_hex(0xFFFFFF);
  const lv_color_t secondary_text = lv_color_hex(0x898989);

  const float scale = ui_scale_for(width, height);
  ui.scale = scale;
  const int status_content_height = std::clamp(ui_px(96), ui_px(56), ui_px(96));
  const int status_top_padding = std::clamp(ui_px(24), ui_px(12), ui_px(24));
  const int status_height = status_content_height + status_top_padding;
  const int nav_height = std::clamp(ui_px(210), ui_px(154), ui_px(210));
  const int outer_margin = std::clamp(ui_px(56), ui_px(18), ui_px(56));
  const int card_gap = std::clamp(ui_px(22), ui_px(10), ui_px(22));
  const int content_width = width - outer_margin * 2;
  const int card_height = landscape ? std::clamp(height * 12 / 100, ui_px(82), ui_px(148))
                                    : std::clamp(width * 18 / 100, ui_px(104), ui_px(210));
  const int icon_size = std::clamp(card_height * 58 / 100, ui_px(48), ui_px(112));

  ui = {
    width,
    height,
    scale,
    status_height,
    status_top_padding,
    nav_height,
    outer_margin,
    card_gap,
    content_width,
    std::clamp(ui_px(112), ui_px(72), ui_px(112)),
    std::clamp(ui_px(24), ui_px(16), ui_px(24)),
    std::clamp(options.brand_font->line_height + options.status_font->line_height + ui_px(18),
               ui_px(120), ui_px(180)),
    card_height,
    icon_size,
    options.text_font,
    options.status_font,
    options.brand_font,
    background,
    card_color,
    nav_color,
    primary_text,
    secondary_text,
  };

  lv_obj_set_style_bg_color(options.screen, background, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(options.screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_font(options.screen, options.text_font, LV_PART_MAIN);
  lv_obj_set_style_pad_all(options.screen, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(options.screen);

  view.status = create_status_bar(options.screen, ui, options.recording_text,
                                  options.status_gesture_callback);

  view.page_layer = lv_obj_create(options.screen);
  lv_obj_set_pos(view.page_layer, 0, status_height);
  lv_obj_set_size(view.page_layer, width, std::max(1, height - status_height));
  gui2_core::set_surface_style(view.page_layer, background, LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.page_layer, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.page_layer);
  return view;
}

}  // namespace gui2_shell
