/*
	Copyright 2012-2020 TeamWin
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

#ifndef STARTUPARGS_HPP
#define STARTUPARGS_HPP

#include <string>
#include <string_view>
#include <vector>

class StartupArgs {
public:
  static constexpr std::string_view kUpdatePackage = "--update_package";
  static constexpr std::string_view kSpecialUpdatePackage = "--special_update_package";
  static constexpr std::string_view kWipeCache = "--wipe_cache";
  static constexpr std::string_view kWipeData = "--wipe_data";
  static constexpr std::string_view kSendIntent = "--send_intent";
  static constexpr std::string_view kFastboot = "--fastboot";
  static constexpr std::string_view kNandroid = "--nandroid";
  static constexpr std::string_view kRescueParty = "--prompt_and_wipe_data";

  void Parse(const int* argc, char*** argv);

  [[nodiscard]] bool ShouldSkipDecryption() const;

  [[nodiscard]] const std::string& GetIntent() const;

  [[nodiscard]] bool GetFastbootMode() const;

  bool ProcessRecoveryArgs(const std::vector<std::string>& args, size_t index);

private:
  bool skip_decryption_ = false;
  bool fastboot_mode_ = false;
  std::string send_intent_;
};
#endif
