#include "kernel_module_loader.hpp"
#include "common.h"
#include "variables.h"
#include <android-base/strings.h>
#include <fstab/fstab.h>

const std::vector<std::string> kernel_modules_requested = android::base::Split(TW_LOAD_VENDOR_MODULES, " ");

BOOT_MODE KernelModuleLoader::Get_Boot_Mode() {
	std::string force_normal_boot, twrpfastboot;
	android::fs_mgr::GetBootconfig("androidboot.force_normal_boot", &force_normal_boot);
	android::fs_mgr::GetKernelCmdline("twrpfastboot", &twrpfastboot);

	if (twrpfastboot == "1" && force_normal_boot == "1")
		return RECOVERY_FASTBOOT_MODE;

	if (android::base::GetProperty(TW_FASTBOOT_MODE_PROP, "0") == "1")
		return FASTBOOTD_MODE;

	return RECOVERY_IN_BOOT_MODE;
}

bool KernelModuleLoader::Load_Vendor_Modules() {
	// check /lib/modules (ramdisk vendor_boot)
	// check /lib/modules/N.N (ramdisk vendor_boot)
	// check /lib/modules/N.N-gki (ramdisk vendor_boot)
	// check /vendor/lib/modules (ramdisk)
	// check /vendor/lib/modules/1.1 (ramdisk prebuilt modules)
	// check /vendor/lib/modules/N.N (vendor mounted)
	// check /vendor/lib/modules/N.N-gki (vendor mounted)
	// check /vendor_dlkm/lib/modules (vendor_dlkm mounted)
	if (android::base::GetBoolProperty(TW_MODULES_MOUNTED_PROP, false)) return true;

	LOGINFO("Attempting to load modules\n");
	std::string vendor_base_dir(VENDOR_MODULE_DIR);
	std::string base_dir(VENDOR_BOOT_MODULE_DIR);
	std::string vendor_dlkm_base_dir(VENDOR_DLKM_MODULE_DIR);
	std::vector<std::string> module_dirs;
	std::vector<std::string> vendor_module_dirs;

	TWPartition* ven = PartitionManager.Find_Partition_By_Path("/vendor");
	TWPartition* ven_dlkm = PartitionManager.Find_Partition_By_Path("/vendor_dlkm");
	vendor_module_dirs.push_back(VENDOR_MODULE_DIR);
	vendor_module_dirs.push_back(vendor_base_dir + "/1.1");

	module_dirs.push_back(base_dir);

	struct utsname uts;
	if (uname(&uts)) {
		LOGERR("Unable to query kernel for version info\n");
	}

	std::string rls(uts.release);
	std::vector<std::string> release = TWFunc::split_string(rls, '.', true);
	module_dirs.push_back(base_dir + "/" + release[0] + "." + release[1]);
#ifndef TW_LOAD_VENDOR_MODULES_EXCLUDE_GKI
	std::string gki = "/" + release[0] + "." + release[1] + "-gki";
	module_dirs.push_back(base_dir + gki);
	vendor_module_dirs.push_back(vendor_base_dir + gki);
#endif

#ifdef TW_LOAD_PREBUILT_MODULES_AT_FIRST
	/* Try ramdisk-provided vendor modules before any mode-specific source. */
	for (auto&& module_dir:vendor_module_dirs) {
		Try_And_Load_Modules(module_dir, true);
	}
#endif

	switch(Get_Boot_Mode()) {
		case RECOVERY_FASTBOOT_MODE:
			/* On bootmode: once, there is not always stock kernel
			 * so try only with twrp prebuilt modules.
			 */
			for (auto&& module_dir:vendor_module_dirs) {
				Try_And_Load_Modules(module_dir, false);
			}
			break;

		case FASTBOOTD_MODE:
		case RECOVERY_IN_BOOT_MODE:
#ifdef TW_LOAD_VENDOR_BOOT_MODULES
			for (auto&& module_dir:module_dirs) {
				Try_And_Load_Modules(module_dir, false);
			}
#endif
			/* In both mode vendor_boot or vendor modules are used
			 * Because Ramdisk is flashed in both.
			 */
			break;
	}

	if (ven) {
		LOGINFO("Checking mounted /vendor\n");
		ven->Mount(true);
	}
	if (ven_dlkm) {
		LOGINFO("Checking mounted /vendor_dlkm\n");
		ven_dlkm->Mount(true);
	}

	for (auto&& module_dir:vendor_module_dirs) {
		Try_And_Load_Modules(module_dir, true);
	}

	Try_And_Load_Modules(vendor_dlkm_base_dir, true);

	if (ven_dlkm)
		ven_dlkm->UnMount(false, MNT_DETACH);

	if (ven) {
		if (!ven->UnMount(false)) {
			TWFunc::killForUseTargetProcess(ven->Get_Mount_Point());
			ven->UnMount(false);
		}
	}

	android::base::SetProperty(TW_MODULES_MOUNTED_PROP, "true");
	LOGINFO("Finished attempting to load requested kernel modules; unavailable modules are optional for this device\n");
	return true;
}

bool KernelModuleLoader::Try_And_Load_Modules(std::string module_dir, bool vendor_is_mounted) {
		LOGINFO("Checking directory: %s\n", module_dir.c_str());
		std::string dest_module_dir;
		dest_module_dir = "/tmp" + module_dir;
		TWFunc::Recursive_Mkdir(dest_module_dir);
		if (!Copy_Modules_To_Tmpfs(module_dir)) {
			LOGINFO("Unable to copy modules from %s\n", module_dir.c_str());
			return false;
		}
		if (!Write_Module_List(dest_module_dir))
			return false;
		if (!vendor_is_mounted && module_dir == "/vendor/lib/modules") {
			module_dir = "/lib/modules";
		}
		LOGINFO("mounting %s on %s\n", dest_module_dir.c_str(), module_dir.c_str());
		if (mount(dest_module_dir.c_str(), module_dir.c_str(), "", MS_BIND, NULL) == 0) {
			Modprobe m({module_dir}, "modules.load.twrp", false);
			m.LoadListedModules(false);
			PartitionManager.UnMount_By_Path(module_dir.c_str(), false, MNT_DETACH);
			LOGINFO("libmodprobe processed %d modules from %s\n", m.GetModuleCount(), module_dir.c_str());
			return true;
		} else {
			LOGINFO("Unable to mount %s on %s\n", dest_module_dir.c_str(), module_dir.c_str());
		}
		return false;
}

std::vector<string> KernelModuleLoader::Skip_Loaded_Kernel_Modules() {
  LOGINFO("[MODULES] -> %s\n", TW_LOAD_VENDOR_MODULES);
  std::vector<string> kernel_modules = kernel_modules_requested;
	std::vector<string> loaded_modules;
	std::string kernel_module_file = "/proc/modules";
	if (TWFunc::read_file(kernel_module_file, loaded_modules) < 0)
		LOGINFO("failed to get loaded kernel modules\n");
	LOGINFO("number of modules loaded by init: %zu\n", loaded_modules.size());
	if (loaded_modules.size() == 0)
		return kernel_modules;
	for (auto&& module_line:loaded_modules) {
		auto module = TWFunc::Split_String(module_line, " ")[0];
		for (auto it = kernel_modules.begin(); it != kernel_modules.end(); ++it) {
			std::string requested = *it;
			for (auto& character : requested) {
				if (character == '-')
					character = '_';
			}
			if (requested == module + ".ko") {
				LOGINFO("found module to dedupe: %s\n", it->c_str());
				kernel_modules.erase(it);
				break;
			}
		}
	}
	return kernel_modules;
}

bool KernelModuleLoader::Write_Module_List(std::string module_dir) {
	DIR* d;
	struct dirent* de;
	std::vector<std::string> kernel_modules;
	d = opendir(module_dir.c_str());
	if (d == nullptr) {
		LOGINFO("Unable to open module directory: %s. Skipping\n", module_dir.c_str());
		return false;
	}
	auto deduped_modules = Skip_Loaded_Kernel_Modules();
	if (deduped_modules.size() == 0) {
		LOGINFO("Requested modules are loaded\n");
		closedir(d);
		return false;
	}
	while ((de = readdir(d)) != nullptr) {
		std::string kernel_module = de->d_name;
		if (!android::base::EndsWith(kernel_module, ".ko"))
			continue;
		for (auto&& requested:deduped_modules) {
			if (kernel_module == requested) {
				kernel_modules.push_back(kernel_module);
				break;
			}
		}
	}
	closedir(d);
	if (kernel_modules.empty()) {
		LOGINFO("No requested modules found in %s\n", module_dir.c_str());
		return false;
	}
	std::string module_file = module_dir + "/modules.load.twrp";
	/* The vector overload appends, so truncate stale lists before writing. */
	if (!TWFunc::write_to_file(module_file, "") ||
			!TWFunc::write_to_file(module_file, kernel_modules)) {
		LOGINFO("Unable to write module list: %s\n", module_file.c_str());
		return false;
	}
	return true;
}

bool KernelModuleLoader::Copy_Modules_To_Tmpfs(std::string module_dir) {
	std::string ramdisk_dir = "/tmp" + module_dir;
	DIR* d;
	struct dirent* de;
	DIR* old = opendir(ramdisk_dir.c_str());
	if (old != nullptr) {
		while ((de = readdir(old)) != nullptr) {
			if (de->d_name[0] == '.')
				continue;
			std::string stale = ramdisk_dir + "/" + de->d_name;
			if (de->d_type == DT_REG)
				remove(stale.c_str());
		}
		closedir(old);
	}
	d = opendir(module_dir.c_str());
	if (d != nullptr) {
		while ((de = readdir(d)) != nullptr) {
			std::string kernel_module = de->d_name;
			if (de->d_type == DT_REG) {
				std::string src =  module_dir + "/" + de->d_name;
				std::string dest = ramdisk_dir + "/" + de->d_name;
				if (TWFunc::copy_file(src, dest, 0700, false) != 0) {
					return false;
				}
			}
		}
		closedir(d);
	} else {
		LOGINFO("Unable to open module directory: %s. Skipping\n", module_dir.c_str());
		return false;
	}
	return true;
}
