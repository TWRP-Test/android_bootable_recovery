#pragma once

#include "lvgl.h"

// Rasterize a GUI2 SVG once and return a stable LVGL bitmap descriptor.
const lv_image_dsc_t* gui2_svg_get_raster(const lv_image_dsc_t* svg, int target_width,
                                          int target_height);

void gui2_svg_cache_clear();
