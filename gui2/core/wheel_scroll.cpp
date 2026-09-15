#include "core/wheel_scroll.h"

#include <algorithm>
#include <cmath>
#include <time.h>

namespace gui2_core {

void wheel_scroll_controller::initialize(const ui_metrics& metrics, lv_indev_t* pointer_indev) {
  metrics_ = &metrics;
  pointer_indev_ = pointer_indev;
  reset();
}

void wheel_scroll_controller::reset() {
  velocity_ = 0.0f;
  remainder_ = 0.0f;
  last_ms_ = 0;
}

void wheel_scroll_controller::queue(int amount, lv_obj_t* content, bool blocked) {
  if (amount == 0 || content == nullptr || blocked || metrics_ == nullptr) return;
  const float impulse = static_cast<float>(ui_px(2100));
  velocity_ = std::clamp(velocity_ + amount * impulse, -static_cast<float>(ui_px(4200)),
                          static_cast<float>(ui_px(4200)));
  if (last_ms_ == 0) {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    last_ms_ = static_cast<uint64_t>(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
  }
}

void wheel_scroll_controller::advance(uint64_t now_ms, lv_obj_t* content, bool blocked) {
  if (content == nullptr || blocked || pointer_indev_ == nullptr || metrics_ == nullptr ||
      lv_indev_get_state(pointer_indev_) == LV_INDEV_STATE_PRESSED) {
    reset();
    return;
  }
  if (velocity_ == 0.0f) {
    last_ms_ = now_ms;
    return;
  }
  if (last_ms_ == 0) last_ms_ = now_ms;
  const uint64_t elapsed_ms = std::min<uint64_t>(now_ms - last_ms_, 50);
  last_ms_ = now_ms;
  if (elapsed_ms == 0) return;

  const float elapsed_s = static_cast<float>(elapsed_ms) / 1000.0f;
  const float distance = velocity_ * elapsed_s + remainder_;
  const int pixels = static_cast<int>(std::lround(distance));
  remainder_ = distance - pixels;
  if (pixels != 0) lv_obj_scroll_by_bounded(content, 0, pixels, LV_ANIM_OFF);
  velocity_ *= std::exp(-8.0f * elapsed_s);
  if (std::abs(velocity_) < static_cast<float>(ui_px(8))) reset();
}

}  // namespace gui2_core
