#include "twrp_screen_backend.h"

#include <private/android_filesystem_config.h>

#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "data.hpp"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#include "twrpminui/minui.h"

namespace gui2_backend {
namespace {

constexpr uid_t kMediaUid = AID_MEDIA_RW;
constexpr gid_t kMediaGid = AID_MEDIA_RW;
constexpr int kDefaultRecordingFps = 30;
constexpr int kRecordingFpsValues[] = { 15, 24, 30, 45, 60 };

int gui_refresh_fps() {
#ifdef TW_FRAMERATE
  return TW_FRAMERATE > 0 ? TW_FRAMERATE : 60;
#else
  return 60;
#endif
}

int highest_supported_recording_fps(int limit) {
  for (auto it = std::rbegin(kRecordingFpsValues); it != std::rend(kRecordingFpsValues); ++it) {
    if (*it <= limit) return *it;
  }
  return kRecordingFpsValues[0];
}

bool is_supported_recording_fps(int fps, int limit) {
  for (const int supported : kRecordingFpsValues) {
    if (fps == supported) return fps <= limit;
  }
  return false;
}

std::string timestamped_name(const char* prefix, const char* extension, int suffix = -1) {
  const time_t now = time(nullptr);
  struct tm local_time;
  if (localtime_r(&now, &local_time) == nullptr) return {};

  char name[128];
  if (suffix < 0) {
    std::strftime(name, sizeof(name), "%Y-%m-%d-%H-%M-%S", &local_time);
    return std::string(prefix) + name + extension;
  }
  std::snprintf(name, sizeof(name), "%s%s%d%s", prefix,
                (prefix[0] != '\0' && prefix[std::strlen(prefix) - 1] == '_') ? "" : "_", suffix,
                extension);
  return name;
}

}  // namespace

twrp_screen_backend::twrp_screen_backend(settings_store* settings) : settings_(settings) {
  last_activity_ms_ = 0;
}

twrp_screen_backend::~twrp_screen_backend() {
  recorder_.stop();
}

bool twrp_screen_backend::has_screenshot() const {
  return true;
}

bool twrp_screen_backend::has_brightness() const {
  return DataManager::GetIntValue("tw_has_brightnesss_file") != 0 &&
         !DataManager::GetStrValue("tw_brightness_file").empty();
}

std::string twrp_screen_backend::current_brightness() const {
  if (!has_brightness()) return {};
  std::string value = settings_ == nullptr ? DataManager::GetStrValue("tw_brightness")
                                           : settings_->get_string("tw_brightness", "");
  return value.empty() ? "255" : value;
}

std::string twrp_screen_backend::make_media_path(const char* directory, const char* prefix,
                                                 const char* extension) const {
  const std::string storage = DataManager::GetCurrentStoragePath();
  const bool mounted = !storage.empty() && PartitionManager.Is_Mounted_By_Path(storage);
  const std::string root = mounted ? storage : "/tmp";
  // Use /tmp when storage is unavailable.
  std::string folder = mounted ? root + "/" + directory : root;
  while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
  if (!TWFunc::Create_Dir_Recursive(folder, 0775, kMediaUid, kMediaGid)) return {};

  const std::string base = folder + "/" + timestamped_name(prefix, extension);
  if (access(base.c_str(), F_OK) != 0) return base;

  for (int suffix = 1; suffix < 1000; ++suffix) {
    const std::string candidate = folder + "/" + timestamped_name(prefix, extension, suffix);
    if (access(candidate.c_str(), F_OK) != 0) return candidate;
  }
  return {};
}

capture_result twrp_screen_backend::save_screenshot() {
  if (!has_screenshot()) return { false, {}, "Screenshot is unavailable" };

  // Match legacy screenshot ownership and mode.
  const std::string path = make_media_path("Pictures/Screenshots/", "Screenshot_", ".png");
  if (path.empty()) return { false, {}, "Unable to create screenshot directory" };

  if (gr_save_screenshot(path.c_str()) != 0) return { false, path, "Unable to save screenshot" };

  if (chmod(path.c_str(), 0666) != 0 || chown(path.c_str(), kMediaUid, kMediaGid) != 0)
    return { false, path, "Unable to set screenshot permissions" };
  return { true, path, {} };
}

bool twrp_screen_backend::has_screen_off() const {
#ifdef TW_NO_SCREEN_TIMEOUT
  return false;
#else
#ifndef TW_NO_SCREEN_BLANK
  return true;
#else
  return has_brightness();
#endif
#endif
}

bool twrp_screen_backend::is_screen_off() const {
  return static_cast<int>(state_) >= static_cast<int>(screen_state::OFF);
}

void twrp_screen_backend::blank_locked() {
  if (static_cast<int>(state_) >= static_cast<int>(screen_state::OFF)) return;
  // Preserve the pre-dimming brightness.
  if (state_ == screen_state::ON) original_brightness_ = current_brightness();
  state_ = screen_state::OFF;
  if (has_brightness()) TWFunc::Set_Brightness("0");
  TWFunc::check_and_run_script("/system/bin/postscreenblank.sh", "blank");
#ifndef TW_NO_SCREEN_BLANK
  gr_fb_blank(true);
  state_ = screen_state::BLANKED;
#endif
}

void twrp_screen_backend::unblank_locked() {
  if (state_ == screen_state::ON) return;
#ifndef TW_NO_SCREEN_BLANK
  if (state_ == screen_state::BLANKED) gr_fb_blank(false);
#endif
  if (static_cast<int>(state_) >= static_cast<int>(screen_state::OFF))
    TWFunc::check_and_run_script("/system/bin/postscreenunblank.sh", "unblank");
  if (!original_brightness_.empty()) TWFunc::Set_Brightness(original_brightness_);
  state_ = screen_state::ON;
  dim_start_ms_ = 0;
  last_dim_brightness_ = -1;
}

bool twrp_screen_backend::screen_off() {
  if (!has_screen_off()) return false;
  stop_recording_locked();
  blank_locked();
  return true;
}

bool twrp_screen_backend::screen_on() {
  if (!has_screen_off()) return false;
  // Synchronize hardware when an explicit wake is requested.
  if (state_ == screen_state::ON) {
#ifndef TW_NO_SCREEN_BLANK
    gr_fb_blank(false);
#endif
    const std::string brightness = current_brightness();
    if (!brightness.empty()) TWFunc::Set_Brightness(brightness);
    last_activity_ms_ = last_tick_ms_;
    return true;
  }
  unblank_locked();
  last_activity_ms_ = last_tick_ms_;
  return true;
}

void twrp_screen_backend::on_input_activity() {
  if (!has_screen_off()) return;
  if (state_ != screen_state::ON) unblank_locked();
  last_activity_ms_ = last_tick_ms_;
}

void twrp_screen_backend::tick(uint64_t monotonic_ms) {
  last_tick_ms_ = monotonic_ms;
  if (!has_screen_off()) return;
  if (last_activity_ms_ == 0) last_activity_ms_ = monotonic_ms;

  const int timeout = settings_ == nullptr ? DataManager::GetIntValue("tw_screen_timeout_secs")
                                           : settings_->get_int("tw_screen_timeout_secs", 0);
  if (timeout <= 0 || static_cast<int>(state_) >= static_cast<int>(screen_state::OFF)) return;

  const uint64_t elapsed = monotonic_ms >= last_activity_ms_ ? monotonic_ms - last_activity_ms_ : 0;
  if (timeout > 2 && state_ == screen_state::ON &&
      elapsed >= static_cast<uint64_t>(timeout - 2) * 1000) {
    original_brightness_ = current_brightness();
    state_ = screen_state::DIM;
    dim_start_ms_ = monotonic_ms;
    last_dim_brightness_ = -1;
  }
  if (state_ == screen_state::DIM && has_brightness()) {
    // Interpolate the final two seconds and write only on value changes.
    const int original = std::max(5, std::atoi(original_brightness_.c_str()));
    const uint64_t dim_elapsed = monotonic_ms >= dim_start_ms_ ? monotonic_ms - dim_start_ms_ : 0;
    const int progress = static_cast<int>(std::min<uint64_t>(1000, dim_elapsed * 1000 / 2000));
    const int value = original - (original - 5) * progress / 1000;
    if (value != last_dim_brightness_) {
      TWFunc::Set_Brightness(std::to_string(value));
      last_dim_brightness_ = value;
    }
  }
  if (elapsed >= static_cast<uint64_t>(timeout) * 1000) blank_locked();
}

bool twrp_screen_backend::has_recording() const {
  return true;
}

int twrp_screen_backend::max_recording_fps() const {
  return std::min(60, gui_refresh_fps());
}

int twrp_screen_backend::recording_fps() const {
  const int fps = settings_ == nullptr
                      ? DataManager::GetIntValue("tw_screen_record_fps")
                      : settings_->get_int("tw_screen_record_fps", kDefaultRecordingFps);
  const int limit = max_recording_fps();
  return is_supported_recording_fps(fps, limit) ? fps : highest_supported_recording_fps(limit);
}

bool twrp_screen_backend::set_recording_fps(int fps) {
  if (!has_recording() || settings_ == nullptr ||
      !is_supported_recording_fps(fps, max_recording_fps()))
    return false;
  return settings_->set_persistent("tw_screen_record_fps", std::to_string(fps));
}

bool twrp_screen_backend::is_recording() const {
  return recorder_.is_recording();
}

capture_result twrp_screen_backend::start_recording() {
  if (!has_recording()) return { false, {}, "Recording is unavailable" };
  if (recorder_.is_recording()) return { false, {}, "Recording is already active" };

  int width = 0;
  int height = 0;
  int row_bytes = 0;
  GRPixelFormat format = GRPixelFormat::UNKNOWN;
  if (gr_copy_frame(nullptr, 0, &width, &height, &row_bytes, &format) != -2)
    return { false, {}, "Unable to capture framebuffer" };
  (void)format;

  const std::string path = make_media_path("Pictures/Screen recordings/", "Screenrecord_", ".webm");
  if (path.empty()) return { false, {}, "Unable to create recording directory" };
  return recorder_.start(path, width, height, recording_fps(),
                         last_tick_ms_ == 0 ? 0 : last_tick_ms_);
}

capture_result twrp_screen_backend::stop_recording_locked() {
  capture_result result = recorder_.stop(last_tick_ms_);
  if (!result.path.empty() && result.success) {
    if (chmod(result.path.c_str(), 0666) != 0 ||
        chown(result.path.c_str(), kMediaUid, kMediaGid) != 0)
      return { false, result.path, "Unable to set recording permissions" };
  }
  return result;
}

capture_result twrp_screen_backend::stop_recording() {
  return stop_recording_locked();
}

void twrp_screen_backend::submit_frame(const frame_view& frame, uint64_t monotonic_ms) {
  recorder_.submit_frame(frame, monotonic_ms);
}

}  // namespace gui2_backend
