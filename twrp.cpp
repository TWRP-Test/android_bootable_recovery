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

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include "recovery_utils/battery_utils.h"
#include "gui/twmsg.h"
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
#ifdef TW_INCLUDE_WIFI
#include "gui2/backend/twrp_wifi_backend.h"
#endif
#include "gui2/backend/twrp_hardware_settings.h"
#include "gui2/backend/twrp_log_export_backend.h"
#include "gui2/backend/twrp_reboot_backend.h"
#include "gui2/backend/twrp_screen_backend.h"
#include "gui2/backend/twrp_settings_store.h"
#include "gui2/backend/twrp_wipe_backend.h"

#include "cutils/properties.h"
#include <android-base/properties.h>

#ifdef ANDROID_RB_RESTART
#include "cutils/android_reboot.h"
#else
#include <sys/reboot.h>
#endif

extern "C" {
#include "gui/gui.h"
}
#include "set_metadata.h"
#include "gui/gui.hpp"
#include "gui/pages.hpp"
#include "gui/objects.hpp"
#include "twrpminui/minui.h"
#include "twcommon.h"
#include "twrp-functions.hpp"
#include "data.hpp"

#include "partitions.hpp"
#ifdef __ANDROID_API_N__
#include <android-base/strings.h>
#else
#include <base/strings.h>
#endif
#include "openrecoveryscript.hpp"
#include "variables.h"
#include "startupArgs.hpp"
#include "twrpAdbBuFifo.hpp"
#ifdef TW_USE_NEW_MINADBD
// #include "minadbd/minadbd.h"
#else
extern "C" {
#include "minadbd21/adb.h"
}
#endif

#ifdef TW_INCLUDE_CRYPTO
#include "FsCrypt.h"
#include "Decrypt.h"
#endif

//extern int adb_server_main(int is_daemon, int server_port, int /* reply_fd */);

TWPartitionManager PartitionManager;
int Log_Offset;
bool datamedia;

static void monitorBatteryInBackground() {
	static char charging = ' ';
	static int lastVal = -1;
	while (true) {
#ifdef TW_USE_LEGACY_BATTERY_SERVICES
		char cap_s[4] = {};
#ifdef TW_CUSTOM_BATTERY_PATH
		string capacity_file = EXPAND(TW_CUSTOM_BATTERY_PATH);
		capacity_file += "/capacity";
		FILE * cap = fopen(capacity_file.c_str(), "rt");
#else
		FILE * cap = fopen("/sys/class/power_supply/battery/capacity", "rt");
#endif
		if (cap) {
			if (fgets(cap_s, sizeof(cap_s), cap)) {
				lastVal = atoi(cap_s);
				if (lastVal > 100) lastVal = 101;
				if (lastVal < 0) lastVal = 0;
			}
			fclose(cap);
		}
#ifdef TW_CUSTOM_BATTERY_PATH
		string status_file = EXPAND(TW_CUSTOM_BATTERY_PATH);
		status_file += "/status";
		cap = fopen(status_file.c_str(), "rt");
#else
		cap = fopen("/sys/class/power_supply/battery/status", "rt");
#endif
		if (cap) {
			if (fgets(cap_s, 2, cap))
				charging = cap_s[0] == 'C' ? '+' : ' ';
			fclose(cap);
		}
#else
		auto battery_info = GetBatteryInfo();
		charging = battery_info.charging ? '+' : ' ';
		lastVal = battery_info.capacity;
#endif
		DataManager::SetValue("tw_battery", std::to_string(lastVal) + "%" + charging);
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

static void startLegacyBatteryMonitor() {
	static std::thread battery_monitor(monitorBatteryInBackground);
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

static void Print_Prop(const char *key, const char *name, void *cookie) {
	printf("%s=%s\n", key, name);
}

// Only reached when GUI2 could not take over: it owns the unlock flow and shows
// its own page, so running this during startup would hide it behind the legacy
// one.
static void Legacy_Decrypt_Page(void) {
	if (DataManager::GetIntValue(TW_IS_ENCRYPTED) == 0) return;
	if (DataManager::GetIntValue(TW_CRYPTO_PWTYPE) == 0) return;

	LOGINFO("Is encrypted, do decrypt page first\n");
	if (DataManager::GetIntValue(TW_IS_FBE))
		DataManager::SetValue("tw_crypto_user_id", "0");
	if (gui_startPage("decrypt", 1, 1) != 0) {
		LOGERR("Failed to start decrypt GUI page.\n");
	}
}

static void Decrypt_Page(bool SkipDecryption, bool datamedia) {
	// Offer to decrypt if the device is encrypted
	if (DataManager::GetIntValue(TW_IS_ENCRYPTED) != 0) {
		if (SkipDecryption) {
			LOGINFO("Skipping decryption\n");
			PartitionManager.Update_System_Details(true);
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

static void process_fastbootd_mode() {
		LOGINFO("starting fastboot\n");

		if (android::base::GetBoolProperty("ro.boot.dynamic_partitions", false)) {
			PartitionManager.Unmap_Super_Devices();
		}

		gui_msg(Msg("fastboot_console_msg=Entered Fastbootd mode..."));
		// Check for and run startup script if script exists
		TWFunc::check_and_run_script("/system/bin/runatboot.sh", "boot");
		TWFunc::check_and_run_script("/system/bin/postfastboot.sh", "fastboot");
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
	virtual void ask_system_read_only() {
		DataManager::SetValue("tw_back", "main");
		if (gui_startPage("system_readonly", 1, 1) != 0) {
			LOGERR("Failed to start system_readonly GUI page.\n");
		}
	}
};

static void process_recovery_mode(twrpAdbBuFifo* adb_bu_fifo, bool skip_decryption, startup_hooks* hooks) {
	char crash_prop_val[PROPERTY_VALUE_MAX];
	int crash_counter;

	hooks->step(gui2_backend::startup_step::SYSTEM);

	property_get("twrp.crash_counter", crash_prop_val, "-1");
	crash_counter = atoi(crash_prop_val) + 1;
	snprintf(crash_prop_val, sizeof(crash_prop_val), "%d", crash_counter);
	property_set("twrp.crash_counter", crash_prop_val);

	if (crash_counter == 0) {
		property_list(Print_Prop, NULL);
		printf("\n");
	} else {
		printf("twrp.crash_counter=%d\n", crash_counter);
	}

// We are doing this here to allow super partition to be set up prior to overriding properties
#if defined(TW_INCLUDE_LIBRESETPROP)
	std::vector<std::string> build_date_props = {"ro.build.date.utc", "ro.bootimage.build.date.utc", "ro.vendor.build.date.utc", "ro.system.build.date.utc", "ro.system_ext.build.date.utc", "ro.product.build.date.utc", "ro.odm.build.date.utc"};
	std::string val = "0";
	for (auto prop : build_date_props) {
		TWFunc::Property_Override(prop, val);
		LOGINFO("Overriding %s with value: \"%s\"\n", prop.c_str(), val.c_str());
	}
#if defined(TW_OVERRIDE_SYSTEM_PROPS)
	stringstream override_props(TW_OVERRIDE_SYSTEM_PROPS);
	string current_prop;

	std::vector<std::string> partition_list;
	partition_list.push_back (PartitionManager.Get_Android_Root_Path().c_str());
#ifdef TW_OVERRIDE_PROPS_ADDITIONAL_PARTITIONS
	std::vector<std::string> additional_partition_list = TWFunc::Split_String(TW_OVERRIDE_PROPS_ADDITIONAL_PARTITIONS, " ");
	partition_list.insert(partition_list.end(), additional_partition_list.begin(), additional_partition_list.end());
#endif
	std::vector<std::string> build_prop_list = {"build.prop"};
#ifdef TW_SYSTEM_BUILD_PROP_ADDITIONAL_PATHS
	std::vector<std::string> additional_build_prop_list = TWFunc::Split_String(TW_SYSTEM_BUILD_PROP_ADDITIONAL_PATHS, ";");
	build_prop_list.insert(build_prop_list.end(), additional_build_prop_list.begin(), additional_build_prop_list.end());
#endif
	while (getline(override_props, current_prop, ';')) {
		string other_prop;
		if (current_prop.find("=") != string::npos) {
			other_prop = current_prop.substr(current_prop.find("=") + 1);
			current_prop = current_prop.substr(0, current_prop.find("="));
		} else {
			other_prop = current_prop;
		}
		other_prop = android::base::Trim(other_prop);
		current_prop = android::base::Trim(current_prop);

		for (auto&& partition_mount_point:partition_list) {
			for (auto&& prop_file:build_prop_list) {
				string sys_val = TWFunc::Partition_Property_Get(other_prop, PartitionManager, partition_mount_point.c_str(), prop_file);
				if (!sys_val.empty()) {
					if (partition_mount_point == "/system_root") {
						LOGINFO("Overriding %s with value: \"%s\" from property %s in /system/%s\n", current_prop.c_str(), sys_val.c_str(), other_prop.c_str(),
							prop_file.c_str());
					} else {
						LOGINFO("Overriding %s with value: \"%s\" from property %s in /%s/%s\n", current_prop.c_str(), sys_val.c_str(), other_prop.c_str(),
							partition_mount_point.c_str(), prop_file.c_str());
					}
					int error = TWFunc::Property_Override(current_prop, sys_val);
					if (error) {
						LOGERR("Failed overriding property %s, error_code: %d\n", current_prop.c_str(), error);
					}
					if (partition_mount_point == partition_list.back()) {
						PartitionManager.UnMount_By_Path(partition_mount_point, false);
					}
					goto exit;
				} else {
					if (partition_mount_point == "/system_root") {
						LOGINFO("Unable to override property %s: property not found in /system/%s\n", current_prop.c_str(), prop_file.c_str());
					} else {
						LOGINFO("Unable to override property %s: property not found in /%s/%s\n", current_prop.c_str(), partition_mount_point.c_str(), prop_file.c_str());
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
	TWFunc::check_and_run_script("/system/bin/runatboot.sh", "boot");
	TWFunc::check_and_run_script("/system/bin/postrecoveryboot.sh", "recovery");

#ifdef TW_INCLUDE_CRYPTO
	android::keystore::syncKeystoreDb();
#endif
	Decrypt_Page(skip_decryption, datamedia);

	// Check for and load custom theme if present
	TWFunc::check_selinux_support();
	if (hooks->legacy_ui())
		gui_loadCustomResources();
	PartitionManager.Output_Partition_Logging();

	// Fixup the RTC clock on devices which require it
	if (crash_counter == 0)
		TWFunc::Fixup_Time_On_Boot();

	DataManager::LoadTWRPFolderInfo();
	//DataManager::ReadSettingsFile();

	// Run any outstanding OpenRecoveryScript
	std::string orsFile = TWFunc::get_log_dir() + "recovery/openrecoveryscript";
	if ((DataManager::GetIntValue(TW_IS_ENCRYPTED) == 0 || skip_decryption) && (TWFunc::Path_Exists(SCRIPT_FILE_TMP) || TWFunc::Path_Exists(orsFile))) {
		hooks->run_script();
	}

	hooks->step(gui2_backend::startup_step::SERVICES);
#ifdef TW_HAS_MTP
	char mtp_crash_check[PROPERTY_VALUE_MAX];
	property_get("mtp.crash_check", mtp_crash_check, "0");
	if (DataManager::GetIntValue("tw_mtp_enabled")
			&& !strcmp(mtp_crash_check, "0") && !crash_counter
			&& (!DataManager::GetIntValue(TW_IS_ENCRYPTED) || DataManager::GetIntValue(TW_IS_DECRYPTED))) {
		property_set("mtp.crash_check", "1");
		LOGINFO("Starting MTP\n");
		if (!PartitionManager.Enable_MTP())
			PartitionManager.Disable_MTP();
		else
			gui_msg("mtp_enabled=MTP Enabled");
		property_set("mtp.crash_check", "0");
	} else if (strcmp(mtp_crash_check, "0")) {
		gui_warn("mtp_crash=MTP Crashed, not starting MTP on boot.");
		DataManager::SetValue("tw_mtp_enabled", 0);
		PartitionManager.Disable_MTP();
	} else if (crash_counter == 1) {
		LOGINFO("TWRP crashed; disabling MTP as a precaution.\n");
		PartitionManager.Disable_MTP();
	}
#endif

	// Check if system has never been changed
	TWPartition* sys = PartitionManager.Find_Partition_By_Path(PartitionManager.Get_Android_Root_Path());
	TWPartition* ven = PartitionManager.Find_Partition_By_Path("/vendor");
	if (sys) {
		if (sys->Get_Super_Status()) {
#ifdef TW_INCLUDE_CRYPTO
			std::string recoveryLogDir(DATA_LOGS_DIR);
			recoveryLogDir += "/recovery";
			if (TWFunc::get_log_dir() != CACHE_LOGS_DIR && !TWFunc::Path_Exists(recoveryLogDir)) {
				bool created = PartitionManager.Recreate_Logs_Dir();
				if (!created)
					LOGERR("Unable to create log directory for TWRP\n");
			}
			//DataManager::ReadSettingsFile();
#endif
		} else {
			if ((DataManager::GetIntValue("tw_mount_system_ro") == 0 && sys->Check_Lifetime_Writes() == 0) || DataManager::GetIntValue("tw_mount_system_ro") == 2) {
				if (DataManager::GetIntValue("tw_never_show_system_ro_page") == 0) {
					hooks->ask_system_read_only();
				} else if (DataManager::GetIntValue("tw_mount_system_ro") == 0) {
					sys->Change_Mount_Read_Only(false);
					if (ven)
						ven->Change_Mount_Read_Only(false);
				}
			} else if (DataManager::GetIntValue("tw_mount_system_ro") == 1) {
				// Do nothing, user selected to leave system read only
			} else {
				sys->Change_Mount_Read_Only(false);
				if (ven)
					ven->Change_Mount_Read_Only(false);
			}
		}
	}

	TWFunc::Update_Log_File();

	adb_bu_fifo->threadAdbBuFifo();

	// Disable flashing of stock recovery
	TWFunc::Disable_Stock_Recovery_Replace();
}

static bool run_early_startup(startup_hooks* hooks) {
	hooks->step(gui2_backend::startup_step::PARTITIONS);
	printf("=> Linking mtab\n");
	symlink("/proc/mounts", "/etc/mtab");
	std::string fstab_filename = "/etc/twrp.fstab";
	if (!TWFunc::Path_Exists(fstab_filename)) {
		fstab_filename = "/etc/recovery.fstab";
	}
	printf("=> Processing %s\n", fstab_filename.c_str());
	if (!PartitionManager.Process_Fstab(fstab_filename, 1, true)) {
		LOGERR("Failing out of recovery due to problem with fstab.\n");
		return false;
	}

	hooks->step(gui2_backend::startup_step::STORAGE);
	PartitionManager.Setup_Fstab_Partitions(true);
	if (TWFunc::get_log_dir() == DATA_LOGS_DIR && !TWFunc::Path_Exists(DATA_LOGS_DIR))
		TWFunc::Use_Tmpfs_Cache();

	hooks->step(gui2_backend::startup_step::SETTINGS);
	DataManager::ReadSettingsFile();
	TWFunc::Clear_Bootloader_Message();
	return true;
}

// Runs the recovery-mode startup on its own thread while gui2 shows the splash.
class twrp_startup_backend final : public gui2_backend::startup_backend, public startup_hooks {
  public:
	twrp_startup_backend(twrpAdbBuFifo* adb_bu_fifo, bool skip_decryption)
		: adb_bu_fifo_(adb_bu_fifo), skip_decryption_(skip_decryption) {}
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

	// The same writes the legacy mountsystemtoggle action makes; system is not
	// mounted at this point, so there is nothing to remount.
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
		DataManager::SetValue("tw_mount_system_ro", keep ? 1 : 0);
		TWPartition* sys = PartitionManager.Find_Partition_By_Path(PartitionManager.Get_Android_Root_Path());
		TWPartition* ven = PartitionManager.Find_Partition_By_Path("/vendor");
		if (sys)
			sys->Change_Mount_Read_Only(keep);
		if (ven)
			ven->Change_Mount_Read_Only(keep);
	}

  private:
	void set_pause(gui2_backend::startup_pause pause) {
		std::lock_guard<std::mutex> lock(mutex_);
		status_.pause = pause;
	}

	void run() {
		if (!run_early_startup(this)) {
			std::lock_guard<std::mutex> lock(mutex_);
			status_.failed = true;
			return;
		}
		process_recovery_mode(adb_bu_fifo_, skip_decryption_, this);

		// gui2 lists partitions by Display_Name, which the legacy language files
		// translate.
		step(gui2_backend::startup_step::FINISHING);
		PageManager::TranslatePartitionNames(DataManager::GetStrValue("tw_language"));
		step(gui2_backend::startup_step::DONE);
	}

	twrpAdbBuFifo* adb_bu_fifo_;
	bool skip_decryption_;
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

static void reboot() {
	gui_msg(Msg("rebooting=Rebooting..."));
	TWFunc::Update_Log_File();
	string Reboot_Arg;
	DataManager::GetValue("tw_reboot_arg", Reboot_Arg);
	if (Reboot_Arg == "recovery")
		TWFunc::tw_reboot(rb_recovery);
	else if (Reboot_Arg == "poweroff")
		TWFunc::tw_reboot(rb_poweroff);
	else if (Reboot_Arg == "bootloader")
		TWFunc::tw_reboot(rb_bootloader);
	else if (Reboot_Arg == "download")
		TWFunc::tw_reboot(rb_download);
	else if (Reboot_Arg == "edl")
		TWFunc::tw_reboot(rb_edl);
	else if (Reboot_Arg == "fastboot")
		TWFunc::tw_reboot(rb_fastboot);
	else
		TWFunc::tw_reboot(rb_system);
}

int main(int argc, char **argv) {
	// Recovery needs to install world-readable files, so clear umask
	// set by init
	umask(0);
	Log_Offset = 0;

	// Set up temporary log file (/tmp/recovery.log)
	freopen(TMP_LOG_FILE, "a", stdout);
	setbuf(stdout, NULL);
	freopen(TMP_LOG_FILE, "a", stderr);
	setbuf(stderr, NULL);

	signal(SIGPIPE, SIG_IGN);

	// Handle ADB sideload
	if (argc == 3 && strcmp(argv[1], "--adbd") == 0) {
		property_set("ctl.stop", "adbd");
#ifdef TW_USE_NEW_MINADBD
		//adb_server_main(0, DEFAULT_ADB_PORT, -1); TODO fix this for android8
		// minadbd_main();
#else
		adb_main(argv[2]);
#endif
		return 0;
	}

#ifdef RECOVERY_SDCARD_ON_DATA
	datamedia = true;
#endif

	property_set("ro.twrp.boot", "1");
    property_set("ro.twrp.version", TWFunc::Get_TWRP_Version_Str().c_str());

#ifdef TARGET_OTA_ASSERT_DEVICE
	property_set("ro.twrp.target.devices", TARGET_OTA_ASSERT_DEVICE);
#endif

	time_t StartupTime = time(NULL);
    printf("Starting TWRP %s-%s on %s (pid %d)\n", TWFunc::Get_TWRP_Version_Str().c_str(), TW_GIT_REVISION, ctime(&StartupTime), getpid());

	// Load default values to set DataManager constants and handle ifdefs
	DataManager::SetDefaultValues();
	startupArgs startup;
	startup.parse(&argc, &argv);
	android::base::SetProperty(TW_FASTBOOT_MODE_PROP, startup.Get_Fastboot_Mode() ? "1" : "0");

	if (startup.Get_Fastboot_Mode()) {
		printf("=> Linking mtab\n");
		symlink("/proc/mounts", "/etc/mtab");
		std::string fstab_filename = "/etc/twrp.fstab";
		if (!TWFunc::Path_Exists(fstab_filename)) {
			fstab_filename = "/etc/recovery.fstab";
		}
		printf("=> Processing %s\n", fstab_filename.c_str());
		if (!PartitionManager.Process_Fstab(fstab_filename, 1, false)) {
			LOGERR("Failing out of recovery due to problem with fstab.\n");
			return -1;
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
		printf("Starting the UI...\n");
		if (TWFunc::get_log_dir() == DATA_LOGS_DIR && !TWFunc::Path_Exists(DATA_LOGS_DIR))
			TWFunc::Use_Tmpfs_Cache();
		DataManager::ReadSettingsFile();
		if (!initializeLegacyGui())
			LOGERR("Unable to initialize the legacy GUI startup path.\n");
		TWFunc::Clear_Bootloader_Message();
		process_fastbootd_mode();
		TWFunc::Update_Intent_File(startup.Get_Intent());
		reboot();
		return 0;
	}

	printf("Starting the UI...\n");
	twrpAdbBuFifo *adb_bu_fifo = new twrpAdbBuFifo();
	twrp_startup_backend startup_backend(adb_bu_fifo, startup.Should_Skip_Decryption());
	// The default until startup reads the settings; ReadSettingsFile sets it again.
	TWFunc::Set_Brightness(DataManager::GetStrValue("tw_brightness"));

	gui2_backend::twrp_settings_store settings_store;
	gui2_backend::twrp_hardware_settings hardware_settings(&settings_store);
	gui2_backend::twrp_screen_backend screen_backend(&settings_store);
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
	gui2_context_value.startup = &startup_backend;
	const int gui2_result = gui2_start(&gui2_context_value);
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
			if (!run_early_startup(&legacy_hooks))
				return -1;
			if (!initializeLegacyGui(reuse_display))
				LOGERR("Unable to initialize the legacy GUI startup path.\n");
			process_recovery_mode(adb_bu_fifo, startup.Should_Skip_Decryption(), &legacy_hooks);
		} else if (!initializeLegacyGui(reuse_display)) {
			LOGERR("Unable to initialize the legacy GUI fallback.\n");
		}
		Legacy_Decrypt_Page();
		startLegacyBatteryMonitor();
		gui_start();
	}
	delete adb_bu_fifo;
	TWFunc::Update_Intent_File(startup.Get_Intent());
	reboot();

	return 0;
}
