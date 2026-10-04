#include "twrp_operation.h"

#include <atomic>
#include <ctime>

#include <cutils/properties.h>

#include "data.hpp"
#include "twcommon.h"
#include "twrpperf/perf_manager.hpp"
#include "variables.h"

namespace gui2_backend {
namespace {

time_t Start;
std::atomic<bool> operation_ended{ false };

}  // namespace

void operation_start(const std::string& operation_name) {
  twrp::TwrpPerfManager::Get().BeginWorkload();
  LOGINFO("operation_start: '%s'\n", operation_name.c_str());
  time(&Start);
  DataManager::SetValue(TW_ACTION_BUSY, 1);
  DataManager::SetValue("ui_progress", 0);
  DataManager::SetValue("ui_portion_size", 0);
  DataManager::SetValue("ui_portion_start", 0);
  DataManager::SetValue("tw_operation", operation_name);
  DataManager::SetValue("tw_operation_state", 0);
  DataManager::SetValue("tw_operation_status", 0);
#ifdef AB_OTA_UPDATER
  DataManager::SetValue("tw_ab_device", 1);
#else
  DataManager::SetValue("tw_ab_device", 0);
#endif
}

void operation_end(const int operation_status) {
  time_t Stop;
  DataManager::SetValue("ui_progress", 100);
  if (operation_status != 0) {
    DataManager::SetValue("tw_operation_status", 1);
  } else {
    DataManager::SetValue("tw_operation_status", 0);
  }
  DataManager::SetValue("tw_operation_state", 1);
  DataManager::SetValue(TW_ACTION_BUSY, 0);
  operation_ended.store(true);
  property_set("twrp.action_complete", "1");
  time(&Stop);

#ifndef TW_NO_HAPTICS
  if ((int)difftime(Stop, Start) > 10) DataManager::Vibrate("tw_action_vibrate");
#endif

  LOGINFO("operation_end - status=%d\n", operation_status);
  twrp::TwrpPerfManager::Get().EndWorkload();
}

bool take_operation_ended() {
  return operation_ended.exchange(false);
}

}  // namespace gui2_backend
