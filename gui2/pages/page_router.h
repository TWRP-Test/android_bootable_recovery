#ifndef GUI2_PAGES_PAGE_ROUTER_H
#define GUI2_PAGES_PAGE_ROUTER_H

#include "core/page_transition.h"

namespace gui2_pages {

enum class page_id {
  HOME,
  ACTION,
  REBOOT,
  LANGUAGE,
  TIMEZONE,
  BRIGHTNESS,
  HAPTICS,
  RECORDING,
  CONSOLE,
  CONSOLE_SETTINGS,
  EXPORT_LOG,
  WIPE,
  ADVANCED_WIPE,
  FORMAT_DATA,
  WIPE_PROGRESS,
  DECRYPT,
  DECRYPT_PROGRESS,
  BACKUP,
  BACKUP_PROGRESS,
  MOUNT,
};

struct page_request {
  page_id id;
  const void* payload = nullptr;
  gui2_core::page_transition transition = gui2_core::page_transition::PUSH;
};

using page_builder = void (*)(const page_request& request);

class page_router {
 public:
  explicit page_router(page_builder builder = nullptr) : builder_(builder) {}

  void set_builder(page_builder builder) {
    builder_ = builder;
  }
  bool navigate(page_id id, const void* payload = nullptr,
                gui2_core::page_transition transition = gui2_core::page_transition::PUSH);
  page_id current() const {
    return current_.id;
  }
  const page_request& current_request() const {
    return current_;
  }

 private:
  page_builder builder_ = nullptr;
  page_request current_{ page_id::HOME, nullptr, gui2_core::page_transition::NONE };
};

}  // namespace gui2_pages

#endif
