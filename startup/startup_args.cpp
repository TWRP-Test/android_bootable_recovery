/*
	Copyright 2012-2021 TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "startup_args.hpp"

#include <chrono>
#include <thread>

#include <android-base/properties.h>

#include "data.hpp"
#include "get_args.hpp"
#include "gui/gui.hpp"
#include "openrecoveryscript.hpp"
#include "twcommon.h"
#include "variables.h"

void StartupArgs::Parse(const int* argc, char*** argv) {
  const std::vector<std::string> args = Args::GetArgs(argc, argv);

  LOGINFO("Startup Commands: ");
  for (size_t index = 1; index < args.size(); index++) {
    if (!ProcessRecoveryArgs(args, index)) break;
  }
  printf("\n");
}

bool StartupArgs::ProcessRecoveryArgs(const std::vector<std::string>& args, const size_t index) {
  // Returns the substring after the first '=', or an empty view when there is no '='.
  constexpr auto value_after_eq = [](const std::string_view a) -> std::string_view {
    const auto pos = a.find('=');
    return pos != std::string_view::npos ? a.substr(pos + 1) : std::string_view{};
  };

  const std::string_view arg = args[index];

  if (arg.starts_with(kRescueParty)) {
    gui_print("\n\n");
    gui_msg(Msg(msg::kError,
                "rescue_party0=Android Rescue Party trigger! Possible solutions? Either:"));
    gui_msg(Msg(msg::kError, "rescue_party1= 1. Wipe caches, and/or"));
    gui_msg(Msg(msg::kError, "rescue_party2= 2. Format data, and/or"));
    gui_msg(Msg(msg::kError, "rescue_party3= 3. Clean-flash your ROM."));
    gui_print("\n");
    gui_msg(Msg(msg::kError, "rescue_party4=The reported problem is:"));
    gui_print_color("error", " '%s'\n\n",
                    index + 1 < args.size() ? args[index + 1].c_str() : "");
  } else {
    printf("'%s'", args[index].c_str());
  }

  if (arg == kFastboot) {
    fastboot_mode_ = true;
    android::base::SetProperty("sys.usb.config", "none");
    android::base::SetProperty("sys.usb.configfs", "0");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    android::base::SetProperty("sys.usb.configfs", "1");
    android::base::SetProperty("sys.usb.config", "fastboot");
    DataManager::SetValue("tw_enable_adb", false);
    DataManager::SetValue("tw_enable_fastboot", true);
  } else if (arg.starts_with(kUpdatePackage) || arg.starts_with(kSpecialUpdatePackage)) {
    if (const std::string_view value = value_after_eq(arg); value.empty()) {
      LOGERR("argument error specifying zip file\n");
    } else {
      const std::string ors_command = std::format("install {}", value);
      skip_decryption_ = value.starts_with('@');
      if (!OpenRecoveryScript::Insert_ORS_Command(ors_command)) return false;
    }
  } else if (arg.starts_with(kSendIntent)) {
    if (const std::string_view value = value_after_eq(arg); value.empty()) {
      LOGERR("argument error specifying intent file\n");
    } else {
      send_intent_ = value;
    }
  } else if (arg.starts_with(kWipeData)) {
    if (!OpenRecoveryScript::Insert_ORS_Command("wipe data\n")) return false;
  } else if (arg.starts_with(kWipeCache)) {
    if (!OpenRecoveryScript::Insert_ORS_Command("wipe cache\n")) return false;
  } else if (arg.starts_with(kNandroid)) {
    DataManager::SetValue(TW_BACKUP_NAME, gui_parse_text("{@auto_generate}"));
    if (!OpenRecoveryScript::Insert_ORS_Command("backup BSDCAE\n")) return false;
  }
  return true;
}

bool StartupArgs::ShouldSkipDecryption() const {
  return skip_decryption_;
}

const std::string& StartupArgs::GetIntent() const {
  return send_intent_;
}

bool StartupArgs::GetFastbootMode() const {
  return fastboot_mode_;
}
