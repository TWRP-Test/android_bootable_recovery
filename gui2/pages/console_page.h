#ifndef GUI2_PAGES_CONSOLE_PAGE_H
#define GUI2_PAGES_CONSOLE_PAGE_H

#include <cstddef>
#include <vector>

#include "backend/console_backend.h"
#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_pages {

struct console_page_options {
  lv_obj_t* content = nullptr;
  const gui2_core::ui_metrics* metrics = nullptr;
  const char* empty_text = nullptr;
  const lv_font_t* font = nullptr;
};

struct console_page_view {
  lv_obj_t* body = nullptr;
  lv_obj_t* empty_label = nullptr;
  lv_obj_t* content = nullptr;
  const lv_font_t* font = nullptr;
  int next_y = 0;
  int line_gap = 0;
  int padding = 0;
  int minimum_height = 0;
};

console_page_view build_console_page(const console_page_options& options);

void append_console_lines(console_page_view* view, const gui2_core::ui_metrics& metrics,
                          const std::vector<gui2_backend::console_line>& lines);

void scroll_console_to_end(const console_page_view& view);

}  // namespace gui2_pages

#endif
