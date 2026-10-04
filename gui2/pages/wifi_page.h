#ifndef GUI2_PAGES_WIFI_PAGE_H
#define GUI2_PAGES_WIFI_PAGE_H

#include <cstddef>

#include "backend/wifi_backend.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "pages/console_page.h"

namespace gui2_pages {

// The legacy WLAN page as cards: the service switch, the three actions it has
// as buttons, the networks the last scan found, and its log box underneath.
struct wifi_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const lv_font_t* log_font = nullptr;

  bool service_running = false;
  lv_event_cb_t service_callback = nullptr;

  const gui2_backend::wifi_network* networks = nullptr;
  size_t network_count = 0;
  const int* network_indices = nullptr;
  lv_event_cb_t network_callback = nullptr;

  // Empty unless the supplicant says it is associated.
  const char* connected_ssid = nullptr;
  // Shown in place of the list while a scan or a connection is running.
  const char* busy_text = nullptr;
  // Shown above the list when the last connection attempt failed.
  const char* failed_text = nullptr;

  lv_event_cb_t scan_callback = nullptr;
  lv_event_cb_t status_callback = nullptr;
  lv_event_cb_t test_callback = nullptr;
  lv_event_cb_t press_guard_callback = nullptr;
};

struct wifi_page_view {
  lv_obj_t* service_toggle = nullptr;
  // The log box; the page loop appends to it as the jobs report.
  console_page_view log;
};

wifi_page_view build_wifi_page(const wifi_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_WIFI_PAGE_H
