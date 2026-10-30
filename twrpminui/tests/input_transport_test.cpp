// TWRP: test minui's real epoll dispatcher with synthetic evdev-sized pipe records.
#include <cassert>
#include <fcntl.h>
#include <linux/input.h>
#include <set>
#include "../events.cpp"

namespace {
std::set<int> devices;
int delivered = 0;
int callbacks = 0;
int callback(int fd, uint32_t events) {
  ++callbacks;
  input_event event;
  if (ev_get_input(fd, events, &event) == 0) {
    assert(event.type == EV_KEY && event.code == KEY_ENTER);
    ++delivered;
  }
  return 0;
}
void send_event(int fd) {
  input_event event{};
  event.type = EV_KEY;
  event.code = KEY_ENTER;
  assert(write(fd, &event, sizeof(event)) == sizeof(event));
}
}
void twrp_input_device_added(int fd) { assert(devices.insert(fd).second); }
void twrp_input_device_removed(int fd) { assert(devices.erase(fd) == 1); }

int main() {
  g_epoll_fd.reset(epoll_create1(EPOLL_CLOEXEC));
  assert(g_epoll_fd.get() >= 0);
  // Replug far beyond the simultaneous-device limit; slots must be reclaimed.
  for (int i = 0; i < 100; ++i) {
    int pipe_fds[2];
    assert(pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) == 0);
    android::base::unique_fd reader(pipe_fds[0]), writer(pipe_fds[1]);
    assert(add_fd(std::move(reader), callback, true) == 0);
    assert(g_ev_dev_count == 1 && devices.size() == 1);
    android::base::unique_fd duplicate(dup(pipe_fds[0]));
    assert(add_fd(std::move(duplicate), callback, true) == 0);
    assert(g_ev_dev_count == 1); // Initial scan/inotify overlap must not duplicate evdev.
    duplicate.reset();
    send_event(writer);
    assert(ev_wait(100) == 0);
    ev_dispatch();
    const int count = callbacks;
    ev_dispatch();
    assert(callbacks == count); // Dispatched events cannot be replayed.
    writer.reset();
    assert(ev_wait(100) == 0);
    ev_dispatch();
    assert(g_ev_dev_count == 0 && devices.empty());
  }
  assert(delivered == 100);
  int pipe_fds[2];
  assert(pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) == 0);
  android::base::unique_fd reader(pipe_fds[0]), writer(pipe_fds[1]);
  assert(ev_add_fd(std::move(reader), callback) == 0);
  assert(g_ev_misc_count == 1 && devices.empty());
  send_event(writer);
  assert(ev_wait(100) == 0);
  ev_dispatch();
  assert(delivered == 101);
  ev_exit();
  assert(g_ev_misc_count == 0 && g_ev_dev_count == 0 && ev_get_epollfd() == -1);
  ev_exit();
  puts("PASS: minui epoll API, 100 disconnect/replug cycles, misc fds, callback lifetime, cleanup");
}
