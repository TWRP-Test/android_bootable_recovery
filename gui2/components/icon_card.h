#ifndef GUI2_COMPONENTS_ICON_CARD_H
#define GUI2_COMPONENTS_ICON_CARD_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

// The row the home page is built from: a coloured icon block, a title, and a
// chevron on the right for rows that lead somewhere.
struct icon_card_options {
  const gui2_core::ui_metrics* metrics = nullptr;
  const lv_image_dsc_t* icon = nullptr;
  uint32_t icon_color = 0;
  const char* title = nullptr;
  int width = 0;
  int height = 0;
  int icon_size = 0;
  // Drawing size of the artwork inside the coloured block. Zero keeps the
  // inset the home page uses; the reboot icons carry their own padding and
  // need more room than that.
  int art_size = 0;
  bool show_arrow = true;
  lv_event_cb_t event_callback = nullptr;
  const void* user_data = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

lv_obj_t* create_icon_card(lv_obj_t* parent, const icon_card_options& options);

// The gap the card keeps on both sides, which callers reuse to line other
// things up with it.
int icon_card_padding(const gui2_core::ui_metrics& metrics);

}  // namespace gui2_components

#endif  // GUI2_COMPONENTS_ICON_CARD_H
