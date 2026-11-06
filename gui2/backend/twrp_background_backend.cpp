#include "twrp_background_backend.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "data.hpp"
#include "openrecoveryscript.hpp"
#include "orscmd/orscmd.h"
#include "partitions.hpp"
#include "twcommon.h"
#include "twrp_operation.h"

namespace gui2_backend {
namespace {

// gui.cpp's command listener. The command itself runs on its own thread, and
// ors_command_done comes back from it, hence the lock.
std::mutex ors_mutex;
int ors_read_fd = -1;
FILE* orsout = NULL;
std::atomic<bool> command_active{ false };
std::thread command_thread;

void setup_ors_command() {
  ors_read_fd = -1;

  unlink(ORS_INPUT_FILE);
  if (mkfifo(ORS_INPUT_FILE, 06660) != 0) {
    LOGINFO("Unable to mkfifo %s\n", ORS_INPUT_FILE);
    return;
  }
  unlink(ORS_OUTPUT_FILE);
  if (mkfifo(ORS_OUTPUT_FILE, 06666) != 0) {
    LOGINFO("Unable to mkfifo %s\n", ORS_OUTPUT_FILE);
    unlink(ORS_INPUT_FILE);
    return;
  }

  ors_read_fd = open(ORS_INPUT_FILE, O_RDONLY | O_NONBLOCK);
  if (ors_read_fd < 0) {
    LOGINFO("Unable to open %s\n", ORS_INPUT_FILE);
    unlink(ORS_INPUT_FILE);
    unlink(ORS_OUTPUT_FILE);
  }
}

// callback called after a CLI command was executed
void ors_command_done() {
  std::lock_guard<std::mutex> lock(ors_mutex);
  gui_set_FILE(NULL);
  fclose(orsout);
  orsout = NULL;

  if (DataManager::GetIntValue("tw_page_done") == 0) {
    // The select function will return ready to read and the
    // read function will return errno 19 no such device unless
    // we set everything up all over again.
    close(ors_read_fd);
    setup_ors_command();
  }
}

// GUIAction::twcmd
void twcmd(std::string arg) {
  operation_start("TWRP CLI Command");
  OpenRecoveryScript::Run_CLI_Command(arg.c_str());
  operation_end(0);
  command_active.store(false);
}

void ors_command_read() {
  char command[1024];
  int read_ret = read(ors_read_fd, &command, sizeof(command));

  if (read_ret > 0) {
    command[1022] = '\n';
    command[1023] = '\0';
    LOGINFO("Command '%s' received\n", command);
    orsout = fopen(ORS_OUTPUT_FILE, "w");
    if (!orsout) {
      close(ors_read_fd);
      ors_read_fd = -1;
      LOGINFO("Unable to fopen %s\n", ORS_OUTPUT_FILE);
      unlink(ORS_INPUT_FILE);
      unlink(ORS_OUTPUT_FILE);
      return;
    }
    if (DataManager::GetIntValue("tw_busy") != 0) {
      fputs("Failed, operation in progress\n", orsout);
      LOGINFO("Command cannot be performed, operation in progress.\n");
      fclose(orsout);
      // Legacy leaves orsout dangling here, which stops the listener for good.
      orsout = NULL;
      close(ors_read_fd);
      setup_ors_command();
    } else if ((strlen(command) == 11 && strncmp(command, "dumpstrings", 11) == 0) ||
               (strlen(command) == 11 && strncmp(command, "reloadtheme", 11) == 0) ||
               (strlen(command) > 11 && strncmp(command, "changepage=", 11) == 0)) {
      // These drive the legacy theme, which gui2 does not load.
      fputs("Failed, not supported\n", orsout);
      fclose(orsout);
      orsout = NULL;
      close(ors_read_fd);
      setup_ors_command();
    } else {
      // mirror output messages
      gui_set_FILE(orsout);
      // close orsout and restart listener after command is done
      OpenRecoveryScript::Call_After_CLI_Command(ors_command_done);
      if (command_thread.joinable()) command_thread.join();
      command_active.store(true);
      command_thread = std::thread(twcmd, std::string(command));
    }
  }
}

}  // namespace

twrp_background_backend::~twrp_background_backend() {
  stop();
  if (command_thread.joinable()) command_thread.join();
}

// gui_start: runPages clears tw_page_done, which the pages of startup leave
// set, and the listener comes up.
void twrp_background_backend::start() {
  if (uevent_thread_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(ors_mutex);
    DataManager::SetValue("tw_page_done", 0);
    if (orsout == nullptr) {
      if (ors_read_fd >= 0) close(ors_read_fd);
      setup_ors_command();
    }
  }
  uevent_stop_.store(false);
  uevent_thread_ = std::thread(&twrp_background_backend::uevent_loop, this);
}

void twrp_background_backend::stop() {
  // Join without holding the storage transaction or command lock.
  if (!uevent_thread_.joinable()) return;
  uevent_stop_.store(true);
  uevent_thread_.join();
}

void twrp_background_backend::uevent_loop() {
  auto next_snapshot = std::chrono::steady_clock::now();
  bool listen_uevent = true;
  while (!uevent_stop_.load()) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_snapshot) {
      PartitionManager.ProcessPendingUeventDevices();
      PartitionManager.RefreshPartitionListSnapshots();
      next_snapshot = now + std::chrono::milliseconds(250);
    }
    pollfd uevent = { .fd = listen_uevent ? PartitionManager.uevent_pfd.fd : -1,
                      .events = POLLIN, .revents = 0 };
    // The timeout is what bounds how long stop() waits on an idle device.
    const int ready = ::poll(&uevent, 1, 20);
    if (uevent_stop_.load()) break;
    if (ready < 0) {
      if (errno == EBADF || errno == EINVAL) {
        LOGINFO("uevent socket closed, disabling hotplug polling\n");
        listen_uevent = false;
        continue;
      }
      // Do not spin on an error the socket keeps reporting.
      if (errno != EINTR) usleep(10000);
      continue;
    }
    if (ready == 0) continue;
    // Drain POLLERR too, so ReadUevent can detect overflow and resync.
    if (uevent.revents & (POLLIN | POLLERR | POLLHUP))
      PartitionManager.ReadUevent((uevent.revents & POLLERR) != 0);
    if (uevent.revents & (POLLNVAL | POLLHUP)) {
      LOGINFO("uevent socket is not open, disabling hotplug polling\n");
      listen_uevent = false;
      continue;
    }
    // Avoid spinning on a persistent error with nothing left to drain.
    if (!(uevent.revents & POLLIN) && (uevent.revents & POLLERR)) usleep(1000);
  }
  PartitionManager.ResetPendingUeventDevices();
}

uint64_t twrp_background_backend::partition_list_revision(const std::string& type) {
  return PartitionManager.PartitionListRevision(type);
}

// One pass of runPages' loop body, minus drawing and input.
void twrp_background_backend::poll() {
  // Apply completed background size scans on the GUI thread.
  PartitionManager.ProcessAsyncDataSize();

  std::lock_guard<std::mutex> lock(ors_mutex);
  // orsout is non-NULL while a command is still running; a negative fd means the
  // listener never came up.
  if (ors_read_fd < 0 || orsout) return;

  fd_set fdset;
  FD_ZERO(&fdset);
  FD_SET(ors_read_fd, &fdset);

  timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 1;
  if (select(ors_read_fd + 1, &fdset, NULL, NULL, &timeout) <= 0) return;
  if (FD_ISSET(ors_read_fd, &fdset)) ors_command_read();
}

bool twrp_background_backend::command_running() {
  return command_active.load();
}

}  // namespace gui2_backend
