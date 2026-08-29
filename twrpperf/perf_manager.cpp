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
#include <sched.h>
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

enum class CpuAffinityKind {
  kAll,
  kWorkload,
  kGui,
};

class CpuAffinityController final {
 public:
  explicit CpuAffinityController(CpuAffinityKind kind) {
    BuildMask(kind);
  }

  bool Supported() const {
    return supported_;
  }

  std::string PreferredMask() const {
    std::ostringstream mask;
    bool first = true;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
      if (!CPU_ISSET(cpu, &preferred_)) continue;
      if (!first) mask << ',';
      mask << cpu;
      first = false;
    }
    return first ? "none" : mask.str();
  }

  bool Active() const {
    return active_;
  }

  bool Disjoint(const CpuAffinityController& other) const {
    cpu_set_t overlap{};
    CPU_AND(&overlap, &preferred_, &other.preferred_);
    return CPU_COUNT(&overlap) == 0;
  }

  bool Activate(pid_t tid, const char* role) {
    if (!supported_) return false;
    if (active_) {
      if (tid == tid_) return true;
      Deactivate();
    }

    cpu_set_t original{};
    if (sched_getaffinity(tid, sizeof(original), &original) != 0) return false;
    if (CPU_EQUAL(&original, &preferred_)) {
      tid_ = tid;
      original_ = original;
      active_ = true;
      return true;
    }
    if (sched_setaffinity(tid, sizeof(preferred_), &preferred_) != 0) {
      PLOG(WARNING) << "TwrpPerfManager: unable to set " << role << " CPU affinity";
      return false;
    }
    tid_ = tid;
    original_ = original;
    active_ = true;
    return true;
  }

  void Deactivate() {
    if (!active_) return;
    if (sched_setaffinity(tid_, sizeof(original_), &original_) != 0)
      PLOG(WARNING) << "TwrpPerfManager: unable to restore CPU affinity";
    active_ = false;
    tid_ = 0;
  }

 private:
  void BuildMask(CpuAffinityKind kind) {
    cpu_set_t allowed{};
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) return;

    CPU_ZERO(&preferred_);
    const long configured_cpus = sysconf(_SC_NPROCESSORS_CONF);
    const int cpu_limit = configured_cpus > 0
                              ? static_cast<int>(std::min<long>(configured_cpus, CPU_SETSIZE))
                              : CPU_SETSIZE;
    std::vector<uint64_t> scores(cpu_limit);
    bool use_capacity = true;
    for (int cpu = 0; cpu < cpu_limit; ++cpu) {
      if (!CPU_ISSET(cpu, &allowed)) continue;
      const std::string cpu_path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
      if (!ReadUint(cpu_path + "/cpu_capacity", &scores[cpu]) || scores[cpu] == 0) {
        use_capacity = false;
        break;
      }
    }
    if (!use_capacity) {
      bool use_frequency = true;
      for (int cpu = 0; cpu < cpu_limit; ++cpu) {
        if (!CPU_ISSET(cpu, &allowed)) continue;
        const std::string cpu_path = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
        if (!ReadUint(cpu_path + "/cpufreq/cpuinfo_max_freq", &scores[cpu]) || scores[cpu] == 0) {
          use_frequency = false;
          break;
        }
      }
      if (!use_frequency) std::fill(scores.begin(), scores.end(), 0);
    }
    uint64_t max_score = 0;
    uint64_t min_score = 0;
    bool have_score = false;
    std::vector<uint64_t> score_levels;
    score_levels.reserve(cpu_limit);
    for (int cpu = 0; cpu < cpu_limit; ++cpu) {
      if (!CPU_ISSET(cpu, &allowed)) continue;
      const uint64_t score = scores[cpu];
      if (score != 0) {
        have_score = true;
        max_score = std::max(max_score, score);
        min_score = min_score == 0 ? score : std::min(min_score, score);
        score_levels.push_back(score);
      }
    }
    std::sort(score_levels.begin(), score_levels.end());
    score_levels.erase(std::unique(score_levels.begin(), score_levels.end()), score_levels.end());

    std::vector<int> min_score_cpus;
    if (score_levels.size() == 2) {
      for (int cpu = 0; cpu < cpu_limit; ++cpu) {
        if (CPU_ISSET(cpu, &allowed) && scores[cpu] == min_score) min_score_cpus.push_back(cpu);
      }
    }

    for (int cpu = 0; cpu < cpu_limit; ++cpu) {
      if (!CPU_ISSET(cpu, &allowed)) continue;
      const uint64_t score = scores[cpu];
      const auto gui_cpu_end =
          min_score_cpus.begin() + std::min<size_t>(2, min_score_cpus.size());
      const bool reserve_for_gui =
          score_levels.size() == 2 &&
          std::find(min_score_cpus.begin(), gui_cpu_end, cpu) != gui_cpu_end;
      const bool use_cpu =
          kind == CpuAffinityKind::kAll || !have_score || min_score == max_score ||
          (kind == CpuAffinityKind::kWorkload &&
           (score_levels.size() == 2 ? !reserve_for_gui
                                     : (score == max_score || score > min_score))) ||
          (kind == CpuAffinityKind::kGui &&
           (score_levels.size() == 2 ? reserve_for_gui : score == min_score));
      if (use_cpu) CPU_SET(cpu, &preferred_);
    }
    supported_ = CPU_COUNT(&preferred_) > 0;
  }

  cpu_set_t preferred_{};
  cpu_set_t original_{};
  pid_t tid_ = 0;
  bool supported_ = false;
  bool active_ = false;
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
    const bool prefer_big_cores =
        android::base::GetBoolProperty("twrp.perf.prefer_big_cores", true);
    const bool separate_gui_cpu =
        android::base::GetBoolProperty("twrp.perf.separate_gui_cpu", true);
    workload_affinity = std::make_unique<CpuAffinityController>(
        prefer_big_cores ? CpuAffinityKind::kWorkload : CpuAffinityKind::kAll);
    if (separate_gui_cpu && prefer_big_cores)
      gui_affinity = std::make_unique<CpuAffinityController>(CpuAffinityKind::kGui);
    std::string requested = android::base::GetProperty("twrp.perf.backend", "auto");
    if (requested != "auto" && requested != "walt" && requested != "uclamp" &&
        requested != "cpufreq" && requested != "off" && requested != "none") {
      LOG(WARNING) << "TwrpPerfManager: unknown backend '" << requested << "', using auto";
      requested = "auto";
    }
    affinity_enabled = requested != "off" && requested != "none";

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
              << ", cpufreq_percent=" << cpufreq_percent
              << ", prefer_big_cores=" << prefer_big_cores
              << ", separate_gui_cpu=" << separate_gui_cpu
              << ", affinity="
              << (affinity_enabled && workload_affinity->Supported() ? "available" : "none")
              << ", workload_mask=" << workload_affinity->PreferredMask()
              << ", gui_affinity="
              << (gui_affinity && gui_affinity->Supported() ? "available" : "none")
              << ", gui_mask="
              << (gui_affinity ? gui_affinity->PreferredMask() : "none");
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
    if (boosted && !workload_active && now_ns >= boost_deadline_ns) DeactivateLocked();
  }

  void BeginWorkload() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!initialized || !enabled) return;

    const int64_t now_ns = MonotonicNs();
    RefreshEnabledLocked(now_ns);
    if (!enabled) return;
    if (!affinity_enabled) return;
    if (!boosted && !backends.empty() && !ActivateLocked() && !workload_affinity->Supported())
      return;

    if (workload_active) {
      boost_deadline_ns = std::max(boost_deadline_ns, now_ns + hold_ms * kNsPerMs);
      return;
    }
    const pid_t tid = CurrentTid();
    const bool workload_affinity_active =
        workload_affinity && workload_affinity->Activate(tid, "workload");
    if (tid != render_tid && workload_affinity_active && gui_affinity &&
        gui_affinity->Supported() &&
        workload_affinity->Disjoint(*gui_affinity)) {
      if (!gui_affinity->Activate(render_tid, "GUI")) workload_affinity->Deactivate();
    }
    workload_active = true;
    boost_deadline_ns = std::max(boost_deadline_ns, now_ns + hold_ms * kNsPerMs);
  }

  void EndWorkload() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!workload_active) return;
    if (workload_affinity) workload_affinity->Deactivate();
    if (gui_affinity) gui_affinity->Deactivate();
    workload_active = false;
    if (boosted && MonotonicNs() >= boost_deadline_ns) DeactivateLocked();
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
    if (workload_affinity) workload_affinity->Deactivate();
    if (gui_affinity) gui_affinity->Deactivate();
    workload_active = false;
    DeactivateLocked();
    backends.clear();
    backend_index = 0;
    render_tid = 0;
    initialized = false;
    enabled = false;
    affinity_enabled = false;
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
  bool affinity_enabled = false;
  bool boosted = false;
  std::unique_ptr<CpuAffinityController> workload_affinity;
  std::unique_ptr<CpuAffinityController> gui_affinity;
  bool workload_active = false;
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

void TwrpPerfManager::BeginWorkload() {
  impl_->BeginWorkload();
}

void TwrpPerfManager::EndWorkload() {
  impl_->EndWorkload();
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
