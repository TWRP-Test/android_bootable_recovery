#ifndef GUI2_CORE_UI_METRICS_H
#define GUI2_CORE_UI_METRICS_H

#include "lvgl.h"

namespace gui2_core {

// What a color is for; dark and light each fill the same set.
struct ui_palette {
  bool dark = true;
  lv_color_t background{};
  lv_color_t card_color{};
  lv_color_t nav_color{};
  lv_color_t primary_text{};
  lv_color_t secondary_text{};
  // Mixed into a surface for its pressed and raised states.
  lv_color_t tint{};
  lv_color_t scrim{};
  lv_color_t outline{};
  lv_color_t accent{};
  lv_color_t on_accent{};
  lv_color_t accent_soft{};
  lv_color_t accent_surface{};
  lv_color_t danger{};
  lv_color_t danger_soft{};
  lv_color_t danger_surface{};
  lv_color_t danger_surface_soft{};
  lv_color_t warning{};
  lv_color_t warning_surface{};
  lv_color_t success{};
  lv_color_t control_track{};
  lv_color_t control_text{};
  lv_color_t idle_dot{};
  lv_color_t key_panel{};
  lv_color_t key_fill{};
  lv_color_t key_pressed{};
  lv_color_t key_subdued{};
};

struct ui_metrics : ui_palette {
  int width = 0;
  int height = 0;
  float scale = 1.0f;
  bool large_screen = false;
  int status_height = 0;
  int status_top_padding = 0;
  int nav_height = 0;
  int outer_margin = 0;
  int card_gap = 0;
  int content_width = 0;
  // Where content_width starts; wide screens centre it.
  int content_left = 0;
  int heading_top = 0;
  int cards_top_gap = 0;
  int heading_height = 0;
  int card_height = 0;
  int icon_size = 0;
  const lv_font_t* text_font = nullptr;
  const lv_font_t* status_font = nullptr;
  const lv_font_t* brand_font = nullptr;
  const lv_font_t* keyboard_font = nullptr;
};

extern ui_metrics ui;

int ui_px(float value);
int navigation_safe_area();
int card_inner_padding();
int single_line_card_height();
// Columns a card grid splits content_width into, and the width of one.
int grid_columns();
int grid_column_width();
// A panel at least this wide on its short side, in millimetres, is laid out as a tablet.
bool is_large_screen(int short_side_mm);
// short_side_mm is 0 when the panel size is unknown.
float ui_scale_for(int width, int height, int short_side_mm);
ui_palette palette(bool dark);
// base with `amount`/255 of the palette's tint mixed in.
lv_color_t tinted(lv_color_t base, uint8_t amount);

}  // namespace gui2_core

#endif
