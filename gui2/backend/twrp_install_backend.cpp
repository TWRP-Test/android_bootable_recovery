#include "twrp_install_backend.h"

#include <sys/stat.h>

#include "data.hpp"
#include "partitions.hpp"
#include "twcommon.h"
#include "twinstall.h"
#include "twrp-functions.hpp"
#include "variables.h"

namespace gui2_backend {

twrp_install_backend::~twrp_install_backend() {
  if (worker_.joinable()) worker_.join();
}

void twrp_install_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

bool twrp_install_backend::start_zip(const std::vector<std::string>& paths, bool verify_digest) {
  if (running_.load() || paths.empty()) return false;
  join_finished_thread();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = install_state::RUNNING;
    detail_.clear();
    cache_wipe_ = false;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_install_backend::run_zip, this, paths, verify_digest);
  return true;
}

// Mirrors GUIAction::flash: performance mode around each zip, stop at the
// first failure, and remember whether anything asked for a cache wipe.
void twrp_install_backend::run_zip(std::vector<std::string> paths, bool verify_digest) {
  int wipe_cache = 0;
  bool ok = true;

  for (const std::string& path : paths) {
    DataManager::SetValue("tw_filename", path);
    const size_t slash = path.find_last_of('/');
    DataManager::SetValue("tw_file", slash == std::string::npos ? path : path.substr(slash + 1));

    TWFunc::SetPerformanceMode(true);
    const int result = TWinstall_zip(path.c_str(), &wipe_cache, verify_digest);
    TWFunc::SetPerformanceMode(false);
    if (result != 0) {
      LOGERR("Error installing zip file '%s'\n", path.c_str());
      ok = false;
      break;
    }
    PartitionManager.Unlock_Block_Partitions();
  }

  PartitionManager.Update_System_Details();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ok ? install_state::DONE : install_state::FAILED;
    cache_wipe_ = wipe_cache != 0;
  }
  running_.store(false);
}

std::vector<image_target> twrp_install_backend::image_targets() {
  std::vector<PartitionList> list;
  PartitionManager.Get_Partition_List("flashimg", &list);

  std::vector<image_target> result;
  result.reserve(list.size());
  for (const PartitionList& entry : list) {
    TWPartition* partition = PartitionManager.Find_Partition_By_Path(entry.Mount_Point);
    result.push_back({ entry.Display_Name, entry.Mount_Point,
                       partition != nullptr && partition->Is_SlotSelect() });
  }
  return result;
}

bool twrp_install_backend::start_image(const std::string& path, const std::string& mount_point,
                                       bool both_slots) {
  if (running_.load() || path.empty()) return false;
  join_finished_thread();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = install_state::RUNNING;
    detail_.clear();
    cache_wipe_ = false;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_install_backend::run_image, this, path, mount_point, both_slots);
  return true;
}

void twrp_install_backend::run_image(std::string path, std::string mount_point, bool both_slots) {
  const size_t slash = path.find_last_of('/');
  std::string directory = slash == std::string::npos ? "/" : path.substr(0, slash);
  std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);

  // Flash_Image reads the target from this variable, the way the legacy page
  // sets it before calling.
  DataManager::SetValue("tw_flash_partition", mount_point + ";");

  bool ok = false;
  TWPartition* target = PartitionManager.Find_Partition_By_Path(mount_point);
  if (both_slots && target != nullptr && target->Is_SlotSelect()) {
    const std::string current = PartitionManager.Get_Active_Slot_Display();
    ok = PartitionManager.Flash_Image(directory, filename);
    PartitionManager.Override_Active_Slot(current == "A" ? "B" : "A");
    ok = ok && PartitionManager.Flash_Image(directory, filename);
    PartitionManager.Override_Active_Slot(current);
  } else {
    ok = PartitionManager.Flash_Image(directory, filename);
  }

  PartitionManager.Update_System_Details();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ok ? install_state::DONE : install_state::FAILED;
  }
  running_.store(false);
}

install_status twrp_install_backend::status() {
  install_status result;
  std::lock_guard<std::mutex> lock(mutex_);
  result.state = state_;
  result.detail = detail_;
  result.cache_wipe_requested = cache_wipe_;
  return result;
}

void twrp_install_backend::acknowledge() {
  join_finished_thread();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != install_state::RUNNING) state_ = install_state::IDLE;
}

}  // namespace gui2_backend
