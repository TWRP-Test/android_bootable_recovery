#include "theme/font_manager.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace gui2_theme {

namespace {

// The preview build points this at the font in the source tree.
#ifndef GUI2_UI_FONT_PATH
#define GUI2_UI_FONT_PATH "/twres/fonts/wqy-microhei.ttf"
#endif
constexpr const char* kUiFontPath = GUI2_UI_FONT_PATH;
#ifdef GUI2_HOST_PREVIEW
const std::string kTerminalFontPath =
    std::string(kUiFontPath).substr(0, std::string(kUiFontPath).find("extra-languages/fonts/")) +
    "common/fonts/DroidSansMono.ttf";
#else
constexpr const char* kTerminalFontPath = "/twres/fonts/DroidSansMono.ttf";
#endif

int scaled_size(float scale, int base_size) {
  return std::max(1, static_cast<int>(std::lround(base_size * scale)));
}

constexpr int kConsoleBaseSizes[] = { 28, 34, 42 };

}  // namespace

lv_font_t* font_manager::console(int index) const {
  if (index < 0 || index >= kConsoleSizeCount) return status_;
  return console_[index] != nullptr ? console_[index] : status_;
}

int font_manager::resize_terminal(int size) {
  size = std::clamp(size, 20, 64);
#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
  if (terminal_ != nullptr && terminal_size_ != size) {
    lv_tiny_ttf_set_size(terminal_, scaled_size(scale_, size));
    terminal_size_ = size;
  }
#endif
  return terminal_size_;
}

bool font_manager::initialize(float scale) {
#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
  text_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 50));
  status_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 42));
  brand_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 104));
  keyboard_ = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, 68));
  scale_ = scale;
  terminal_size_ = 34;
#ifdef GUI2_HOST_PREVIEW
  terminal_ = lv_tiny_ttf_create_file(kTerminalFontPath.c_str(), scaled_size(scale, terminal_size_));
#else
  terminal_ = lv_tiny_ttf_create_file(kTerminalFontPath, scaled_size(scale, terminal_size_));
#endif
  for (int i = 0; i < kConsoleSizeCount; ++i)
    console_[i] = lv_tiny_ttf_create_file(kUiFontPath, scaled_size(scale, kConsoleBaseSizes[i]));
  if (text_ != nullptr && status_ != nullptr && brand_ != nullptr && terminal_ != nullptr)
    return true;
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
  if (terminal_ != nullptr) lv_tiny_ttf_destroy(terminal_);
  terminal_ = nullptr;
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
