#ifndef GUI2_CORE_WHEEL_SCROLL_H
#define GUI2_CORE_WHEEL_SCROLL_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_core {

class wheel_scroll_controller {
 public:
  void initialize(const ui_metrics& metrics, lv_indev_t* pointer_indev);
  void queue(int amount, lv_obj_t* content, bool blocked);
  void advance(uint64_t now_ms, lv_obj_t* content, bool blocked);
  void reset();

 private:
  const ui_metrics* metrics_ = nullptr;
  lv_indev_t* pointer_indev_ = nullptr;
  float velocity_ = 0.0f;
  float remainder_ = 0.0f;
  uint64_t last_ms_ = 0;
};

}  // namespace gui2_core

#endif
