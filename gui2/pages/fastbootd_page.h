#ifndef GUI2_PAGES_FASTBOOTD_PAGE_H
#define GUI2_PAGES_FASTBOOTD_PAGE_H

#include "components/tab_bar.h"
#include "core/ui_metrics.h"
#include "i18n/i18n.h"
#include "lvgl.h"
#include "pages/console_page.h"

namespace gui2_pages {

// The legacy fastboot page: the USB mode switch and the console under it.
struct fastbootd_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const gui2_i18n::language_pack* strings = nullptr;
  const lv_font_t* console_font = nullptr;
  gui2_components::tab_bar* usb_tabs = nullptr;
  bool usb_fastboot = true;
  // Tab 0 is fastboot, tab 1 is adb.
  gui2_components::tab_bar::change_callback usb_callback = nullptr;
};

struct fastbootd_page_view {
  console_page_view console;
};

fastbootd_page_view build_fastbootd_page(const fastbootd_page_options& options);

}  // namespace gui2_pages

#endif  // GUI2_PAGES_FASTBOOTD_PAGE_H
