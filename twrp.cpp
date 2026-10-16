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

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <condition_variable>
#include <mutex>
#include "gui2/gui2.h"
#include "gui2/backend/twrp_console_backend.h"
#include "gui2/backend/twrp_backup_backend.h"
#include "gui2/backend/twrp_decrypt_backend.h"
#include "gui2/backend/twrp_mount_backend.h"
#include "gui2/backend/twrp_restore_backend.h"
#include "gui2/backend/twrp_file_manager_backend.h"
#include "gui2/backend/twrp_install_backend.h"
#include "gui2/backend/twrp_sideload_backend.h"
#include "gui2/backend/twrp_terminal_backend.h"
#include "gui2/backend/twrp_tools_backend.h"
#ifdef TW_INCLUDE_WIFI
#include "gui2/backend/twrp_wifi_backend.h"
#endif
#include "gui2/backend/twrp_hardware_settings.h"
#include "gui2/backend/backlight_controller.h"
#include "gui2/backend/twrp_log_export_backend.h"
#include "gui2/backend/twrp_reboot_backend.h"
#include "gui2/backend/twrp_screen_backend.h"
#include "gui2/backend/twrp_settings_store.h"
#include "gui2/backend/twrp_wipe_backend.h"
#include "gui2/backend/twrp_background_backend.h"

#include <android-base/properties.h>
#include "twrpminui/minui.h"
#include <android-base/strings.h>
#include <cutils/properties.h>

#include "data.hpp"
#include "openrecoveryscript.hpp"
#include "partitions.hpp"
#include "set_metadata.h"
#include "twcommon.h"
#include "twrp_functions.hpp"
#include "variables.h"
#include "gui/gui.hpp"
#include "gui/objects.hpp"
#include "gui/pages.hpp"
#include "gui/twmsg.h"
#include "recovery_utils/battery_utils.h"
#include "startup/startup_args.hpp"

#ifdef TW_INCLUDE_CRYPTO
#include "FsCrypt.h"
#include "Decrypt.h"
#endif

namespace fs = std::filesystem;

TWPartitionManager PartitionManager;
int Log_Offset;
bool datamedia;


static void MonitorBattery();

static void startBatteryMonitor() {
  std::thread(MonitorBattery).detach();
}

// A small amount of the existing recovery startup path still presents legacy
// pages (for example, decryption and the system read-only prompt).  Keep that
// bootstrap available, then release its graphics/input resources before
// entering GUI2.  This also makes the GUI2 -> legacy handoff use the same
// initialization sequence as a normal legacy startup.
static bool legacy_gui_initialized;

static bool initializeLegacyGui(bool reuse_display = false) {
	if (legacy_gui_initialized)
		return true;

	if ((reuse_display ? gui_init_reuse_display() : gui_init()) != 0)
		return false;

	if (gui_loadResources() != 0) {
		// Do not leave the guard set after a partial initialization. The caller
		// may still need to fall back to the other UI in this process.
		return false;
	}

	PageManager::LoadLanguage(DataManager::GetStrValue("tw_language"));
	GUIConsole::Translate_Now();
	legacy_gui_initialized = true;
	return true;
}

static void shutdownLegacyGui(bool keep_display) {
	if (!legacy_gui_initialized)
		return;

	PageManager::ReleasePackage("TWRP");
	ev_exit();
	if (!keep_display)
		gr_exit();
	legacy_gui_initialized = false;
}


static void ProcessFastbootdMode() {
  LOGINFO("starting fastboot\n");

  if (android::base::GetBoolProperty("ro.boot.dynamic_partitions", false)) {
    PartitionManager.Unmap_Super_Devices();
  }

  gui_msg(Msg("fastboot_console_msg=Entered Fastbootd mode..."));
  // Check for and run startup script if script exists
  TWFunc::CheckAndRunScript("/system/bin/runatboot.sh", "boot");
  TWFunc::CheckAndRunScript("/system/bin/postfastboot.sh", "fastboot");
}


static void legacy_fastbootd_page() {
	if (gui_startPage("fastboot", 1, 1) != 0) {
		LOGERR("Failed to start fastbootd page.\n");
	}
}

// What startup reports to. The defaults are the legacy behaviour; the gui2
// splash overrides them.
class startup_hooks {
  public:
	virtual ~startup_hooks() = default;
	virtual void step(gui2_backend::startup_step) {}
	virtual bool legacy_ui() const { return true; }
	virtual void run_script() { OpenRecoveryScript::Run_OpenRecoveryScript(); }
	virtual void decrypt() {
		if (gui_startPage("decrypt", 1, 1) != 0) {
			LOGERR("Failed to start decrypt GUI page.\n");
		}
	}
	virtual void ask_system_read_only() {
		DataManager::SetValue("tw_back", "main");
		if (gui_startPage("system_readonly", 1, 1) != 0) {
			LOGERR("Failed to start system_readonly GUI page.\n");
		}
	}
};

static void DecryptPage(bool SkipDecryption, bool datamedia, startup_hooks* hooks) {
	// Offer to decrypt if the device is encrypted
	if (DataManager::GetIntValue(TW_IS_ENCRYPTED) != 0) {
		if (SkipDecryption) {
			LOGINFO("Skipping decryption\n");
			PartitionManager.Update_System_Details(true);
		} else if (DataManager::GetIntValue(TW_CRYPTO_PWTYPE) != 0) {
			LOGINFO("Is encrypted, do decrypt page first\n");
			if (DataManager::GetIntValue(TW_IS_FBE))
				DataManager::SetValue("tw_crypto_user_id", "0");
			hooks->decrypt();
		}
	} else if (datamedia) {
		PartitionManager.Update_System_Details(true);
		if (tw_get_default_metadata(DataManager::GetCurrentStoragePath().c_str()) != 0) {
			LOGINFO("Failed to get default contexts and file mode for storage files.\n");
		} else {
			LOGINFO("Got default contexts and file mode for storage files.\n");
		}
	}
}

static void ProcessRecoveryMode(const bool skip_decryption, startup_hooks* hooks) {
  hooks->step(gui2_backend::startup_step::SYSTEM);
  const int crash_counter = std::stoi(android::base::GetProperty("twrp.crash_counter", "-1")) + 1;
  android::base::SetProperty("twrp.crash_counter", std::to_string(crash_counter));

  if (crash_counter == 0) {
    property_list([](const char* key, const char* value, void*) {
      printf("%s=%s\n", key, value);
    }, nullptr);
    printf("\n");
  } else {
    printf("twrp.crash_counter=%d\n", crash_counter);
  }

  // We are doing this here to allow super partition to be set up prior to overriding properties
#if defined(TW_INCLUDE_LIBRESETPROP)
  for (const std::string& prop : std::initializer_list<std::string>{
    "ro.build.date.utc", "ro.bootimage.build.date.utc",
    "ro.vendor.build.date.utc", "ro.system.build.date.utc",
    "ro.system_ext.build.date.utc", "ro.product.build.date.utc",
    "ro.odm.build.date.utc",
  }) {
    TWFunc::OverrideProperty(prop, "0");
    LOGINFO("Overriding %s with value: \"0\"\n", prop.c_str());
  }
#if defined(TW_OVERRIDE_SYSTEM_PROPS)
  stringstream override_props(EXPAND(TW_OVERRIDE_SYSTEM_PROPS));
  string current_prop;

  std::vector<std::string> partition_list;
  partition_list.emplace_back(PartitionManager.Get_Android_Root_Path());
#ifdef TW_OVERRIDE_PROPS_ADDITIONAL_PARTITIONS
  auto additional_partitions = TWFunc::SplitString(TW_OVERRIDE_PROPS_ADDITIONAL_PARTITIONS, " ");
  partition_list.reserve(partition_list.size() + additional_partitions.size());
  for (auto& part : additional_partitions) partition_list.push_back(std::move(part));
#endif
  std::vector<std::string> build_prop_list = { "build.prop" };
#ifdef TW_SYSTEM_BUILD_PROP_ADDITIONAL_PATHS
  auto additional_build_props = TWFunc::SplitString(TW_SYSTEM_BUILD_PROP_ADDITIONAL_PATHS, ";");
  build_prop_list.reserve(build_prop_list.size() + additional_build_props.size());
  for (auto& build_prop : additional_build_props) build_prop_list.push_back(std::move(build_prop));
#endif
  while (getline(override_props, current_prop, ';')) {
    std::string other_prop;
    if (const auto eq_pos = current_prop.find('='); eq_pos != std::string::npos) {
      other_prop = current_prop.substr(eq_pos + 1);
      current_prop = current_prop.substr(0, eq_pos);
    } else {
      other_prop = current_prop;
    }
    other_prop = android::base::Trim(other_prop);
    current_prop = android::base::Trim(current_prop);

    for (const auto& partition_mount_point : partition_list) {
      for (const auto& prop_file : build_prop_list) {
        std::string sys_val = TWFunc::GetPropertyFromPartition(other_prop, PartitionManager,
                                                          partition_mount_point.c_str(), prop_file);
        if (!sys_val.empty()) {
          if (partition_mount_point == "/system_root") {
            LOGINFO("Overriding %s with value: \"%s\" from property %s in /system/%s\n",
                    current_prop.c_str(), sys_val.c_str(), other_prop.c_str(),
                    prop_file.c_str());
          } else {
            LOGINFO("Overriding %s with value: \"%s\" from property %s in /%s/%s\n",
                    current_prop.c_str(), sys_val.c_str(), other_prop.c_str(),
                    partition_mount_point.c_str(), prop_file.c_str());
          }
          int error = TWFunc::OverrideProperty(current_prop, sys_val);
          if (error) {
            LOGERR("Failed overriding property %s, error_code: %d\n", current_prop.c_str(), error);
          }
          if (partition_mount_point == partition_list.back()) {
            PartitionManager.UnMount_By_Path(partition_mount_point, false);
          }
          goto exit;
        } else {
          if (partition_mount_point == "/system_root") {
            LOGINFO("Unable to override property %s: property not found in /system/%s\n",
                    current_prop.c_str(), prop_file.c_str());
          } else {
            LOGINFO("Unable to override property %s: property not found in /%s/%s\n",
                    current_prop.c_str(), partition_mount_point.c_str(), prop_file.c_str());
          }
        }
      }
      PartitionManager.UnMount_By_Path(partition_mount_point, false);
    }
  exit:
    continue;
  }
#endif // defined(TW_OVERRIDE_SYSTEM_PROPS)
#endif // defined(TW_INCLUDE_LIBRESETPROP)

  hooks->step(gui2_backend::startup_step::SCRIPTS);
  // Check for and run startup script if script exists
  TWFunc::CheckAndRunScript("/system/bin/runatboot.sh", "boot");
  TWFunc::CheckAndRunScript("/system/bin/postrecoveryboot.sh", "recovery");

#ifdef TW_INCLUDE_CRYPTO
  android::keystore::syncKeystoreDb();
#endif
  DecryptPage(skip_decryption, datamedia, hooks);

  // Check for and load custom theme if present
  TWFunc::CheckSelinuxSupport();
  if (hooks->legacy_ui()) gui_loadCustomResources();
  PartitionManager.Output_Partition_Logging();

  // Fixup the RTC clock on devices which require it
  if (crash_counter == 0) TWFunc::FixupTimeOnBoot();

  DataManager::LoadTWRPFolderInfo();
  //DataManager::ReadSettingsFile();

  // Run any outstanding OpenRecoveryScript
  std::string cache_dir = TWFunc::GetLogDir();
  if (cache_dir == DATA_LOGS_DIR) cache_dir = "/data/cache";
  if (const std::string ors_file = cache_dir + "/recovery/openrecoveryscript";
    (DataManager::GetIntValue(TW_IS_ENCRYPTED) == 0 || skip_decryption) &&
    (TWFunc::IsPathExists(SCRIPT_FILE_TMP) || TWFunc::IsPathExists(ors_file))) {
    hooks->run_script();
  }

	hooks->step(gui2_backend::startup_step::SERVICES);
#ifdef TW_HAS_MTP
  const std::string mtp_crash_check = android::base::GetProperty("mtp.crash_check", "0");
  const bool try_mtp = DataManager::GetIntValue("tw_mtp_enabled")
      && mtp_crash_check == "0" && !crash_counter
      && (!DataManager::GetIntValue(TW_IS_ENCRYPTED) ||
          DataManager::GetIntValue(TW_IS_DECRYPTED));
  if (try_mtp) {
    android::base::SetProperty("mtp.crash_check", "1");
    LOGINFO("Starting MTP\n");
    if (!PartitionManager.Enable_MTP()) PartitionManager.Disable_MTP();
    else gui_msg("mtp_enabled=MTP Enabled");
    android::base::SetProperty("mtp.crash_check", "0");
  } else if (mtp_crash_check != "0") {
    gui_warn("mtp_crash=MTP Crashed, not starting MTP on boot.");
    DataManager::SetValue("tw_mtp_enabled", 0);
    PartitionManager.Disable_MTP();
  } else if (crash_counter == 1) {
    LOGINFO("TWRP crashed; disabling MTP as a precaution.\n");
    PartitionManager.Disable_MTP();
  }
#endif

  // Check if system has never been changed
  TWPartition* sys = PartitionManager.Find_Partition_By_Path(
      PartitionManager.Get_Android_Root_Path());
  TWPartition* ven = PartitionManager.Find_Partition_By_Path("/vendor");
  if (sys) {
    if (sys->Get_Super_Status()) {
#ifdef TW_INCLUDE_CRYPTO
      std::string recovery_log_dir = std::string(DATA_LOGS_DIR) + "/recovery";
      if (!TWFunc::IsPathExists(recovery_log_dir)) {
        if (!PartitionManager.Recreate_Logs_Dir())
          LOGERR("Unable to create log directory for TWRP\n");
      }
#endif
    } else {
      if (const int mount_system_ro = DataManager::GetIntValue("tw_mount_system_ro");
        (mount_system_ro == 0 && sys->Check_Lifetime_Writes() == 0) || mount_system_ro == 2) {
        if (DataManager::GetIntValue("tw_never_show_system_ro_page") == 0) {
          hooks->ask_system_read_only();
        } else if (mount_system_ro == 0) {
          sys->Change_Mount_Read_Only(false);
          if (ven) ven->Change_Mount_Read_Only(false);
        }
      } else if (mount_system_ro != 1) {
        sys->Change_Mount_Read_Only(false);
        if (ven) ven->Change_Mount_Read_Only(false);
      }
    }
  }

  TWFunc::UpdateLogFile();

  // Disable flashing of stock recovery
  TWFunc::DisableStockRecoveryReplace();
}

static bool run_early_startup(startup_hooks* hooks) {
	hooks->step(gui2_backend::startup_step::PARTITIONS);
	printf("=> Linking mtab\n");
	symlink("/proc/mounts", "/etc/mtab");
	std::string fstab_filename = "/etc/twrp.fstab";
	if (!TWFunc::IsPathExists(fstab_filename)) {
		fstab_filename = "/etc/recovery.fstab";
	}
	printf("=> Processing %s\n", fstab_filename.c_str());
	if (!PartitionManager.Process_Fstab(fstab_filename, 1, true)) {
		LOGERR("Failing out of recovery due to problem with fstab.\n");
		return false;
	}

	hooks->step(gui2_backend::startup_step::STORAGE);
	PartitionManager.Setup_Fstab_Partitions(true);

	hooks->step(gui2_backend::startup_step::SETTINGS);
	DataManager::ReadSettingsFile();
	TWFunc::ClearBootloaderMessage();
	return true;
}

static bool run_fastboot_startup(startup_hooks* hooks) {
	hooks->step(gui2_backend::startup_step::PARTITIONS);
	printf("=> Linking mtab\n");
	symlink("/proc/mounts", "/etc/mtab");
	std::string fstab_filename = "/etc/twrp.fstab";
	if (!TWFunc::IsPathExists(fstab_filename)) {
		fstab_filename = "/etc/recovery.fstab";
	}
	printf("=> Processing %s\n", fstab_filename.c_str());
	if (!PartitionManager.Process_Fstab(fstab_filename, 1, false)) {
		LOGERR("Failing out of recovery due to problem with fstab.\n");
		return false;
	}
#ifdef TW_LOAD_VENDOR_MODULES
	std::vector<std::string> prepareParts = {
		"/system_root",
		"/vendor",
		"/vendor_dlkm",
		"/odm"
	};
	for (auto& preparePart : prepareParts) {
		TWPartition *part = PartitionManager.Find_Partition_By_Path(preparePart);
		if (part) PartitionManager.Prepare_Super_Volume(part);
	}
#endif

	hooks->step(gui2_backend::startup_step::SETTINGS);
	DataManager::ReadSettingsFile();
	TWFunc::ClearBootloaderMessage();

	hooks->step(gui2_backend::startup_step::SCRIPTS);
	ProcessFastbootdMode();
	return true;
}

// Runs the recovery-mode startup on its own thread while gui2 shows the splash.
class twrp_startup_backend final : public gui2_backend::startup_backend, public startup_hooks {
  public:
	twrp_startup_backend(bool skip_decryption, bool fastboot)
		: skip_decryption_(skip_decryption), fastboot_(fastboot) {}
	~twrp_startup_backend() override { finish(); }

	bool started() const { return started_; }

	void start() override {
		started_ = true;
		worker_ = std::thread(&twrp_startup_backend::run, this);
	}

	gui2_backend::startup_status status() override {
		std::lock_guard<std::mutex> lock(mutex_);
		return status_;
	}

	void finish() override {
		{
			// A loop that ends during the prompt must not leave the thread waiting.
			std::lock_guard<std::mutex> lock(mutex_);
			abandoned_ = true;
		}
		answered_.notify_all();
		if (worker_.joinable())
			worker_.join();
	}

	void answer_system_read_only(bool keep_read_only, bool never_show_again) override {
		std::lock_guard<std::mutex> lock(mutex_);
		if (status_.pause != gui2_backend::startup_pause::SYSTEM_READ_ONLY || has_answer_)
			return;
		keep_read_only_ = keep_read_only;
		never_show_again_ = never_show_again;
		has_answer_ = true;
		answered_.notify_all();
	}

	void finish_decrypt() override {
		std::lock_guard<std::mutex> lock(mutex_);
		if (status_.pause != gui2_backend::startup_pause::DECRYPT || has_answer_)
			return;
		has_answer_ = true;
		answered_.notify_all();
	}

	bool can_hide_system_read_only() override {
		return DataManager::GetIntValue(TW_IS_ENCRYPTED) == 0;
	}

	std::string device_label() override {
		const std::string device = android::base::GetProperty("ro.product.device", "");
		const std::string model = android::base::GetProperty("ro.product.model", "");
		if (device.empty() || model.empty())
			return device + model;
		return device + " · " + model;
	}

	void step(gui2_backend::startup_step step) override {
		std::lock_guard<std::mutex> lock(mutex_);
		status_.step = step;
	}

	bool legacy_ui() const override { return false; }

	void run_script() override {
		set_pause(gui2_backend::startup_pause::SCRIPT);
		OpenRecoveryScript::Run_OpenRecoveryScript_Action();
		set_pause(gui2_backend::startup_pause::NONE);
	}

	void decrypt() override {
		std::unique_lock<std::mutex> lock(mutex_);
		status_.pause = gui2_backend::startup_pause::DECRYPT;
		has_answer_ = false;
		answered_.wait(lock, [this] { return has_answer_ || abandoned_; });
		status_.pause = gui2_backend::startup_pause::NONE;
	}

	// The system_readonly page: its checkbox, then mountsystemtoggle.
	void ask_system_read_only() override {
		std::unique_lock<std::mutex> lock(mutex_);
		status_.pause = gui2_backend::startup_pause::SYSTEM_READ_ONLY;
		has_answer_ = false;
		answered_.wait(lock, [this] { return has_answer_ || abandoned_; });
		status_.pause = gui2_backend::startup_pause::NONE;
		if (!has_answer_)
			return;
		const bool keep = keep_read_only_;
		const bool never_show = never_show_again_;
		lock.unlock();

		DataManager::SetValue("tw_never_show_system_ro_page", never_show ? 1 : 0);
		gui2_backend::mountsystemtoggle(keep ? "1" : "0");
	}

  private:
	void set_pause(gui2_backend::startup_pause pause) {
		std::lock_guard<std::mutex> lock(mutex_);
		status_.pause = pause;
	}

	void run() {
		if (fastboot_) {
			if (!run_fastboot_startup(this)) {
				std::lock_guard<std::mutex> lock(mutex_);
				status_.failed = true;
				return;
			}
			step(gui2_backend::startup_step::DONE);
			return;
		}
		if (!run_early_startup(this)) {
			std::lock_guard<std::mutex> lock(mutex_);
			status_.failed = true;
			return;
		}
		ProcessRecoveryMode(skip_decryption_, this);

		// gui2 lists partitions by Display_Name, which the legacy language files
		// translate.
		step(gui2_backend::startup_step::FINISHING);
		PageManager::TranslatePartitionNames(DataManager::GetStrValue("tw_language"));
		step(gui2_backend::startup_step::DONE);
	}

	bool skip_decryption_;
	bool fastboot_;
	bool started_ = false;
	std::thread worker_;
	std::mutex mutex_;
	std::condition_variable answered_;
	gui2_backend::startup_status status_;
	bool has_answer_ = false;
	bool keep_read_only_ = true;
	bool never_show_again_ = false;
	bool abandoned_ = false;
};

static void Reboot() {
  gui_msg(Msg("rebooting=Rebooting..."));
  TWFunc::UpdateLogFile();
  std::string reboot_arg;
  DataManager::GetValue("tw_reboot_arg", reboot_arg);
  if (reboot_arg == "recovery") TWFunc::TwReboot(RECOVERY);
  else if (reboot_arg == "poweroff") TWFunc::TwReboot(POWER_OFF);
  else if (reboot_arg == "bootloader") TWFunc::TwReboot(BOOTLOADER);
  else if (reboot_arg == "download") TWFunc::TwReboot(DOWNLOAD);
  else if (reboot_arg == "edl") TWFunc::TwReboot(EDL);
  else if (reboot_arg == "fastboot") TWFunc::TwReboot(FSATBOOTD);
  else TWFunc::TwReboot(SYSTEM);
}

static constexpr auto kBatteryPollInterval = std::chrono::milliseconds(250);

#ifdef TW_USE_LEGACY_BATTERY_SERVICES
static constexpr int kBatteryHiddenSentinelLow = 0;
static constexpr int kBatteryHiddenSentinelHigh = 101;

static BatteryInfo ReadLegacyBattery() {
#ifdef TW_CUSTOM_BATTERY_PATH
  const auto battery_path = fs::path(TW_CUSTOM_BATTERY_PATH);
#else
  const auto battery_path = fs::path("/sys/class/power_supply/battery");
#endif

  const std::string cap_path = battery_path / "capacity";
  const std::string st_path = battery_path / "status";

  BatteryInfo battery_info{};

  // Sysfs capacity: single integer (0–100), newline-terminated
  if (std::ifstream f(cap_path); f) {
    if (std::string line; std::getline(f, line)) {
      const int v = std::stoi(line);
      battery_info.capacity = v > 100
                                ? kBatteryHiddenSentinelHigh
                                : v < 0 ? kBatteryHiddenSentinelLow : v;
    }
  }

  // Sysfs status: "Charging", "Discharging", "Full", etc.
  if (std::ifstream f(st_path); f) {
    std::string line;
    battery_info.charging = std::getline(f, line) && line == "Charging";
  }

  return battery_info;
}
#endif

static void MonitorBattery() {
  for (;;) {
#ifdef TW_USE_LEGACY_BATTERY_SERVICES
    const auto [charging, capacity] = ReadLegacyBattery();
#else
    const auto [charging, capacity] = GetBatteryInfo();
#endif
    DataManager::SetValue("tw_battery", std::format("{}%{}", capacity, charging ? "+" : ""));
    std::this_thread::sleep_for(kBatteryPollInterval);
  }
}

int main(int argc, char* argv[]) {
  // Recovery needs to install world-readable files, so clear umask
  // set by init
  umask(0);
  Log_Offset = 0;

  // Set up temporary log file (/tmp/recovery.log)
  freopen(TMP_LOG_FILE, "a", stdout);
  setbuf(stdout, nullptr);
  freopen(TMP_LOG_FILE, "a", stderr);
  setbuf(stderr, nullptr);

  signal(SIGPIPE, SIG_IGN);

  // Handle ADB sideload
  if (argc == 3 && strcmp(argv[1], "--adbd") == 0) {
    android::base::SetProperty("ctl.stop", "adbd");
    return 0;
  }

#ifdef RECOVERY_SDCARD_ON_DATA
  datamedia = true;
#endif

  android::base::SetProperty("ro.twrp.boot", "1");
  android::base::SetProperty("ro.twrp.version", TWFunc::GetTwrpVersion());

#ifdef TARGET_OTA_ASSERT_DEVICE
  android::base::SetProperty("ro.twrp.target.devices", TARGET_OTA_ASSERT_DEVICE);
#endif

  const time_t startup_time = time(nullptr);
  printf("Starting TWRP %s-%s on %s (pid %d)\n", TWFunc::GetTwrpVersion().c_str(), TW_GIT_REVISION,
         ctime(&startup_time), getpid());

	// Load default values to set DataManager constants and handle ifdefs
	DataManager::SetDefaultValues();
	StartupArgs startup;
	startup.Parse(&argc, &argv);
	android::base::SetProperty(TW_FASTBOOT_MODE_PROP, startup.GetFastbootMode() ? "1" : "0");

	printf("Starting the UI...\n");
	const bool fastboot = startup.GetFastbootMode();
	twrp_startup_backend startup_backend(startup.ShouldSkipDecryption(), fastboot);
	// The default until startup reads the settings; ReadSettingsFile sets it again.
	TWFunc::SetBrightness(DataManager::GetStrValue("tw_brightness"));

	gui2_backend::twrp_settings_store settings_store;
	gui2_backend::backlight_controller backlight([](int value) {
		return TWFunc::SetBrightness(std::to_string(value)) == 0;
	});
	gui2_backend::twrp_hardware_settings hardware_settings(&settings_store, &backlight);
	gui2_backend::twrp_screen_backend screen_backend(&settings_store, &backlight);
	gui2_backend::twrp_reboot_backend reboot_backend;
	gui2_backend::twrp_console_backend console_backend;
	gui2_backend::twrp_log_export_backend log_export_backend(&settings_store);
	gui2_backend::twrp_wipe_backend wipe_backend(&settings_store);
	gui2_backend::twrp_decrypt_backend decrypt_backend;
	gui2_backend::twrp_backup_backend backup_backend;
	gui2_backend::twrp_mount_backend mount_backend;
	gui2_backend::twrp_restore_backend restore_backend;
	gui2_backend::twrp_terminal_backend terminal_backend;
#ifdef TW_INCLUDE_WIFI
	gui2_backend::twrp_wifi_backend wifi_backend;
#endif
	gui2_backend::twrp_file_manager_backend file_manager_backend;
	gui2_backend::twrp_install_backend install_backend;
	gui2_backend::twrp_sideload_backend sideload_backend;
	gui2_backend::twrp_tools_backend tools_backend;
	gui2_backend::twrp_background_backend background_backend;
	gui2_context gui2_context_value;
	gui2_context_value.settings = &settings_store;
	gui2_context_value.hardware = &hardware_settings;
	gui2_context_value.screen = &screen_backend;
	gui2_context_value.reboot = &reboot_backend;
	gui2_context_value.console = &console_backend;
	gui2_context_value.log_export = &log_export_backend;
	gui2_context_value.wipe = &wipe_backend;
	gui2_context_value.decrypt = &decrypt_backend;
	gui2_context_value.backup = &backup_backend;
	gui2_context_value.mount = &mount_backend;
	gui2_context_value.restore = &restore_backend;
	gui2_context_value.terminal = &terminal_backend;
#ifdef TW_INCLUDE_WIFI
	gui2_context_value.wifi = &wifi_backend;
#endif
	gui2_context_value.file_manager = &file_manager_backend;
	gui2_context_value.install = &install_backend;
	gui2_context_value.sideload = &sideload_backend;
	gui2_context_value.tools = &tools_backend;
	gui2_context_value.background = &background_backend;
	gui2_context_value.startup = &startup_backend;
	gui2_context_value.fastboot_mode = fastboot;
	// Started ahead of the GUI, as upstream does.
	startBatteryMonitor();
	const int gui2_result = gui2_start(&gui2_context_value);
	backlight.stop();
	if (gui2_result == GUI2_EXIT_STARTUP_FAILED)
		return -1;

	// GUI2 owns the display and input loop.  A user-requested switch is a
	// process-local handoff; there is deliberately no persistent GUI selector.
	// Initialization failures also fall back to the established GUI so recovery
	// remains usable if a device cannot initialize LVGL or its font resources.
	if (gui2_result == GUI2_EXIT_TO_LEGACY ||
		gui2_result == GUI2_EXIT_INITIALIZATION_FAILED) {
		// gui2 keeps a display it brought up; Qualcomm DRM refuses a second modeset.
		const bool reuse_display = gr_fb_pixel_bytes() > 0;
		if (!startup_backend.started()) {
			startup_hooks legacy_hooks;
			if (!(fastboot ? run_fastboot_startup(&legacy_hooks) : run_early_startup(&legacy_hooks)))
				return -1;
			if (!initializeLegacyGui(reuse_display))
				LOGERR("Unable to initialize the legacy GUI startup path.\n");
			if (!fastboot)
				ProcessRecoveryMode(startup.ShouldSkipDecryption(), &legacy_hooks);
		} else if (!initializeLegacyGui(reuse_display)) {
			LOGERR("Unable to initialize the legacy GUI fallback.\n");
		}
		if (fastboot) {
			legacy_fastbootd_page();
			TWFunc::UpdateIntentFile(startup.GetIntent());
			Reboot();
			return 0;
		}
		gui_start();
	}
	TWFunc::UpdateIntentFile(startup.GetIntent());
	Reboot();

	return 0;
}
