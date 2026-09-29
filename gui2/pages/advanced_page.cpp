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
  if (options.sideload_target != nullptr)
    gui2_components::create_setting_card(options.content, *options.metrics,
                                         options.strings->sideload_title,
                                         options.strings->sideload_summary,
                                         options.option_event_callback, options.sideload_target,
                                         options.press_guard_callback);
  if (options.wifi_target != nullptr)
    gui2_components::create_setting_card(options.content, *options.metrics,
                                         options.strings->wifi_title,
                                         options.strings->wifi_summary,
                                         options.option_event_callback, options.wifi_target,
                                         options.press_guard_callback);
  gui2_components::create_setting_card(options.content, *options.metrics,
                                       options.strings->export_log_title,
                                       options.strings->export_log_summary,
                                       options.option_event_callback, options.export_log_target,
                                       options.press_guard_callback);

  const auto add = [&](const void* target, const char* title, const char* detail) {
    if (target == nullptr) return;
    gui2_components::create_setting_card(options.content, *options.metrics, title, detail,
                                         options.option_event_callback, target,
                                         options.press_guard_callback);
  };
  const auto& strings = *options.strings;
  add(options.twrp_folder_target, strings.twrp_folder_title, options.twrp_folder_detail);
  add(options.disable_avb2_target, strings.avb_title, strings.avb_summary);
  add(options.fix_contexts_target, strings.fix_contexts_title, strings.fix_contexts_summary);
  add(options.install_ramdisk_target, strings.ramdisk_title, strings.ramdisk_summary);
  add(options.reflash_twrp_target, strings.reflash_title, strings.reflash_summary);
  add(options.install_kernel_target, strings.kernel_title, strings.kernel_summary);
  add(options.fix_bootloop_target, strings.fix_bootloop_title, strings.fix_bootloop_summary);
  add(options.unmap_super_target, strings.unmap_title, strings.unmap_summary);
  add(options.merge_snapshots_target, strings.merge_title, strings.merge_summary);
}

}  // namespace gui2_pages
