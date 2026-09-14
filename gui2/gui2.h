#ifndef GUI2_H
#define GUI2_H

#include "backend/hardware_settings.h"
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
  // Reuse an already initialized minui display when possible.
  bool display_initialized = false;
};

// Starts GUI2 and owns its display, input, and event loop.
int gui2_start(const gui2_context* context);

#endif
