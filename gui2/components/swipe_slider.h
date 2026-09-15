#ifndef GUI2_COMPONENTS_SWIPE_SLIDER_H
#define GUI2_COMPONENTS_SWIPE_SLIDER_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

using swipe_complete_callback = void (*)(void* user_data);

// Reusable slide-to-action control. The text and completion callback are
// supplied by the caller so the same widget can serve unlock, flash, and
// other confirmation flows.
class swipe_slider {
 public:
  lv_obj_t* create(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, int x, int y, int width,
                   int height, const char* text, swipe_complete_callback callback, void* user_data);
  void reset();
  int progress() const {
    return progress_;
  }

 private:
  static void event_callback(lv_event_t* event);
  void update_from_point(lv_point_t point);
  void finish_drag(bool cancelled);
  void set_progress(int progress);

  lv_obj_t* track_ = nullptr;
  lv_obj_t* fill_ = nullptr;
  lv_obj_t* knob_ = nullptr;
  lv_obj_t* prompt_ = nullptr;
  int inner_margin_ = 0;
  int knob_width_ = 0;
  int prompt_inset_ = 0;
  int progress_ = 0;
  bool dragging_ = false;
  swipe_complete_callback callback_ = nullptr;
  void* user_data_ = nullptr;
};

}  // namespace gui2_components

#endif
