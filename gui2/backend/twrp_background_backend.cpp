#include "twrp_background_backend.h"

#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "data.hpp"
#include "gui/pages.hpp"
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
// twrpAdbBuFifo shows action_page while an adb backup or restore streams, and
// main when it is over.
std::atomic<bool> adb_backup_active{ false };

void page_changed(const std::string& page) {
  if (page == "action_page")
    adb_backup_active.store(true);
  else if (page == "main")
    adb_backup_active.store(false);
}
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
  if (command_thread.joinable()) command_thread.join();
}

// gui_start: runPages clears tw_page_done, which the pages of startup leave
// set, and the listener comes up.
void twrp_background_backend::start() {
  std::lock_guard<std::mutex> lock(ors_mutex);
  DataManager::SetValue("tw_page_done", 0);
  setup_ors_command();
  gui_set_page_change_hook(page_changed);
}

// One pass of runPages' loop body, minus drawing and input.
void twrp_background_backend::poll() {
  // Apply completed background size scans on the GUI thread.
  PartitionManager.Process_Async_Data_Size();

  std::lock_guard<std::mutex> lock(ors_mutex);
  fd_set fdset;
  FD_ZERO(&fdset);
  int select_fd = 0;
  if (PartitionManager.uevent_pfd.fd > 0) {
    FD_SET(PartitionManager.uevent_pfd.fd, &fdset);
    select_fd = std::max(select_fd, PartitionManager.uevent_pfd.fd + 1);
  }
  if (ors_read_fd > 0 && !orsout) {  // orsout is non-NULL if a command is still running
    FD_SET(ors_read_fd, &fdset);
    select_fd = std::max(select_fd, ors_read_fd + 1);
  }
  if (select_fd == 0) return;

  timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 1;
  if (select(select_fd, &fdset, NULL, NULL, &timeout) <= 0) return;
  if (PartitionManager.uevent_pfd.fd > 0 && FD_ISSET(PartitionManager.uevent_pfd.fd, &fdset))
    PartitionManager.read_uevent();
  if (ors_read_fd > 0 && !orsout && FD_ISSET(ors_read_fd, &fdset)) ors_command_read();
}

bool twrp_background_backend::command_running() {
  return command_active.load() || adb_backup_active.load();
}

}  // namespace gui2_backend
