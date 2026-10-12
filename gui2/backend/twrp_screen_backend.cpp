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
#include "gui/gui.hpp"
#include "gui/twmsg.h"
#include "partitions.hpp"
#include "twrp-functions.hpp"
#include "twrp_operation.h"
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
  orig_brightness = getBrightness();
  setTimer();
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

// GUIAction::screenshot; gui2 draws its own flash once the file is written.
capture_result twrp_screen_backend::save_screenshot() {
  time_t tm;
  char path[256];
  int path_len;
  uid_t uid = AID_MEDIA_RW;
  gid_t gid = AID_MEDIA_RW;

  const std::string storage = DataManager::GetCurrentStoragePath();
  if (PartitionManager.Is_Mounted_By_Path(storage)) {
    snprintf(path, sizeof(path), "%s/Pictures/Screenshots/", storage.c_str());
  } else {
    strcpy(path, "/tmp/");
  }

  if (!TWFunc::Create_Dir_Recursive(path, 0775, uid, gid))
    return { false, {}, "Unable to create screenshot directory" };

  tm = time(NULL);
  path_len = strlen(path);

  // Screenshot_2014-01-01-18-21-38.png
  strftime(path + path_len, sizeof(path) - path_len, "Screenshot_%Y-%m-%d-%H-%M-%S.png",
           localtime(&tm));

  int res = gr_save_screenshot(path);
  if (res == 0) {
    chmod(path, 0666);
    chown(path, uid, gid);

    gui_msg(Msg("screenshot_saved=Screenshot was saved to {1}")(path));
    return { true, path, {} };
  }
  gui_err("screenshot_err=Failed to take a screenshot!");
  return { false, path, "Unable to save screenshot" };
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
  return state >= kOff;
}

void twrp_screen_backend::setTimer() {
  clock_gettime(CLOCK_MONOTONIC, &btimer);
}

void twrp_screen_backend::checkForTimeout() {
#ifndef TW_NO_SCREEN_TIMEOUT
  const int sleepTimer = DataManager::GetIntValue("tw_screen_timeout_secs");
  timespec curTime, diff;
  clock_gettime(CLOCK_MONOTONIC, &curTime);
  diff = TWFunc::timespec_diff(btimer, curTime);
  if (sleepTimer > 2 && diff.tv_sec > (sleepTimer - 2) && state == kOn) {
    orig_brightness = getBrightness();
    state = kDim;
    TWFunc::Set_Brightness("5");
  }
  if (sleepTimer && diff.tv_sec > sleepTimer && state < kOff) {
    state = kOff;
    stop_recording();
    TWFunc::Set_Brightness("0");
    TWFunc::check_and_run_script("/system/bin/postscreenblank.sh", "blank");
    if (before_screen_off_callback_ != nullptr)
      before_screen_off_callback_(before_screen_off_user_data_);
  }
#ifndef TW_NO_SCREEN_BLANK
  if (state == kOff) {
    gr_fb_blank(true);
    state = kBlanked;
  }
#endif
#endif
}

std::string twrp_screen_backend::getBrightness() const {
  std::string result;

  if (DataManager::GetIntValue("tw_has_brightnesss_file")) {
    DataManager::GetValue("tw_brightness", result);
    if (result.empty()) result = "255";
  }
  return result;
}

void twrp_screen_backend::resetTimerAndUnblank() {
#ifndef TW_NO_SCREEN_TIMEOUT
  setTimer();
  switch (state) {
    case kBlanked:
#ifndef TW_NO_SCREEN_BLANK
      gr_fb_blank(false);
#endif
      TWFunc::check_and_run_script("/system/bin/postscreenunblank.sh", "unblank");
      [[fallthrough]];
    case kOff:
      [[fallthrough]];
    case kDim:
      if (!orig_brightness.empty()) TWFunc::Set_Brightness(orig_brightness);
      state = kOn;
      [[fallthrough]];
    case kOn:
      break;
  }
#endif
}

void twrp_screen_backend::blank() {
#ifndef TW_NO_SCREEN_TIMEOUT
  if (state == kOn) {
    orig_brightness = getBrightness();
    state = kOff;
    stop_recording();
    TWFunc::Set_Brightness("0");
    TWFunc::check_and_run_script("/system/bin/postscreenblank.sh", "blank");
  }
#ifndef TW_NO_SCREEN_BLANK
  if (state == kOff) {
    gr_fb_blank(true);
    state = kBlanked;
  }
#endif
#endif
}

void twrp_screen_backend::set_before_screen_off_callback(void (*callback)(void*), void* user_data) {
  before_screen_off_callback_ = callback;
  before_screen_off_user_data_ = user_data;
}

// The power key: blanktimer::toggleBlank, split in its two halves.
bool twrp_screen_backend::screen_off() {
  if (!has_screen_off()) return false;
  if (state != kOn) {
    resetTimerAndUnblank();
    return false;
  }
  blank();
  if (before_screen_off_callback_ != nullptr)
    before_screen_off_callback_(before_screen_off_user_data_);
  return true;
}

bool twrp_screen_backend::screen_on() {
  if (!has_screen_off()) return false;
  resetTimerAndUnblank();
  return true;
}

// InputHandler::processInput: input only counts while the screen is on.
void twrp_screen_backend::on_input_activity() {
#ifndef TW_NO_SCREEN_BLANK
  if (!is_screen_off())
#endif
    resetTimerAndUnblank();
}

void twrp_screen_backend::tick(uint64_t monotonic_ms) {
#ifdef TW_SCREEN_BLANK_ON_BOOT
  // gui_init, once the display is up.
  if (last_tick_ms_ == 0) {
    printf("TW_SCREEN_BLANK_ON_BOOT := true\n");
    blank();
    resetTimerAndUnblank();
  }
#endif
  last_tick_ms_ = monotonic_ms;
  if (take_operation_ended()) resetTimerAndUnblank();
  checkForTimeout();
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
  capture_result result =
      path.empty() ? capture_result{ false, {}, "Unable to create recording directory" }
                   : recorder_.start(path, width, height, recording_fps(),
                                     last_tick_ms_ == 0 ? 0 : last_tick_ms_);
  if (result.success)
    gui_msg(Msg("screenrecord_started=Screen recording started: {1}")(path));
  else
    gui_err("screenrecord_start_err=Unable to start screen recording!");
  return result;
}

capture_result twrp_screen_backend::stop_recording() {
  if (!recorder_.is_recording()) return {};
  capture_result result = recorder_.stop(last_tick_ms_);
  if (!result.path.empty() && result.success) {
    if (chmod(result.path.c_str(), 0666) != 0 ||
        chown(result.path.c_str(), kMediaUid, kMediaGid) != 0)
      result = { false, result.path, "Unable to set recording permissions" };
  }
  if (result.success)
    gui_msg(Msg("screenrecord_saved=Screen recording was saved to {1}")(result.path));
  else
    gui_err("screenrecord_save_err=Failed to save the screen recording!");
  return result;
}

void twrp_screen_backend::submit_frame(const frame_view& frame, uint64_t monotonic_ms) {
  recorder_.submit_frame(frame, monotonic_ms);
}

}  // namespace gui2_backend
