#ifndef GUI2_H
#define GUI2_H

#include "backend/console_backend.h"
#include "backend/backup_backend.h"
#include "backend/decrypt_backend.h"
#include "backend/mount_backend.h"
#include "backend/hardware_settings.h"
#include "backend/log_export_backend.h"
#include "backend/wipe_backend.h"
#include "backend/reboot_backend.h"
#include "backend/restore_backend.h"
#include "backend/file_manager_backend.h"
#include "backend/install_backend.h"
#include "backend/terminal_backend.h"
#include "backend/wifi_backend.h"
#include "backend/screen_backend.h"
#include "backend/settings_store.h"
#include "backend/sideload_backend.h"
#include "backend/startup_backend.h"

enum gui2_exit_reason {
  GUI2_EXIT_INITIALIZATION_FAILED = 1,
  GUI2_EXIT_TO_LEGACY = 2,
  GUI2_EXIT_STARTUP_FAILED = 3,
};

struct gui2_context {
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
  // Left null when Wi-Fi is not part of the build; every page that would
  // show it checks the pointer instead of a build flag.
  gui2_backend::wifi_backend* wifi = nullptr;
  gui2_backend::file_manager_backend* file_manager = nullptr;
  gui2_backend::install_backend* install = nullptr;
  gui2_backend::sideload_backend* sideload = nullptr;
  // Set: show the splash while startup runs, build the pages after.
  gui2_backend::startup_backend* startup = nullptr;
  // fastbootd: the pages are the fastboot page and reboot, nothing else.
  bool fastboot_mode = false;
  // Reuse an already initialized minui display when possible.
  bool display_initialized = false;
};

// Starts GUI2 and owns its display, input, and event loop.
int gui2_start(const gui2_context* context);

#endif
