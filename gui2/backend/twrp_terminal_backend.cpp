#include "twrp_terminal_backend.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include "twcommon.h"

namespace gui2_backend {

namespace {

// Wide enough that the shell does not wrap lines itself; the page wraps.
constexpr int kColumns = 200;
constexpr int kRows = 60;

}  // namespace

twrp_terminal_backend::~twrp_terminal_backend() {
  stop();
}

bool twrp_terminal_backend::running() {
  if (pid_ <= 0) return false;
  int status = 0;
  // Reap it without blocking; a shell that exited leaves the page able to
  // start a new one.
  if (waitpid(pid_, &status, WNOHANG) == pid_) {
    pid_ = 0;
    if (master_ >= 0) {
      close(master_);
      master_ = -1;
    }
    return false;
  }
  return true;
}

bool twrp_terminal_backend::start() {
  if (running()) return true;

  master_ = getpt();
  if (master_ < 0) {
    LOGERR("gui2 terminal: getpt failed, error %d\n", errno);
    return false;
  }
  if (unlockpt(master_) != 0) {
    LOGERR("gui2 terminal: unlockpt failed, error %d\n", errno);
    close(master_);
    master_ = -1;
    return false;
  }

  const char* slave_name = ptsname(master_);
  if (slave_name == nullptr) {
    LOGERR("gui2 terminal: ptsname failed, error %d\n", errno);
    close(master_);
    master_ = -1;
    return false;
  }
  // Copied before the fork: ptsname returns a static buffer.
  const std::string slave_path = slave_name;

  const pid_t child = fork();
  if (child < 0) {
    LOGERR("gui2 terminal: fork failed, error %d\n", errno);
    close(master_);
    master_ = -1;
    return false;
  }

  if (child == 0) {
    const int slave = open(slave_path.c_str(), O_RDWR);
    close(master_);
    if (slave < 0) _exit(127);
    dup2(slave, 0);
    dup2(slave, 1);
    dup2(slave, 2);
    close(slave);
    setsid();
    // Without a controlling terminal the shell runs without job control and
    // several programs refuse to start.
    ioctl(0, TIOCSCTTY, 1);
    setenv("TERM", "dumb", 1);
    setenv("HOME", "/", 1);
    setenv("PS1", "# ", 1);
    execl("/system/bin/sh", "sh", nullptr);
    _exit(127);
  }

  pid_ = child;
  fcntl(master_, F_SETFL, O_NONBLOCK);

  winsize size = {};
  size.ws_col = kColumns;
  size.ws_row = kRows;
  ioctl(master_, TIOCSWINSZ, &size);
  return true;
}

void twrp_terminal_backend::stop() {
  if (pid_ > 0) {
    kill(pid_, SIGHUP);
    // Give it a moment to go, then insist. Reaping it matters: a shell left
    // unwaited stays a zombie for as long as recovery runs.
    int status = 0;
    bool reaped = false;
    for (int attempt = 0; attempt < 20 && !reaped; ++attempt) {
      if (waitpid(pid_, &status, WNOHANG) == pid_) {
        reaped = true;
        break;
      }
      usleep(10000);
    }
    if (!reaped) {
      kill(pid_, SIGKILL);
      waitpid(pid_, &status, 0);
    }
    pid_ = 0;
  }
  if (master_ >= 0) {
    close(master_);
    master_ = -1;
  }
  pending_.clear();
  escape_.clear();
  in_escape_ = false;
}

std::string twrp_terminal_backend::working_directory() {
  if (pid_ <= 0) return "/";
  char link[64];
  snprintf(link, sizeof(link), "/proc/%d/cwd", pid_);
  char path[PATH_MAX];
  const ssize_t length = readlink(link, path, sizeof(path) - 1);
  if (length <= 0) return "/";
  path[length] = '\0';
  return path;
}

bool twrp_terminal_backend::send_line(const std::string& line) {
  if (!running()) return false;
  const std::string payload = line + "\n";
  return write(master_, payload.data(), payload.size()) ==
         static_cast<ssize_t>(payload.size());
}

bool twrp_terminal_backend::send_byte(char byte) {
  if (!running()) return false;
  return write(master_, &byte, 1) == 1;
}

// A dumb terminal still emits CSI and OSC sequences; drop them rather than
// printing their letters into the output.
void twrp_terminal_backend::append_filtered(const char* data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    const char c = data[i];
    if (in_escape_) {
      escape_.push_back(c);
      const char first = escape_.size() > 1 ? escape_[1] : '\0';
      if (first == '[') {
        // CSI runs until a byte in 0x40..0x7E.
        if (c >= 0x40 && c <= 0x7E) in_escape_ = false;
      } else if (first == ']') {
        // OSC runs until BEL or ST.
        if (c == '\a' || (c == '\\' && escape_.size() > 2 &&
                          escape_[escape_.size() - 2] == '\x1B'))
          in_escape_ = false;
      } else if (escape_.size() >= 2) {
        in_escape_ = false;
      }
      if (!in_escape_) escape_.clear();
      continue;
    }
    if (c == '\x1B') {
      in_escape_ = true;
      escape_.assign(1, c);
      continue;
    }
    if (c == '\r' || c == '\a') continue;
    if (c == '\b') {
      if (!pending_.empty() && pending_.back() != '\n') pending_.pop_back();
      continue;
    }
    pending_.push_back(c);
  }
}

std::string twrp_terminal_backend::take_output() {
  if (master_ >= 0) {
    char buffer[4096];
    for (;;) {
      const ssize_t count = read(master_, buffer, sizeof(buffer));
      if (count <= 0) break;
      append_filtered(buffer, static_cast<size_t>(count));
      if (static_cast<size_t>(count) < sizeof(buffer)) break;
    }
  }
  std::string out;
  out.swap(pending_);
  return out;
}

}  // namespace gui2_backend
