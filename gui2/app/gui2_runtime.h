#ifndef GUI2_APP_GUI2_RUNTIME_H
#define GUI2_APP_GUI2_RUNTIME_H

#include "backend/console_backend.h"
#include "backend/hardware_settings.h"
#include "backend/log_export_backend.h"
#include "backend/reboot_backend.h"
#include "backend/screen_backend.h"
#include "backend/settings_store.h"

namespace gui2_app {

// Recovery services and process-level GUI2 state. LVGL object ownership stays
// with Shell/page modules; this object only groups injected dependencies and
// exit intent so callbacks do not each own independent globals.
struct runtime_state {
  gui2_backend::settings_store* settings = nullptr;
  gui2_backend::hardware_settings* hardware = nullptr;
  gui2_backend::screen_backend* screen = nullptr;
  gui2_backend::reboot_backend* reboot = nullptr;
  gui2_backend::console_backend* console = nullptr;
  gui2_backend::log_export_backend* log_export = nullptr;
  bool switch_to_legacy = false;
  bool reboot_requested = false;
};

}  // namespace gui2_app

#endif
