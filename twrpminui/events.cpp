/*
 * Copyright (C) 2007 The Android Open Source Project
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

// Baseline: current minui/events.cpp; local differences are marked TWRP.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h> // TWRP: deduplicate discovery and inotify opens.
#include <unistd.h>

#include <functional>
#include <memory>
#include <string>

#include <android-base/strings.h>
#include <android-base/unique_fd.h>

#include "twrpminui/minui.h"
#include "twrp_input.h" // TWRP: touch/keyboard metadata, without owning descriptors.

constexpr const char* INPUT_DEV_DIR = "/dev/input";

constexpr size_t MAX_DEVICES = 32; // TWRP: preserve historical device capacity.
constexpr size_t MAX_MISC_FDS = 16;

constexpr size_t BITS_PER_LONG = sizeof(unsigned long) * 8;
constexpr size_t BITS_TO_LONGS(size_t bits) {
  return ((bits + BITS_PER_LONG - 1) / BITS_PER_LONG);
}

struct FdInfo {
  android::base::unique_fd fd;
  ev_callback cb;
  bool input_device = false; // TWRP: hotplugged devices are not misc fds.
};

static bool g_allow_touch_inputs = true;
static ev_callback g_saved_input_cb;
static android::base::unique_fd g_epoll_fd;
static epoll_event g_polled_events[MAX_DEVICES + MAX_MISC_FDS];
static int g_polled_events_count;

static FdInfo ev_fdinfo[MAX_DEVICES + MAX_MISC_FDS];

static size_t g_ev_count = 0;
static size_t g_ev_dev_count = 0;
static size_t g_ev_misc_count = 0;

// TWRP BEGIN: reuse disconnected slots instead of exhausting minui's fd table.
static void remove_fd(int fd) {
  for (size_t i = 0; i < g_ev_count; ++i) {
    auto& info = ev_fdinfo[i];
    if (info.fd.get() != fd) continue;
    epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    if (info.input_device) { twrp_input_device_removed(fd); --g_ev_dev_count; }
    else --g_ev_misc_count;
    info.fd.reset();
    info.cb = nullptr;
    info.input_device = false;
    return;
  }
}
static int add_fd(android::base::unique_fd&& fd, ev_callback cb, bool input) {
  // TWRP: an IN_CREATE queued during the initial scan must not open evdev twice.
  if (input) {
    struct stat candidate{};
    if (fstat(fd, &candidate)) return -1;
    for (size_t i = 0; i < g_ev_count; ++i) {
      if (!ev_fdinfo[i].input_device || ev_fdinfo[i].fd.get() < 0) continue;
      struct stat existing{};
      if (!fstat(ev_fdinfo[i].fd, &existing) && candidate.st_dev == existing.st_dev &&
          candidate.st_ino == existing.st_ino && candidate.st_rdev == existing.st_rdev) return 0;
    }
  }
  if (!cb || (input ? g_ev_dev_count == MAX_DEVICES : g_ev_misc_count == MAX_MISC_FDS)) return -1;
  size_t slot = 0;
  while (slot < g_ev_count && ev_fdinfo[slot].fd.get() >= 0) ++slot;
  if (slot == MAX_DEVICES + MAX_MISC_FDS) return -1;
  epoll_event event{};
  event.events = EPOLLIN | EPOLLWAKEUP;
  event.data.ptr = &ev_fdinfo[slot];
  if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &event)) return -1;
  ev_fdinfo[slot].fd = std::move(fd);
  ev_fdinfo[slot].cb = std::move(cb);
  ev_fdinfo[slot].input_device = input;
  if (slot == g_ev_count) ++g_ev_count;
  if (input) { ++g_ev_dev_count; twrp_input_device_added(ev_fdinfo[slot].fd); }
  else ++g_ev_misc_count;
  return 0;
}
// TWRP END

static bool test_bit(size_t bit, unsigned long* array) { // NOLINT
  return (array[bit / BITS_PER_LONG] & (1UL << (bit % BITS_PER_LONG))) != 0;
}

static bool should_add_input_device(int fd, bool allow_touch_inputs) {
  // Use unsigned long to match ioctl's parameter type.
  unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];  // NOLINT

  // Read the evbits of the input device.
  if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
    return false;
  }

  // We assume that only EV_KEY, EV_REL, and EV_SW event types are ever needed. EV_ABS is also
  // allowed if allow_touch_inputs is set.
  if (!test_bit(EV_KEY, ev_bits) && !test_bit(EV_REL, ev_bits) && !test_bit(EV_SW, ev_bits)) {
    if (!allow_touch_inputs || !test_bit(EV_ABS, ev_bits)) {
      return false;
    }
  }

  return true;
}

static int inotify_cb(int fd, __unused uint32_t epevents) {
  if (g_saved_input_cb == nullptr) return -1;

  // The inotify will put one or several complete events.
  // Should not read part of one event.
  int event_len_int;
  int ret = ioctl(fd, FIONREAD, &event_len_int);
  if (ret != 0) return -1;
  if (event_len_int < 0) return -1;
  size_t event_len = event_len_int;

  std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(INPUT_DEV_DIR), closedir);
  if (!dir) {
    return -1;
  }

  std::vector<int8_t> buf(event_len);

  ssize_t r = TEMP_FAILURE_RETRY(read(fd, buf.data(), event_len));
  if (r != event_len) {
    return -1;
  }

  size_t offset = 0;
  while (offset < event_len) {
    struct inotify_event* pevent = reinterpret_cast<struct inotify_event*>(buf.data() + offset);
    if (offset + sizeof(inotify_event) + pevent->len > event_len) {
      // The pevent->len is too large and buffer will over flow.
      // In general, should not happen, just make more stable.
      return -1;
    }
    offset += sizeof(inotify_event) + pevent->len;

    std::string event_name(pevent->name); // TWRP: inotify len includes NUL padding.
    if (!android::base::StartsWith(event_name, "event")) {
      continue;
    }

    android::base::unique_fd dfd(openat(dirfd(dir.get()), event_name.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    if (dfd == -1) {
      break;
    }

    if (!should_add_input_device(dfd, g_allow_touch_inputs)) {
      continue;
    }

    // TWRP: hotplugged nodes remain input devices for sync/enumeration APIs.
    add_fd(std::move(dfd), g_saved_input_cb, true);
  }

  return 0;
}

int ev_init(ev_callback input_cb, bool allow_touch_inputs) {
  ev_exit(); // TWRP: repeated initialization releases callbacks and devices.

  android::base::unique_fd epoll_fd(epoll_create1(EPOLL_CLOEXEC));
  if (epoll_fd == -1) {
    return -1;
  }

  android::base::unique_fd inotify_fd(inotify_init1(IN_CLOEXEC | IN_NONBLOCK));
  if (inotify_fd.get() == -1) {
    return -1;
  }

  if (inotify_add_watch(inotify_fd, INPUT_DEV_DIR, IN_CREATE) < 0) {
    return -1;
  }

  std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(INPUT_DEV_DIR), closedir);
  if (!dir) {
    return -1;
  }

  g_epoll_fd = std::move(epoll_fd); // TWRP: shared registration path for discovery/hotplug.
  bool epoll_ctl_failed = false;
  dirent* de;
  while ((de = readdir(dir.get())) != nullptr) {
    if (strncmp(de->d_name, "event", 5)) continue;
    android::base::unique_fd fd(openat(dirfd(dir.get()), de->d_name, O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    if (fd == -1) continue;

    if (!should_add_input_device(fd, allow_touch_inputs)) {
      continue;
    }

    if (add_fd(std::move(fd), input_cb, true)) epoll_ctl_failed = true;
    if (g_ev_dev_count == MAX_DEVICES) break;
  }

  if (epoll_ctl_failed && !g_ev_count) {
    return -1;
  }


  g_saved_input_cb = input_cb;
  g_allow_touch_inputs = allow_touch_inputs;
  ev_add_fd(std::move(inotify_fd), inotify_cb);

  return 0;
}

int ev_get_epollfd(void) {
  return g_epoll_fd.get();
}

int ev_add_fd(android::base::unique_fd&& fd, ev_callback cb) {
  return add_fd(std::move(fd), std::move(cb), false); // TWRP: reuse slots after removal.
}

void ev_exit(void) {
  while (g_ev_count > 0) {
    auto& info = ev_fdinfo[--g_ev_count];
    if (info.fd.get() >= 0 && info.input_device) twrp_input_device_removed(info.fd);
    info.fd.reset();
    info.cb = nullptr; // TWRP: callback may capture a released GUI object.
    info.input_device = false;
  }
  g_ev_misc_count = 0;
  g_ev_dev_count = 0;
  g_polled_events_count = 0; // TWRP: discard events from the previous epoll instance.
  g_saved_input_cb = nullptr;
  g_epoll_fd.reset();
}

int ev_wait(int timeout) {
  g_polled_events_count = epoll_wait(g_epoll_fd, g_polled_events, MAX_DEVICES + MAX_MISC_FDS, timeout);
  if (g_polled_events_count <= 0) {
    return -1;
  }
  return 0;
}

void ev_dispatch(void) {
  for (int n = 0; n < g_polled_events_count; n++) {
    FdInfo* fdi = static_cast<FdInfo*>(g_polled_events[n].data.ptr);
    const ev_callback cb = fdi->cb; // TWRP: ev_get_input may remove the owning slot.
    if (cb) {
      cb(fdi->fd, g_polled_events[n].events);
    }
  }
  g_polled_events_count = 0; // TWRP: events are dispatched once.
}

int ev_get_input(int fd, uint32_t epevents, input_event* ev) {
  if (epevents & EPOLLIN) {
    ssize_t r = TEMP_FAILURE_RETRY(read(fd, ev, sizeof(*ev)));
    if (r == sizeof(*ev)) return 0;
    // TWRP: evdev unplug may report ENODEV before EPOLLHUP.
    if (r == 0 || (r < 0 && (errno == ENODEV || errno == EBADF))) {
      remove_fd(fd);
      return -1;
    }
  }
  if (epevents & (EPOLLHUP | EPOLLERR)) {
    remove_fd(fd); // TWRP: close, invalidate device metadata, and permit replug.
  }
  return -1;
}

int ev_sync_sw_state(const ev_set_sw_callback& set_sw_cb) {
  // Use unsigned long to match ioctl's parameter type.
  unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];  // NOLINT
  unsigned long sw_bits[BITS_TO_LONGS(SW_MAX)];  // NOLINT

  for (size_t i = 0; i < g_ev_count; ++i) {
    if (!ev_fdinfo[i].input_device || ev_fdinfo[i].fd.get() < 0) continue; // TWRP: sparse slots.
    memset(ev_bits, 0, sizeof(ev_bits));
    memset(sw_bits, 0, sizeof(sw_bits));

    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
      continue;
    }
    if (!test_bit(EV_SW, ev_bits)) {
      continue;
    }
    if (ioctl(ev_fdinfo[i].fd, EVIOCGSW(sizeof(sw_bits)), sw_bits) == -1) {
      continue;
    }

    for (int code = 0; code <= SW_MAX; code++) {
      if (test_bit(code, sw_bits)) {
        set_sw_cb(code, 1);
      }
    }
  }

  return 0;
}

int ev_sync_key_state(const ev_set_key_callback& set_key_cb) {
  // Use unsigned long to match ioctl's parameter type.
  unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];    // NOLINT
  unsigned long key_bits[BITS_TO_LONGS(KEY_MAX)];  // NOLINT

  for (size_t i = 0; i < g_ev_count; ++i) {
    if (!ev_fdinfo[i].input_device || ev_fdinfo[i].fd.get() < 0) continue; // TWRP: sparse slots.
    memset(ev_bits, 0, sizeof(ev_bits));
    memset(key_bits, 0, sizeof(key_bits));

    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
      continue;
    }
    if (!test_bit(EV_KEY, ev_bits)) {
      continue;
    }
    if (ioctl(ev_fdinfo[i].fd, EVIOCGKEY(sizeof(key_bits)), key_bits) == -1) {
      continue;
    }

    for (int code = 0; code <= KEY_MAX; code++) {
      if (test_bit(code, key_bits)) {
        set_key_cb(code, 1);
      }
    }
  }

  return 0;
}

void ev_iterate_available_keys(const std::function<void(int)>& f) {
  // Use unsigned long to match ioctl's parameter type.
  unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];    // NOLINT
  unsigned long key_bits[BITS_TO_LONGS(KEY_MAX)];  // NOLINT

  for (size_t i = 0; i < g_ev_count; ++i) {
    if (!ev_fdinfo[i].input_device || ev_fdinfo[i].fd.get() < 0) continue; // TWRP: sparse slots.
    memset(ev_bits, 0, sizeof(ev_bits));
    memset(key_bits, 0, sizeof(key_bits));

    // Does this device even have keys?
    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
      continue;
    }
    if (!test_bit(EV_KEY, ev_bits)) {
      continue;
    }

    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(EV_KEY, KEY_MAX), key_bits) == -1) {
      continue;
    }

    for (int key_code = 0; key_code <= KEY_MAX; ++key_code) {
      if (test_bit(key_code, key_bits)) {
        f(key_code);
      }
    }
  }
}

void ev_iterate_touch_inputs(const std::function<void(int)>& action) {
  for (size_t i = 0; i < g_ev_count; ++i) {
    if (!ev_fdinfo[i].input_device || ev_fdinfo[i].fd.get() < 0) continue; // TWRP: sparse slots.
    // Use unsigned long to match ioctl's parameter type.
    unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)] = {};  // NOLINT
    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) == -1) {
      continue;
    }
    if (!test_bit(EV_ABS, ev_bits)) {
      continue;
    }

    unsigned long key_bits[BITS_TO_LONGS(KEY_MAX)] = {};  // NOLINT
    if (ioctl(ev_fdinfo[i].fd, EVIOCGBIT(EV_ABS, KEY_MAX), key_bits) == -1) {
      continue;
    }

    for (int key_code = 0; key_code <= KEY_MAX; ++key_code) {
      if (test_bit(key_code, key_bits)) {
        action(key_code);
      }
    }
  }
}
