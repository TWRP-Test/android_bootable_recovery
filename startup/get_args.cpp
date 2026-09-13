#include "get_args.hpp"

#include <algorithm>
#include <iterator>
#include <ranges>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "bootloader_message/bootloader_message.h"

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
std::vector<std::string> Args::GetArgs(const int* argc, char*** argv) {
  CHECK_GT(*argc, 0);

  bootloader_message boot = {};

  std::string err;
  if (!read_bootloader_message(&boot, &err)) {
    LOG(ERROR) << err;
    // If fails, leave a zeroed bootloader_message.
    boot = {};
  }

  std::string boot_command;
  if (boot.command[0] != 0) {
    const auto cmd_end = std::ranges::find(boot.command, '\0');
    boot_command.assign(boot.command, cmd_end);
    LOG(INFO) << "Boot command: " << boot_command;
    printf("boot command: %s\n", boot_command.c_str());
  }

  if (boot.status[0] != 0) {
    const auto status_end = std::ranges::find(boot.status, '\0');
    LOG(INFO) << "Boot status: " << std::string(boot.status, status_end);
  }

  std::vector<std::string> args(*argv, *argv + *argc);

  // --- if arguments weren't supplied, look in the bootloader control block
  if (args.size() == 1) {
    boot.recovery[sizeof(boot.recovery) - 1] = '\0'; // Ensure termination
    const std::string boot_recovery(boot.recovery);
    if (std::vector<std::string> tokens = android::base::Split(boot_recovery, "\n");
      !tokens.empty() && tokens[0] == "recovery") {
      for (auto& token : tokens | std::views::drop(1)) {
        // Skip empty and '\0'-filled tokens.
        if (!token.empty() && token[0] != '\0') args.push_back(std::move(token));
      }
      LOG(INFO) << "Got " << args.size() << " arguments from boot message";
    } else if (boot.recovery[0] != 0) {
      LOG(ERROR) << "Bad boot message: \"" << boot_recovery << "\"";
    }
  }

  // Write the arguments (excluding the filename in args[0]) back into the
  // bootloader control block. So the device will always boot into recovery to
  // finish the pending work, until finish_recovery() is called.
  if (const std::vector options(args.cbegin() + 1, args.cend());
    !update_bootloader_message(options, &err)) {
    LOG(ERROR) << "Failed to set BCB message: " << err;
  }

  // Finally, if no arguments were specified, check whether we should boot
  // into fastboot or rescue mode.
  if (args.size() == 1 && boot_command == "boot-fastboot") {
    printf("fastbootd needed\n");
    args.emplace_back("--fastboot");
  } else if (args.size() == 1 && boot_command == "boot-rescue") {
    args.emplace_back("--rescue");
  }

  return args;
}
