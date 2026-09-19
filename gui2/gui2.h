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
#include "backend/screen_backend.h"
#include "backend/settings_store.h"

enum gui2_exit_reason {
  GUI2_EXIT_INITIALIZATION_FAILED = 1,
  GUI2_EXIT_TO_LEGACY = 2,
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
  // Reuse an already initialized minui display when possible.
  bool display_initialized = false;
};

// Starts GUI2 and owns its display, input, and event loop.
int gui2_start(const gui2_context* context);

#endif
