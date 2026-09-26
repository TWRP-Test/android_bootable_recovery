#ifndef GUI2_APP_GUI2_RUNTIME_H
#define GUI2_APP_GUI2_RUNTIME_H

#include "backend/console_backend.h"
#include "backend/file_manager_backend.h"
#include "backend/install_backend.h"
#include "backend/terminal_backend.h"
#include "backend/hardware_settings.h"
#include "backend/log_export_backend.h"
#include "backend/wipe_backend.h"
#include "backend/reboot_backend.h"
#include "backend/screen_backend.h"
#include "backend/settings_store.h"
#include "backend/startup_backend.h"

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
  gui2_backend::wipe_backend* wipe = nullptr;
  gui2_backend::decrypt_backend* decrypt = nullptr;
  gui2_backend::backup_backend* backup = nullptr;
  gui2_backend::mount_backend* mount = nullptr;
  gui2_backend::restore_backend* restore = nullptr;
  gui2_backend::terminal_backend* terminal = nullptr;
  gui2_backend::wifi_backend* wifi = nullptr;
  gui2_backend::file_manager_backend* file_manager = nullptr;
  gui2_backend::install_backend* install = nullptr;
  gui2_backend::startup_backend* startup = nullptr;
  bool switch_to_legacy = false;
  bool reboot_requested = false;
};

}  // namespace gui2_app

#endif
