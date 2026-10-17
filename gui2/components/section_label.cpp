#include "components/section_label.h"

lv_obj_t* gui2_components::create_section_label(lv_obj_t* parent,
                                                const gui2_core::ui_metrics& metrics,
                                                const char* text) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_obj_set_width(label, metrics.content_width);
  lv_obj_set_style_text_color(label, metrics.secondary_text, LV_PART_MAIN);
  lv_obj_set_style_text_font(label, metrics.status_font, LV_PART_MAIN);
  return label;
}
