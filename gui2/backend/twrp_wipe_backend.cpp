#include "twrp_wipe_backend.h"

#include <utility>

#include "data.hpp"
#include "partitions.hpp"
#include "variables.h"

namespace gui2_backend {

twrp_wipe_backend::~twrp_wipe_backend() {
  if (worker_.joinable()) worker_.join();
}

std::vector<wipe_target> twrp_wipe_backend::targets() {
  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("wipe", &list);

  std::vector<wipe_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list)
    result.push_back({ entry.Display_Name, entry.Mount_Point });
  return result;
}

bool twrp_wipe_backend::has_data_media() {
  return settings_ != nullptr && settings_->get_int(TW_HAS_DATA_MEDIA, 0) != 0;
}

void twrp_wipe_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

bool twrp_wipe_backend::start(job kind, std::vector<std::string> mount_points) {
  if (running_.load()) return false;
  join_finished_thread();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = wipe_state::RUNNING;
    status_.done = 0;
    status_.total = kind == job::LIST ? static_cast<int>(mount_points.size()) : 1;
    if (status_.total <= 0) return false;
  }

  running_.store(true);
  worker_ = std::thread(&twrp_wipe_backend::run, this, kind, std::move(mount_points));
  return true;
}

bool twrp_wipe_backend::start_factory_reset() {
  return start(job::FACTORY_RESET, {});
}

bool twrp_wipe_backend::start_format_data() {
  return start(job::FORMAT_DATA, {});
}

bool twrp_wipe_backend::start_wipe(const std::vector<std::string>& mount_points) {
  return start(job::LIST, mount_points);
}

void twrp_wipe_backend::run(job kind, std::vector<std::string> mount_points) {
  bool ok = false;

  if (kind == job::FACTORY_RESET) {
    ok = PartitionManager.Factory_Reset() != 0;
  } else if (kind == job::FORMAT_DATA) {
    ok = PartitionManager.Format_Data() != 0;
  } else {
    ok = true;
    for (const std::string& path : mount_points) {
      const bool wiped = path == "DALVIK" ? PartitionManager.Wipe_Dalvik_Cache() != 0
                                          : PartitionManager.Wipe_By_Path(path) != 0;
      if (!wiped) {
        ok = false;
        break;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      ++status_.done;
    }
  }

  if (ok) {
    PartitionManager.Update_System_Details();
    std::lock_guard<std::mutex> lock(mutex_);
    status_.done = status_.total;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.state = ok ? wipe_state::DONE : wipe_state::FAILED;
  }
  running_.store(false);
}

wipe_status twrp_wipe_backend::status() {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

}  // namespace gui2_backend
