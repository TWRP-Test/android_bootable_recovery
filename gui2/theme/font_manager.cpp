#include "theme/font_manager.h"

#include <algorithm>
#include <cmath>

#include "src/libs/tiny_ttf/lv_tiny_ttf.h"

namespace gui2_theme {

namespace {

constexpr const char* kUiFontPath = "/twres/fonts/wqy-microhei.ttf";

int scaled_size(float scale, int base_size) {
  return std::max(1, static_cast<int>(std::lround(base_size * scale)));
}

}  // namespace

bool font_manager::initialize(float scale) {
#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
  text_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 50));
  status_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 42));
  brand_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 104));
  if (text_ != nullptr && status_ != nullptr && brand_ != nullptr) return true;
#else
  (void)scale;
#endif

  shutdown();
  return false;
}

void font_manager::shutdown() {
#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
  if (text_ != nullptr) lv_tiny_ttf_destroy(text_);
  if (status_ != nullptr) lv_tiny_ttf_destroy(status_);
  if (brand_ != nullptr) lv_tiny_ttf_destroy(brand_);
#endif
  text_ = nullptr;
  status_ = nullptr;
  brand_ = nullptr;
}

}  // namespace gui2_theme
