/*
 * Copyright (C) 2026 The Team Win Recovery Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "twrpperf/perf_manager.hpp"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace twrp {
namespace {

constexpr char kCpuFreqPolicyGlob[] = "/sys/devices/system/cpu/cpufreq/policy*";
constexpr char kWaltSchedBoostPath[] = "/proc/sys/walt/sched_boost";
constexpr int64_t kNsPerMs = 1000000;
constexpr int64_t kPropertyCheckNs = 1000000000;

int64_t MonotonicNs() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<int64_t>(now.tv_sec) * 1000000000LL + now.tv_nsec;
}

pid_t CurrentTid() {
  return static_cast<pid_t>(syscall(SYS_gettid));
}

bool ReadValue(const std::string& path, std::string* value) {
  std::string contents;
  if (!android::base::ReadFileToString(path, &contents)) return false;
  *value = android::base::Trim(contents);
  return true;
}

bool ReadUint(const std::string& path, uint64_t* value) {
  std::string text;
  if (!ReadValue(path, &text) || text.empty()) return false;

  char* end = nullptr;
  errno = 0;
  const unsigned long long parsed = strtoull(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || *end != '\0') return false;
  *value = parsed;
  return true;
}

bool WriteValue(const std::string& path, const std::string& value) {
  const int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
  if (fd < 0) return false;

  ssize_t written;
  do {
    written = write(fd, value.data(), value.size());
  } while (written < 0 && errno == EINTR);
  int saved_errno = errno;
  if (written >= 0 && written != static_cast<ssize_t>(value.size())) saved_errno = EIO;
  close(fd);
  errno = saved_errno;
  return written == static_cast<ssize_t>(value.size());
}

std::vector<std::string> CpuFreqPolicies() {
  glob_t matches{};
  std::vector<std::string> policies;
  if (glob(kCpuFreqPolicyGlob, 0, nullptr, &matches) == 0) {
    policies.reserve(matches.gl_pathc);
    for (size_t i = 0; i < matches.gl_pathc; ++i) policies.emplace_back(matches.gl_pathv[i]);
  }
  globfree(&matches);
  return policies;
}

bool HasGovernor(const std::string& governor) {
  for (const auto& policy : CpuFreqPolicies()) {
    std::string current;
    if (ReadValue(policy + "/scaling_governor", &current) && current == governor) return true;
  }
  return false;
}

bool GetSchedAttr(pid_t tid, sched_attr* attr) {
  *attr = {};
  attr->size = sizeof(*attr);
  return syscall(SYS_sched_getattr, tid, attr, sizeof(*attr), 0) == 0;
}

bool SetSchedAttr(pid_t tid, sched_attr* attr) {
  attr->size = sizeof(*attr);
  return syscall(SYS_sched_setattr, tid, attr, 0) == 0;
}

class UClampController {
 public:
  UClampController(pid_t tid, uint32_t target_min) : tid_(tid), target_min_(target_min) {
    sched_attr attr{};
    supported_ = GetSchedAttr(tid_, &attr) && attr.size >= SCHED_ATTR_SIZE_VER1;
  }

  bool Supported() const {
    return supported_;
  }

  bool Activate() {
    sched_attr attr{};
    if (!supported_ || !GetSchedAttr(tid_, &attr)) return false;

    original_min_ = attr.sched_util_min;
    const uint32_t max_util = attr.sched_util_max == 0 ? 1024 : attr.sched_util_max;
    applied_min_ = std::min(std::max(original_min_, target_min_), max_util);
    if (applied_min_ != original_min_) {
      attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN;
      attr.sched_util_min = applied_min_;
      if (!SetSchedAttr(tid_, &attr)) {
        PLOG(WARNING) << "TwrpPerfManager: unable to apply uclamp.min";
        return false;
      }
    }
    active_ = true;
    return true;
  }

  void Deactivate() {
    if (!active_) return;

    sched_attr attr{};
    if (applied_min_ != original_min_ && GetSchedAttr(tid_, &attr) &&
        attr.sched_util_min == applied_min_) {
      attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN;
      attr.sched_util_min = original_min_;
      if (!SetSchedAttr(tid_, &attr))
        PLOG(WARNING) << "TwrpPerfManager: unable to restore uclamp.min";
    }
    active_ = false;
  }

 private:
  pid_t tid_;
  uint32_t target_min_;
  uint32_t original_min_ = 0;
  uint32_t applied_min_ = 0;
  bool supported_ = false;
  bool active_ = false;
};

class PerfBackend {
 public:
  virtual ~PerfBackend() = default;
  virtual const char* Name() const = 0;
  virtual bool Supported() const = 0;
  virtual bool Activate() = 0;
  virtual void Deactivate() = 0;
};

class WaltBackend final : public PerfBackend {
 public:
  WaltBackend(pid_t tid, uint32_t uclamp_min, int sched_boost)
      : uclamp_(tid, uclamp_min), sched_boost_(sched_boost) {
    walt_available_ = HasGovernor("walt") || access(kWaltSchedBoostPath, F_OK) == 0;
    sched_boost_available_ = sched_boost_ > 0 && access(kWaltSchedBoostPath, W_OK) == 0;
  }

  const char* Name() const override {
    return "walt";
  }

  bool Supported() const override {
    return walt_available_ && (uclamp_.Supported() || sched_boost_available_);
  }

  bool Activate() override {
    bool applied = uclamp_.Supported() && uclamp_.Activate();

    if (sched_boost_available_) {
      std::string current;
      if (ReadValue(kWaltSchedBoostPath, &current)) {
        original_sched_boost_ = current;
        if (current == "0") {
          const std::string target = std::to_string(sched_boost_);
          if (WriteValue(kWaltSchedBoostPath, target)) {
            applied_sched_boost_ = target;
            sched_boost_changed_ = true;
            applied = true;
          } else {
            PLOG(WARNING) << "TwrpPerfManager: unable to apply WALT sched_boost";
          }
        } else {
          applied = true;
        }
      }
    }
    active_ = applied;
    return applied;
  }

  void Deactivate() override {
    if (!active_) return;

    if (sched_boost_changed_) {
      std::string current;
      if (ReadValue(kWaltSchedBoostPath, &current) && current == applied_sched_boost_ &&
          !WriteValue(kWaltSchedBoostPath, original_sched_boost_)) {
        PLOG(WARNING) << "TwrpPerfManager: unable to restore WALT sched_boost";
      }
    }
    uclamp_.Deactivate();
    sched_boost_changed_ = false;
    active_ = false;
  }

 private:
  UClampController uclamp_;
  int sched_boost_;
  bool walt_available_ = false;
  bool sched_boost_available_ = false;
  bool sched_boost_changed_ = false;
  bool active_ = false;
  std::string original_sched_boost_;
  std::string applied_sched_boost_;
};

class UClampBackend final : public PerfBackend {
 public:
  UClampBackend(pid_t tid, uint32_t target_min, bool require_schedutil)
      : uclamp_(tid, target_min),
        governor_supported_(!require_schedutil || HasGovernor("schedutil")) {}

  const char* Name() const override {
    return "uclamp";
  }

  bool Supported() const override {
    return governor_supported_ && uclamp_.Supported();
  }

  bool Activate() override {
    return uclamp_.Activate();
  }

  void Deactivate() override {
    uclamp_.Deactivate();
  }

 private:
  UClampController uclamp_;
  bool governor_supported_;
};

class CpuFreqBackend final : public PerfBackend {
 public:
  explicit CpuFreqBackend(uint32_t target_percent) : target_percent_(target_percent) {
    for (const auto& policy : CpuFreqPolicies()) {
      if (access((policy + "/scaling_min_freq").c_str(), W_OK) == 0) {
        supported_ = true;
        break;
      }
    }
  }

  const char* Name() const override {
    return "cpufreq";
  }

  bool Supported() const override {
    return supported_;
  }

  bool Activate() override {
    states_.clear();
    bool applied = false;
    for (const auto& policy : CpuFreqPolicies()) {
      const std::string min_path = policy + "/scaling_min_freq";
      uint64_t original_min;
      uint64_t max_freq;
      if (!ReadUint(min_path, &original_min) ||
          !ReadUint(policy + "/scaling_max_freq", &max_freq)) {
        continue;
      }

      uint64_t target = max_freq * target_percent_ / 100;
      std::string available;
      if (ReadValue(policy + "/scaling_available_frequencies", &available)) {
        std::istringstream frequencies(available);
        std::vector<uint64_t> values;
        uint64_t frequency;
        while (frequencies >> frequency) values.push_back(frequency);
        std::sort(values.begin(), values.end());
        const auto it = std::lower_bound(values.begin(), values.end(), target);
        if (it != values.end()) target = *it;
      }
      target = std::min(target, max_freq);
      target = std::max(target, original_min);

      PolicyState state{ min_path, original_min, target, false };
      if (target == original_min) {
        applied = true;
      } else if (WriteValue(min_path, std::to_string(target))) {
        state.changed = true;
        applied = true;
      }
      states_.push_back(std::move(state));
    }
    active_ = applied;
    return applied;
  }

  void Deactivate() override {
    if (!active_) return;

    for (const auto& state : states_) {
      uint64_t current;
      if (state.changed && ReadUint(state.path, &current) && current == state.applied &&
          !WriteValue(state.path, std::to_string(state.original))) {
        PLOG(WARNING) << "TwrpPerfManager: unable to restore " << state.path;
      }
    }
    states_.clear();
    active_ = false;
  }

 private:
  struct PolicyState {
    std::string path;
    uint64_t original;
    uint64_t applied;
    bool changed;
  };

  uint32_t target_percent_;
  bool supported_ = false;
  bool active_ = false;
  std::vector<PolicyState> states_;
};

}  // namespace

struct TwrpPerfManager::Impl {
  void Initialize(pid_t new_render_tid) {
    std::lock_guard<std::mutex> lock(mutex);
    const pid_t target_tid = new_render_tid > 0 ? new_render_tid : CurrentTid();
    if (initialized && render_tid == target_tid) return;
    ReleaseLocked();

    render_tid = target_tid;
    hold_ms = android::base::GetIntProperty<int>("twrp.perf.hold_ms", 300, 50, 2000);
    const int uclamp_min = android::base::GetIntProperty<int>("twrp.perf.uclamp_min", 512, 0, 1024);
    const int cpufreq_percent =
        android::base::GetIntProperty<int>("twrp.perf.cpufreq_percent", 50, 1, 100);
    const int walt_sched_boost =
        android::base::GetIntProperty<int>("twrp.perf.walt_sched_boost", 1, 0, 3);
    std::string requested = android::base::GetProperty("twrp.perf.backend", "auto");
    if (requested != "auto" && requested != "walt" && requested != "uclamp" &&
        requested != "cpufreq" && requested != "off" && requested != "none") {
      LOG(WARNING) << "TwrpPerfManager: unknown backend '" << requested << "', using auto";
      requested = "auto";
    }

    auto add_backend = [this](std::unique_ptr<PerfBackend> backend) {
      if (backend->Supported()) backends.push_back(std::move(backend));
    };

    if (requested == "auto" || requested == "walt") {
      add_backend(std::make_unique<WaltBackend>(render_tid, uclamp_min, walt_sched_boost));
    }
    if (requested == "auto") {
      add_backend(std::make_unique<UClampBackend>(render_tid, uclamp_min, true));
    } else if (requested == "walt" || requested == "uclamp") {
      add_backend(std::make_unique<UClampBackend>(render_tid, uclamp_min, false));
    }
    if (requested != "off" && requested != "none") {
      add_backend(std::make_unique<CpuFreqBackend>(cpufreq_percent));
    }

    initialized = true;
    enabled = android::base::GetBoolProperty("twrp.perf.enabled", true);
    next_property_check_ns = MonotonicNs() + kPropertyCheckNs;
    LOG(INFO) << "TwrpPerfManager: backend=" << BackendNameLocked() << ", enabled=" << enabled
              << ", hold_ms=" << hold_ms << ", uclamp_min=" << uclamp_min
              << ", cpufreq_percent=" << cpufreq_percent;
  }

  void NotifyActivity(bool allow_activate) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!initialized) return;

    const int64_t now_ns = MonotonicNs();
    RefreshEnabledLocked(now_ns);
    if (!enabled || backends.empty()) return;

    if (!boosted) {
      if (!allow_activate || !ActivateLocked()) return;
    }
    boost_deadline_ns = std::max(boost_deadline_ns, now_ns + hold_ms * kNsPerMs);
  }

  void Update() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!initialized) return;

    const int64_t now_ns = MonotonicNs();
    RefreshEnabledLocked(now_ns);
    if (boosted && now_ns >= boost_deadline_ns) DeactivateLocked();
  }

  int ClampTimeoutMs(int timeout_ms) const {
    std::lock_guard<std::mutex> lock(mutex);
    if (!boosted || timeout_ms <= 0) return timeout_ms;

    const int64_t remaining_ns = boost_deadline_ns - MonotonicNs();
    if (remaining_ns <= 0) return 0;
    const int64_t remaining_ms = (remaining_ns + kNsPerMs - 1) / kNsPerMs;
    return std::min<int64_t>(timeout_ms, remaining_ms);
  }

  void Release() {
    std::lock_guard<std::mutex> lock(mutex);
    ReleaseLocked();
  }

  std::string BackendName() const {
    std::lock_guard<std::mutex> lock(mutex);
    return BackendNameLocked();
  }

  bool IsBoosted() const {
    std::lock_guard<std::mutex> lock(mutex);
    return boosted;
  }

 private:
  bool TraceEnabledLocked() const {
    return android::base::GetBoolProperty("twrp.perf.stats",
                                          android::base::GetBoolProperty("twrp.gui.stats", false));
  }

  std::string BackendNameLocked() const {
    return backend_index < backends.size() ? backends[backend_index]->Name() : "none";
  }

  bool ActivateLocked() {
    while (backend_index < backends.size()) {
      if (backends[backend_index]->Activate()) {
        boosted = true;
        if (TraceEnabledLocked())
          LOG(INFO) << "TwrpPerfManager: boost on via " << BackendNameLocked();
        return true;
      }
      LOG(WARNING) << "TwrpPerfManager: backend " << BackendNameLocked()
                   << " failed, trying fallback";
      backends[backend_index]->Deactivate();
      ++backend_index;
    }
    return false;
  }

  void DeactivateLocked() {
    if (!boosted) return;
    backends[backend_index]->Deactivate();
    boosted = false;
    boost_deadline_ns = 0;
    if (TraceEnabledLocked()) LOG(INFO) << "TwrpPerfManager: boost off via " << BackendNameLocked();
  }

  void RefreshEnabledLocked(int64_t now_ns) {
    if (now_ns < next_property_check_ns) return;
    next_property_check_ns = now_ns + kPropertyCheckNs;
    const bool new_enabled = android::base::GetBoolProperty("twrp.perf.enabled", true);
    if (enabled && !new_enabled) DeactivateLocked();
    enabled = new_enabled;
  }

  void ReleaseLocked() {
    DeactivateLocked();
    backends.clear();
    backend_index = 0;
    render_tid = 0;
    initialized = false;
    enabled = false;
    boost_deadline_ns = 0;
    next_property_check_ns = 0;
  }

  mutable std::mutex mutex;
  std::vector<std::unique_ptr<PerfBackend>> backends;
  size_t backend_index = 0;
  pid_t render_tid = 0;
  int64_t boost_deadline_ns = 0;
  int64_t next_property_check_ns = 0;
  int hold_ms = 300;
  bool initialized = false;
  bool enabled = false;
  bool boosted = false;
};

TwrpPerfManager& TwrpPerfManager::Get() {
  static TwrpPerfManager instance;
  return instance;
}

TwrpPerfManager::TwrpPerfManager() : impl_(std::make_unique<Impl>()) {}

TwrpPerfManager::~TwrpPerfManager() {
  impl_->Release();
}

void TwrpPerfManager::Initialize(pid_t render_tid) {
  impl_->Initialize(render_tid);
}

void TwrpPerfManager::NotifyInteraction() {
  impl_->NotifyActivity(true);
}

void TwrpPerfManager::NotifyFrameActivity() {
  impl_->NotifyActivity(false);
}

void TwrpPerfManager::Update() {
  impl_->Update();
}

int TwrpPerfManager::ClampTimeoutMs(int timeout_ms) const {
  return impl_->ClampTimeoutMs(timeout_ms);
}

void TwrpPerfManager::Release() {
  impl_->Release();
}

std::string TwrpPerfManager::BackendName() const {
  return impl_->BackendName();
}

bool TwrpPerfManager::IsBoosted() const {
  return impl_->IsBoosted();
}

}  // namespace twrp
