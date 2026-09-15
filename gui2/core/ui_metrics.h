#ifndef GUI2_CORE_UI_METRICS_H
#define GUI2_CORE_UI_METRICS_H

#include "lvgl.h"

namespace gui2_core {

struct ui_metrics {
  int width = 0;
  int height = 0;
  float scale = 1.0f;
  int status_height = 0;
  int status_top_padding = 0;
  int nav_height = 0;
  int outer_margin = 0;
  int card_gap = 0;
  int content_width = 0;
  int heading_top = 0;
  int cards_top_gap = 0;
  int heading_height = 0;
  int card_height = 0;
  int icon_size = 0;
  const lv_font_t* text_font = nullptr;
  const lv_font_t* status_font = nullptr;
  const lv_font_t* brand_font = nullptr;
  lv_color_t background{};
  lv_color_t card_color{};
  lv_color_t nav_color{};
  lv_color_t primary_text{};
  lv_color_t secondary_text{};
};

extern ui_metrics ui;

int ui_px(float value);
int navigation_safe_area();
int card_inner_padding();
int single_line_card_height();
float ui_scale_for(int width, int height);

}  // namespace gui2_core

#endif
