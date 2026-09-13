#ifndef GUI2_H
#define GUI2_H

#include "backend/hardware_settings.h"
#include "backend/settings_store.h"

enum gui2_exit_reason {
  GUI2_EXIT_INITIALIZATION_FAILED = 1,
  GUI2_EXIT_TO_LEGACY = 2,
};

struct gui2_context {
  gui2_backend::settings_store* settings = nullptr;
  gui2_backend::hardware_settings* hardware = nullptr;
};

// Start the LVGL-based recovery interface. The caller must provide the
// recovery settings and hardware backends. This function owns the display,
// input and event loop and returns only if initialization fails or the GUI is
// explicitly stopped in the future.
int gui2_start(const gui2_context* context);

#endif
