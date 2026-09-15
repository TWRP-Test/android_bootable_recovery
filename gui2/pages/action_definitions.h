#ifndef GUI2_PAGES_ACTION_DEFINITIONS_H
#define GUI2_PAGES_ACTION_DEFINITIONS_H

#include <cstddef>

#include "lvgl.h"

namespace gui2_pages {

enum class action_id {
  INSTALL,
  WIPE,
  BACKUP,
  RESTORE,
  MOUNT,
  ADVANCED,
  SETTINGS,
};

struct action_definition {
  action_id id;
  const lv_image_dsc_t* icon;
  uint32_t color;
};

const action_definition* action_definitions();
size_t action_definition_count();

}  // namespace gui2_pages

#endif
