#include "pages/page_router.h"

namespace gui2_pages {

bool page_router::navigate(page_id id, const void* payload, gui2_core::page_transition transition) {
  if (builder_ == nullptr) return false;
  current_ = id;
  builder_({ id, payload, transition });
  return true;
}

}  // namespace gui2_pages
