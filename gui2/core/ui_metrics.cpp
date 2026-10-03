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

int grid_columns() {
  return ui.large_screen && ui.content_width >= ui_px(1900) ? 2 : 1;
}

int grid_column_width() {
  const int columns = grid_columns();
  return (ui.content_width - ui.card_gap * (columns - 1)) / columns;
}

ui_palette palette(bool dark) {
  ui_palette p;
  p.dark = dark;
  if (dark) {
    p.background = lv_color_hex(0x000000);
    p.card_color = lv_color_hex(0x252525);
    p.nav_color = lv_color_hex(0x252525);
    p.primary_text = lv_color_hex(0xFFFFFF);
    p.secondary_text = lv_color_hex(0x898989);
    p.tint = lv_color_hex(0xFFFFFF);
    p.outline = lv_color_hex(0x3B3B3B);
    p.accent_soft = lv_color_hex(0x9BC5E9);
    p.accent_surface = lv_color_hex(0x0E1B2E);
    p.danger_soft = lv_color_hex(0xFF8A80);
    p.danger_surface = lv_color_hex(0x2A1010);
    p.danger_surface_soft = lv_color_hex(0x2E1414);
    p.warning = lv_color_hex(0xFFC46B);
    p.warning_surface = lv_color_hex(0x2E2412);
    p.control_track = lv_color_hex(0x454545);
    p.control_text = lv_color_hex(0xBDBDBD);
    p.idle_dot = lv_color_hex(0x6A6A6A);
    p.key_panel = lv_color_hex(0x141414);
    p.key_fill = lv_color_hex(0x3C3C3C);
    p.key_pressed = lv_color_hex(0x585858);
    p.key_subdued = lv_color_hex(0x2E2E2E);
  } else {
    p.background = lv_color_hex(0xF2F2F7);
    p.card_color = lv_color_hex(0xFFFFFF);
    p.nav_color = lv_color_hex(0xFFFFFF);
    p.primary_text = lv_color_hex(0x1C1C1E);
    p.secondary_text = lv_color_hex(0x6E6E73);
    p.tint = lv_color_hex(0x000000);
    p.outline = lv_color_hex(0xE5E5EA);
    p.accent_soft = lv_color_hex(0x1F5FBF);
    p.accent_surface = lv_color_hex(0xE3EDFD);
    p.danger_soft = lv_color_hex(0xC4302B);
    p.danger_surface = lv_color_hex(0xFDE7E6);
    p.danger_surface_soft = lv_color_hex(0xFDECEB);
    p.warning = lv_color_hex(0xA86A00);
    p.warning_surface = lv_color_hex(0xFFF3DC);
    p.control_track = lv_color_hex(0xD1D1D6);
    p.control_text = lv_color_hex(0x6E6E73);
    p.idle_dot = lv_color_hex(0xAEAEB2);
    p.key_panel = lv_color_hex(0xD3D6DC);
    p.key_fill = lv_color_hex(0xFFFFFF);
    p.key_pressed = lv_color_hex(0xB9BEC6);
    p.key_subdued = lv_color_hex(0xADB3BD);
  }
  p.scrim = lv_color_hex(0x000000);
  p.accent = lv_color_hex(0x347FF1);
  p.on_accent = lv_color_hex(0xFFFFFF);
  p.danger = lv_color_hex(0xF0443E);
  p.success = lv_color_hex(0x18C935);
  return p;
}

lv_color_t tinted(lv_color_t base, uint8_t amount) {
  return lv_color_mix(ui.tint, base, amount);
}

bool is_large_screen(int short_side_mm) {
  return short_side_mm >= 100;
}

float ui_scale_for(int width, int height, int short_side_mm) {
  const int short_side = std::min(width, height);
  if (is_large_screen(short_side_mm)) {
    // Sized by the millimetre against a 1200 px, 70 mm phone, and drawn a fifth larger.
    constexpr float reference_px_per_mm = 1200.0f / 70.0f;
    return std::clamp(short_side * 1.2f / (short_side_mm * reference_px_per_mm), 0.5f, 2.0f);
  }
  constexpr float reference_short_side = 1200.0f;
  return std::clamp(short_side / reference_short_side, 0.72f, 2.0f);
}

}  // namespace gui2_core
