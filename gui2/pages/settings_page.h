#ifndef GUI2_PAGES_SETTINGS_PAGE_H
#define GUI2_PAGES_SETTINGS_PAGE_H

#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"

namespace gui2_pages {

struct settings_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  lv_event_cb_t option_event_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
  const void* language_target = nullptr;
  const void* timezone_target = nullptr;
  const void* screen_target = nullptr;
  const void* haptics_target = nullptr;
  const void* recording_target = nullptr;
  const void* console_settings_target = nullptr;
  const void* legacy_target = nullptr;
  bool has_screen = false;
  bool has_haptics = false;
  bool has_recording = false;
};

void build_settings_page(const settings_page_options& options);

}  // namespace gui2_pages

#endif
