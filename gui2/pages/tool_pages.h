#ifndef GUI2_PAGES_TOOL_PAGES_H
#define GUI2_PAGES_TOOL_PAGES_H

#include <cstddef>
#include <string>

#include "backend/tools_backend.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

// The legacy partitionoptions page: what the partition is, and what can be
// done to it. The callback's user data is one of the three targets.
struct partition_options_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const gui2_backend::partition_details* details = nullptr;
  lv_event_cb_t action_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
  const void* repair_target = nullptr;
  const void* resize_target = nullptr;
  const void* change_target = nullptr;
};

void build_partition_options_page(const partition_options_page_options& options);

// selectfilesystem: one card per file system, marked the way the reboot page
// marks its target. The callback's user data points into choice_indices.
struct change_fs_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const gui2_backend::partition_details* details = nullptr;
  int selected = -1;
  const int* choice_indices = nullptr;
  lv_event_cb_t choice_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

void build_change_fs_page(const change_fs_page_options& options);

// What the change page shows for a changefilesystem value.
const char* file_system_label(const std::string& value);

enum class confirm_tone {
  INFO,
  WARNING,
  DANGER,
};

// The body of a confirm_action style page; the swipe goes on the page layer.
struct confirm_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const char* text = nullptr;
  confirm_tone tone = confirm_tone::INFO;
};

void build_confirm_page(const confirm_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_TOOL_PAGES_H
