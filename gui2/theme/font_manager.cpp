#include "theme/font_manager.h"

#include <algorithm>
#include <cmath>

namespace gui2_theme {

namespace {

// The preview build points this at the font in the source tree.
#ifndef GUI2_UI_FONT_PATH
#define GUI2_UI_FONT_PATH "/twres/fonts/wqy-microhei.ttf"
#endif
constexpr const char* kUiFontPath = GUI2_UI_FONT_PATH;

int scaled_size(float scale, int base_size) {
  return std::max(1, static_cast<int>(std::lround(base_size * scale)));
}

constexpr int kConsoleBaseSizes[] = { 28, 34, 42 };

}  // namespace

lv_font_t* font_manager::console(int index) const {
  if (index < 0 || index >= kConsoleSizeCount) return status_;
  return console_[index] != nullptr ? console_[index] : status_;
}

bool font_manager::initialize(float scale) {
#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
  text_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 50));
  status_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 42));
  brand_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 104));
  keyboard_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 68));
  for (int i = 0; i < kConsoleSizeCount; ++i)
    console_[i] = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, kConsoleBaseSizes[i]));
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
  if (keyboard_ != nullptr) lv_tiny_ttf_destroy(keyboard_);
  for (lv_font_t*& font : console_) {
    if (font != nullptr) lv_tiny_ttf_destroy(font);
    font = nullptr;
  }
#endif
  text_ = nullptr;
  status_ = nullptr;
  brand_ = nullptr;
  keyboard_ = nullptr;
}

}  // namespace gui2_theme
