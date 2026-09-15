#include "shell/page_host.h"

#include "core/ui_helpers.h"
#include "shell/navigation_fade.h"

namespace gui2_shell {

void page_host::initialize(lv_obj_t* layer, const gui2_core::ui_metrics& metrics) {
  layer_ = layer;
  metrics_ = &metrics;
  content_ = nullptr;
}

page_scaffold_result page_host::build(const char* title, const char* summary, int bottom_reserved) {
  page_scaffold_result result;
  if (layer_ == nullptr || metrics_ == nullptr) return result;
  clear();
  result = build_page_scaffold(layer_, *metrics_, title, summary, bottom_reserved);
  create_navigation_fade(layer_, *metrics_);
  content_ = result.content;
  return result;
}

void page_host::clear() {
  if (layer_ != nullptr) lv_obj_clean(layer_);
  content_ = nullptr;
}

}  // namespace gui2_shell
