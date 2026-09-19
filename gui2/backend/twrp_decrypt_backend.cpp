#include "twrp_decrypt_backend.h"

#include <utility>

#include "data.hpp"
#include "partitions.hpp"
#include "variables.h"

namespace gui2_backend {

twrp_decrypt_backend::~twrp_decrypt_backend() {
  if (worker_.joinable()) worker_.join();
}

bool twrp_decrypt_backend::is_encrypted() {
  return DataManager::GetIntValue(TW_IS_ENCRYPTED) != 0;
}

lock_kind twrp_decrypt_backend::kind() {
  switch (DataManager::GetIntValue(TW_CRYPTO_PWTYPE)) {
    case 1:
      return lock_kind::PASSWORD;
    case 2:
      return lock_kind::PATTERN;
    case 3:
      return lock_kind::PIN;
    default:
      return lock_kind::DEFAULT;
  }
}

void twrp_decrypt_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

bool twrp_decrypt_backend::start(const std::string& password) {
  if (running_.load()) return false;
  join_finished_thread();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = decrypt_state::RUNNING;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_decrypt_backend::run, this, password);
  return true;
}

bool twrp_decrypt_backend::start_refresh() {
  if (running_.load()) return false;
  join_finished_thread();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = decrypt_state::RUNNING;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_decrypt_backend::run_refresh, this);
  return true;
}

void twrp_decrypt_backend::run_refresh() {
  PartitionManager.Update_System_Details();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = decrypt_state::DONE;
  }
  running_.store(false);
}

void twrp_decrypt_backend::run(std::string password) {
  const int result = DataManager::GetIntValue(TW_IS_FBE)
                         ? PartitionManager.Decrypt_Device(password, 0)
                         : PartitionManager.Decrypt_Device(password);
  if (result == 0) {
    DataManager::SetValue(TW_IS_ENCRYPTED, 0);
    DataManager::SetBackupFolder();
    // Startup already ran this while /data was still locked.
    DataManager::LoadTWRPFolderInfo();
    PartitionManager.Update_System_Details();
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = result == 0 ? decrypt_state::DONE : decrypt_state::FAILED;
  }
  running_.store(false);
}

decrypt_state twrp_decrypt_backend::state() {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void twrp_decrypt_backend::acknowledge() {
  join_finished_thread();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != decrypt_state::RUNNING) state_ = decrypt_state::IDLE;
}

}  // namespace gui2_backend
