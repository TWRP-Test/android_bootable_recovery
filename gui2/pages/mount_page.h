#ifndef GUI2_PAGES_MOUNT_PAGE_H
#define GUI2_PAGES_MOUNT_PAGE_H

#include <cstddef>

#include "backend/mount_backend.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct mount_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const gui2_backend::mount_target* targets = nullptr;
  size_t target_count = 0;
  const int* target_indices = nullptr;
  lv_event_cb_t mount_callback = nullptr;
  // Current storage, shown the way the legacy header shows it.
  const char* storage_name = nullptr;
  const char* storage_free = nullptr;
  lv_event_cb_t storage_callback = nullptr;
  const void* storage_target = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
  // Only offered while data is still locked.
  bool has_decrypt = false;
  lv_event_cb_t decrypt_callback = nullptr;
  const void* decrypt_target = nullptr;
  bool mtp_enabled = false;
  const int* mtp_target = nullptr;
  bool has_usb_storage = false;
  bool usb_storage_enabled = false;
  const int* usb_storage_target = nullptr;
  lv_event_cb_t toggle_callback = nullptr;
  bool has_system = false;
  bool system_writable = false;
  const int* system_target = nullptr;
  lv_event_cb_t system_callback = nullptr;
};

void build_mount_page(const mount_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_MOUNT_PAGE_H
