#include "pages/action_definitions.h"

#include "gui2_svg_assets.h"

namespace gui2_pages {

const action_definition* action_definitions() {
  static const action_definition actions[] = {
      { action_id::INSTALL, &kGui2IconInstall, 0x347FF1 },
      { action_id::WIPE, &kGui2IconWipe, 0xF0443E },
      { action_id::BACKUP, &kGui2IconBackup, 0xFFAA20 },
      { action_id::RESTORE, &kGui2IconRestore, 0x18C935 },
      { action_id::MOUNT, &kGui2IconMount, 0x6754E8 },
      { action_id::ADVANCED, &kGui2IconAdvanced, 0x9AA7B0 },
      { action_id::SETTINGS, &kGui2IconSettings, 0x405A6C },
  };
  return actions;
}

size_t action_definition_count() {
  return 7;
}

}  // namespace gui2_pages
