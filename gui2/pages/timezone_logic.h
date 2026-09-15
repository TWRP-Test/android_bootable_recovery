#ifndef GUI2_PAGES_TIMEZONE_LOGIC_H
#define GUI2_PAGES_TIMEZONE_LOGIC_H

#include <string>

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_pages {

void set_choice_style(lv_obj_t* object, bool selected, lv_color_t card_color);
void refresh_time_choices(lv_obj_t* const* format_cards, bool military_time,
                          lv_obj_t* const* timezone_cards, int timezone_index,
                          lv_obj_t* const* offset_cards, int offset_index,
                          lv_obj_t* dst_card, bool use_dst, lv_color_t card_color);
std::string build_timezone_value(const char* const* timezone_values,
                                 const char* const* timezone_offsets, int timezone_index,
                                 int offset_index, bool use_dst);

}  // namespace gui2_pages

#endif
