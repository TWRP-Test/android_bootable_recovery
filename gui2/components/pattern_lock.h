#ifndef GUI2_COMPONENTS_PATTERN_LOCK_H
#define GUI2_COMPONENTS_PATTERN_LOCK_H

#include <string>

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

using pattern_complete_callback = void (*)(const std::string& passphrase, void* user_data);
using pattern_dot_callback = void (*)(void* user_data);

// 3x3 unlock grid. The passphrase it produces matches what Android derives from
// the same gesture: each visited dot index becomes '1' + index, concatenated in
// visit order, with dots crossed on the way added automatically.
class pattern_lock {
 public:
  lv_obj_t* create(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, int x, int y, int size,
                   pattern_complete_callback callback, void* user_data,
                   pattern_dot_callback dot_callback = nullptr);
  void reset();
  void detach();

 private:
  static constexpr int kGrid = 3;
  static constexpr int kDots = kGrid * kGrid;

  static void event_callback(lv_event_t* event);
  bool local_point(lv_point_t* out) const;
  int dot_at(const lv_point_t& point) const;
  bool used(int index) const;
  void connect(int index);
  void connect_crossed(int index);
  void refresh_path(const lv_point_t* live);
  void finish();

  lv_point_t center_of(int index) const;

  lv_obj_t* root_ = nullptr;
  lv_obj_t* dots_[kDots] = {};
  lv_obj_t* path_ = nullptr;
  lv_point_precise_t points_[kDots + 1] = {};
  int order_[kDots] = {};
  int count_ = 0;
  int size_ = 0;
  int cell_ = 0;
  int dot_size_ = 0;
  bool dragging_ = false;
  pattern_complete_callback callback_ = nullptr;
  pattern_dot_callback dot_callback_ = nullptr;
  void* user_data_ = nullptr;
};

}  // namespace gui2_components

#endif  // GUI2_COMPONENTS_PATTERN_LOCK_H
