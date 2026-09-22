#include "pages/advanced_page.h"

#include "components/setting_card.h"

namespace gui2_pages {

void build_advanced_page(const advanced_page_options& options) {
  if (options.content == nullptr || options.metrics == nullptr || options.strings == nullptr)
    return;

  gui2_components::create_setting_card(
      options.content, *options.metrics, options.strings->file_manager_title,
      options.strings->file_manager_summary, options.option_event_callback,
      options.file_manager_target, options.press_guard_callback);
  gui2_components::create_setting_card(options.content, *options.metrics,
                                       options.strings->export_log_title,
                                       options.strings->export_log_summary,
                                       options.option_event_callback, options.export_log_target,
                                       options.press_guard_callback);
}

}  // namespace gui2_pages
