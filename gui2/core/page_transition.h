#ifndef GUI2_CORE_PAGE_TRANSITION_H
#define GUI2_CORE_PAGE_TRANSITION_H

namespace gui2_core {

// Direction of a page-host transition. REPLACE is used when a page is rebuilt
// in place (for example after applying a setting) and intentionally has no
// stack motion.
enum class page_transition {
  NONE,
  PUSH,
  POP,
  REPLACE,
};

}  // namespace gui2_core

#endif
