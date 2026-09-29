#ifndef TWRP_KERNEL_MODULE_LOADER_HPP
#define TWRP_KERNEL_MODULE_LOADER_HPP

#include <filesystem>
#include <string>
#include <vector>

// Base paths probed for kernel modules by TWRP.

// vendor modules (mounted /vendor)
inline const std::filesystem::path kVendorModuleDir = "/vendor/lib/modules";

// vendor_boot ramdisk GKI modules
inline const std::filesystem::path kVendorBootModuleDir = "/lib/modules";

// vendor_dlkm placed modules
inline const std::filesystem::path kVendorDlkmModuleDir = "/vendor_dlkm/lib/modules";

enum class BootMode {
  kRecoveryFastboot = 0,
  kRecoveryInBoot,
  kFastbootd,
};

class KernelModuleLoader {
public:
  // Load maintainer-defined kernel modules in TWRP
  static bool LoadVendorModules();

private:
  // Use libmodprobe to attempt loading kernel modules
  static bool TryAndLoadModules(std::string module_dir, bool vendor_is_mounted);

  // Write list of modules to load from TW_LOAD_VENDOR_MODULES
  static bool WriteModuleList(const std::string& module_dir);

  // Copy modules to ramdisk for loading
  static bool CopyModulesToTmpfs(const std::string& module_dir);

  // Modules already loaded by init that we must not reload
  static std::vector<std::string> SkipLoadedKernelModules();

  // Query the current boot mode
  static BootMode GetBootMode();
};

#endif  // TWRP_KERNEL_MODULE_LOADER_HPP
