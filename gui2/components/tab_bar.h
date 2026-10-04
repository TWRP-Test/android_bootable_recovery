#ifndef GUI2_COMPONENTS_TAB_BAR_H
#define GUI2_COMPONENTS_TAB_BAR_H

#include <cstddef>

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_components {

// Segmented control: one pill per tab inside a rounded track, the active one
// filled. The caller owns the panes and only gets told which index is active.
class tab_bar {
 public:
  static constexpr size_t kMaxTabs = 4;

  using change_callback = void (*)(size_t index, void* user_data);

  lv_obj_t* create(lv_obj_t* parent, const gui2_core::ui_metrics& metrics, const char* const* labels,
                   size_t count, size_t active, change_callback callback, void* user_data);
  void detach();
  size_t active() const {
    return active_;
  }

 private:
  static void event_callback(lv_event_t* event);
  void select(size_t index);

  lv_obj_t* root_ = nullptr;
  lv_obj_t* pills_[kMaxTabs] = {};
  lv_obj_t* labels_[kMaxTabs] = {};
  size_t count_ = 0;
  size_t active_ = 0;
  change_callback callback_ = nullptr;
  void* user_data_ = nullptr;
};

}  // namespace gui2_components

#endif  // GUI2_COMPONENTS_TAB_BAR_H
