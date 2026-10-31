#include "theme/font_manager.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <cstdint>
#include <limits>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gui2_theme {

namespace {

// The preview build points this at the font in the source tree.
#ifndef GUI2_UI_FONT_PATH
#define GUI2_UI_FONT_PATH "/res/fonts/wqy-microhei.ttf"
#endif
constexpr const char* kUiFontPath = GUI2_UI_FONT_PATH;
#ifdef GUI2_HOST_PREVIEW
const std::string kTerminalFontPath =
    std::string(kUiFontPath).substr(0, std::string(kUiFontPath).find_last_of('/') + 1) +
    "DroidSansMono.ttf";
#else
constexpr const char* kTerminalFontPath = "/res/fonts/DroidSansMono.ttf";
#endif

int scaled_size(float scale, int base_size) {
  return std::max(1, static_cast<int>(std::lround(base_size * scale)));
}

constexpr int kConsoleBaseSizes[] = { 28, 34, 42 };

#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
bool map_font(const char* path, void** data, size_t* size) {
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat info {};
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
      static_cast<uintmax_t>(info.st_size) > std::numeric_limits<size_t>::max()) {
    close(fd);
    return false;
  }
  const size_t length = static_cast<size_t>(info.st_size);
  void* mapping = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (mapping == MAP_FAILED) return false;
  *data = mapping;
  *size = length;
  return true;
}
#endif

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
  // Assets stay immutable for the lifetime of GUI2. Mapping once avoids the
  // seek/read pair TinyTTF otherwise performs for individual glyph metrics.
#ifdef GUI2_HOST_PREVIEW
  const char* terminal_path = kTerminalFontPath.c_str();
#else
  const char* terminal_path = kTerminalFontPath;
#endif
  if (!map_font(kUiFontPath, &ui_data_, &ui_data_size_) ||
      !map_font(terminal_path, &terminal_data_, &terminal_data_size_)) {
    shutdown();
    return false;
  }
  text_ = lv_tiny_ttf_create_data(ui_data_, ui_data_size_, scaled_size(scale, 50));
  status_ = lv_tiny_ttf_create_data(ui_data_, ui_data_size_, scaled_size(scale, 42));
  brand_ = lv_tiny_ttf_create_data(ui_data_, ui_data_size_, scaled_size(scale, 104));
  keyboard_ = lv_tiny_ttf_create_data(ui_data_, ui_data_size_, scaled_size(scale, 68));
  scale_ = scale;
  terminal_size_ = 34;
  terminal_ = lv_tiny_ttf_create_data(terminal_data_, terminal_data_size_,
                                    scaled_size(scale, terminal_size_));
  for (int i = 0; i < kConsoleSizeCount; ++i)
    console_[i] = lv_tiny_ttf_create_data(ui_data_, ui_data_size_,
                                        scaled_size(scale, kConsoleBaseSizes[i]));
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
  if (ui_data_ != nullptr) munmap(ui_data_, ui_data_size_);
  if (terminal_data_ != nullptr) munmap(terminal_data_, terminal_data_size_);
  ui_data_ = terminal_data_ = nullptr;
  ui_data_size_ = terminal_data_size_ = 0;
}

}  // namespace gui2_theme
