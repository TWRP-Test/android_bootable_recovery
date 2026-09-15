#include "core/ui_metrics.h"

#include <algorithm>
#include <cmath>

namespace gui2_core {

ui_metrics ui;

int ui_px(float value) {
  return std::max(1, static_cast<int>(std::lround(value * ui.scale)));
}

int navigation_safe_area() {
  return ui.nav_height + ui.outer_margin;
}

int card_inner_padding() {
  return std::clamp(ui.outer_margin, ui_px(32), ui_px(56));
}

int single_line_card_height() {
  return std::clamp(ui.card_height * 2 / 3, ui_px(96), ui_px(148));
}

float ui_scale_for(int width, int height) {
  constexpr float reference_short_side = 1200.0f;
  return std::clamp(std::min(width, height) / reference_short_side, 0.72f, 2.0f);
}

}  // namespace gui2_core
