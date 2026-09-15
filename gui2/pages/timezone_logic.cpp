#include "pages/timezone_logic.h"

#include <string>

namespace gui2_pages {

void set_choice_style(lv_obj_t* object, bool selected, lv_color_t card_color) {
  if (object == nullptr) return;
  const lv_color_t color = selected ? lv_color_hex(0x347FF1) : card_color;
  lv_obj_set_style_bg_color(object, color, LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, lv_color_mix(lv_color_hex(0xFFFFFF), color, 18),
                            LV_STATE_PRESSED);
}

void refresh_time_choices(lv_obj_t* const* format_cards, bool military_time,
                          lv_obj_t* const* timezone_cards, int timezone_index,
                          lv_obj_t* const* offset_cards, int offset_index,
                          lv_obj_t* dst_card, bool use_dst, lv_color_t card_color) {
  for (int i = 0; i < 2; ++i)
    set_choice_style(format_cards[i], (i == 1) == military_time, card_color);
  for (int i = 0; i < 24; ++i)
    set_choice_style(timezone_cards[i], i == timezone_index, card_color);
  for (int i = 0; i < 4; ++i)
    set_choice_style(offset_cards[i], i == offset_index, card_color);
  set_choice_style(dst_card, use_dst, card_color);
}

std::string build_timezone_value(const char* const* timezone_values,
                                 const char* const* timezone_offsets, int timezone_index,
                                 int offset_index, bool use_dst) {
  std::string value = timezone_values[timezone_index];
  const size_t separator = value.find(';');
  const std::string zone = value.substr(0, separator);
  const std::string dst_zone =
      separator == std::string::npos ? std::string() : value.substr(separator + 1);
  value = zone;
  if (offset_index != 0) value += ":" + std::string(timezone_offsets[offset_index]);
  if (use_dst) value += dst_zone;
  return value;
}

}  // namespace gui2_pages
