/*
	Copyright 2012 to 2021 TeamWin
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

#ifndef DATAMANAGER_HPP_HEADER
#define DATAMANAGER_HPP_HEADER

#include <string>
#include <pthread.h>

#include "infomanager.hpp"

class DataManager {
public:
  static int ResetDefaults();

  static int LoadValues(const std::string& filename);

  static int Flush();

  static void LoadTWRPFolderInfo();

  // Core get routines
  static int GetValue(const std::string& key, std::string& value);

  static int GetValue(const std::string& key, int& value);

  static int GetValue(const std::string& key, float& value);

  static int GetValue(const std::string& key, uint64_t& value);

  // Helper functions
  static std::string GetStrValue(const std::string& key);

  static int GetIntValue(const std::string& key);

  // Core set routines
  static int SetValue(const std::string& key, const std::string& value, bool persist = false);

  static int SetValue(const std::string& key, int value, bool persist = false);

  static int SetValue(const std::string& key, float value, bool persist = false);

  static int SetValue(const std::string& key, uint64_t value, bool persist = false);

  // scoped=false (default): legacy absolute mode — claim the full bar, set the
  // fraction, then release the scope. scoped=true: honor the active portion set
  // by ShowProgress (the former _SetProgress, used by the updater set_progress cmd).
  static int SetProgress(float fraction, bool scoped = false);

  static int ShowProgress(float portion, float seconds);

  static void UpdateTimezoneEnvironment();

  static void Vibrate(const std::string& key);

  static void SetBackupFolder();

  static void SetDefaultValues();

  // Outputs the version to a file in the TWRP folder
  static void OutputVersion();

  static void ReadSettingsFile();

  static std::string GetCurrentStoragePath();

  static std::string GetSettingsStoragePath();

  static std::string kBackingFile;

protected:
  static bool initialized_;
  static InfoManager persist_;
  static InfoManager data_;
  static InfoManager consts_;

  static std::map<std::string, std::string> const_values_;

  static int SaveValues();

  static int GetMagicValue(const std::string& key, std::string& value);

private:
  static void SetDeviceId();

  static void HandleBrightnessConfig();

  static pthread_mutex_t values_lock_;
};

#endif // DATAMANAGER_HPP_HEADER