#ifndef GUI2_PAGES_SETTINGS_DATA_H
#define GUI2_PAGES_SETTINGS_DATA_H

#include <algorithm>

#include "backend/screen_backend.h"

namespace gui2_pages {

inline constexpr const char* timezone_values[24] = {
    "BST11;BDT", "HST10;HDT", "AST9;ADT", "PST8;PDT,M3.2.0,M11.1.0",
    "MST7;MDT,M3.2.0,M11.1.0", "CST6;CDT,M3.2.0,M11.1.0",
    "EST5;EDT,M3.2.0,M11.1.0", "AST4;ADT", "GRNLNDST3;GRNLNDDT",
    "FALKST2;FALKDT", "AZOREST1;AZOREDT", "GMT0;BST,M3.5.0,M10.5.0",
    "CET-1;CEST,M3.5.0,M10.5.0", "WET-2;WET,M3.2.0,M10.5.0", "SAUST-3;SAUDT",
    "WST-4;WDT", "PAKST-5;PAKDT", "TASHST-6;TASHDT", "THAIST-7;THAIDT",
    "TAIST-8;TAIDT", "JST-9;JSTDT", "EET-10;EETDT", "MET-11;METDT", "NZST-12;NZDT",
};

inline constexpr const char* timezone_offsets[4] = { "0", "15", "30", "45" };
inline constexpr int timezone_indices[24] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
};
inline constexpr int offset_indices[4] = { 0, 1, 2, 3 };
inline constexpr int format_indices[2] = { 0, 1 };
inline constexpr int recording_fps_values[5] = { 15, 24, 30, 45, 60 };

inline int recording_fps_limit(const gui2_backend::screen_backend* screen) {
  return screen == nullptr ? 60 : std::clamp(screen->max_recording_fps(), 15, 60);
}

inline int recording_fps_count(const gui2_backend::screen_backend* screen) {
  int count = 0;
  for (const int fps : recording_fps_values) {
    if (fps <= recording_fps_limit(screen)) ++count;
  }
  return std::max(1, count);
}

inline int recording_fps_at(const gui2_backend::screen_backend* screen, int index) {
  const int count = recording_fps_count(screen);
  index = std::clamp(index, 0, count - 1);
  int available_index = 0;
  for (const int fps : recording_fps_values) {
    if (fps <= recording_fps_limit(screen) && available_index++ == index) return fps;
  }
  return recording_fps_values[0];
}

}  // namespace gui2_pages

#endif
