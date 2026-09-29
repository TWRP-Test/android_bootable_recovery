#include "kernel_module_loader.hpp"

#include <sys/mount.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <unordered_set>
#include <vector>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/scopeguard.h>
#include <android-base/strings.h>
#include <fstab/fstab.h>
#include <modprobe/modprobe.h>

#include "partitions.hpp"
#include "twcommon.h"
#include "twrp_functions.hpp"
#include "variables.h"

namespace fs = std::filesystem;

BootMode KernelModuleLoader::GetBootMode() {
  std::string force_normal_boot;
  std::string twrpfastboot;
  android::fs_mgr::GetBootconfig("androidboot.force_normal_boot", &force_normal_boot);
  android::fs_mgr::GetKernelCmdline("twrpfastboot", &twrpfastboot);

  if (twrpfastboot == "1" && force_normal_boot == "1") return BootMode::kRecoveryFastboot;

  if (android::base::GetProperty(TW_FASTBOOT_MODE_PROP, "0") == "1") return BootMode::kFastbootd;

  return BootMode::kRecoveryInBoot;
}

bool KernelModuleLoader::LoadVendorModules() {
  if (android::base::GetBoolProperty(TW_MODULES_MOUNTED_PROP, false)) return true;

  LOGINFO("Attempting to load modules\n");

  // Probed directories, in priority order. vendor_module_dirs hold the
  // prebuilt/vendor modules; module_dirs hold the vendor_boot ramdisk (GKI)
  // modules.
  //   /lib/modules (ramdisk vendor_boot)
  //   /lib/modules/N.N (ramdisk vendor_boot)
  //   /lib/modules/N.N-gki (ramdisk vendor_boot)
  //   /vendor/lib/modules (ramdisk)
  //   /vendor/lib/modules/1.1 (ramdisk prebuilt modules)
  //   /vendor/lib/modules/N.N (vendor mounted)
  //   /vendor/lib/modules/N.N-gki (vendor mounted)
  //   /vendor_dlkm/lib/modules (vendor_dlkm mounted)
  std::vector vendor_module_dirs = {
    kVendorModuleDir,
    kVendorModuleDir / "1.1",
  };
  std::vector ramdisk_module_dirs = { kVendorBootModuleDir };

  if (std::string kernel_version = android::base::GetProperty("ro.kernel.version", "");
    !kernel_version.empty()) {
    ramdisk_module_dirs.push_back(kVendorBootModuleDir / kernel_version);
#ifndef TW_LOAD_VENDOR_MODULES_EXCLUDE_GKI
    const auto gki = std::format("{}-gki", kernel_version);
    ramdisk_module_dirs.push_back(kVendorBootModuleDir / gki);
    vendor_module_dirs.push_back(kVendorModuleDir / gki);
#endif
  } else {
    LOGERR("Unable to query kernel for version info\n");
  }

#ifdef TW_LOAD_PREBUILT_MODULES_AT_FIRST
  /* Try ramdisk-provided vendor modules before any mode-specific source. */
  for (const auto& module_dir : vendor_module_dirs) TryAndLoadModules(module_dir, true);
#endif

  switch (GetBootMode()) {
    case BootMode::kRecoveryFastboot:
      /* On bootmode the stock kernel is not always present, so try only
       * the TWRP prebuilt modules. */
      for (const auto& module_dir : vendor_module_dirs) TryAndLoadModules(module_dir, false);
      break;
    case BootMode::kFastbootd:
    case BootMode::kRecoveryInBoot:
#ifdef TW_LOAD_VENDOR_BOOT_MODULES
      for (const auto& module_dir : ramdisk_module_dirs) TryAndLoadModules(module_dir, false);
#endif
      /* In both modes vendor_boot or vendor modules are used, because the
       * ramdisk is flashed in both. */
      break;
  }

  TWPartition* ven = PartitionManager.Find_Partition_By_Path("/vendor");
  TWPartition* ven_dlkm = PartitionManager.Find_Partition_By_Path("/vendor_dlkm");
  if (ven) {
    LOGINFO("Checking mounted /vendor\n");
    ven->Mount(true);
  }
  if (ven_dlkm) {
    LOGINFO("Checking mounted /vendor_dlkm\n");
    ven_dlkm->Mount(true);
  }

  auto unmount_with_kill = [](TWPartition* partition, int fallback_flags) {
    if (!partition || partition->UnMount(false)) return;

    const std::string mount_point = partition->Get_Mount_Point();
    TWFunc::KillForUseTargetProcess(mount_point);
    if (!partition->UnMount(false, fallback_flags)) {
      LOGERR("Unable to unmount '%s' after killing processes\n", mount_point.c_str());
    }
  };

  /* Always release whatever we mounted, even on an early return below. */
  auto mount_guard = android::base::make_scope_guard([&] {
    unmount_with_kill(ven_dlkm, MNT_DETACH);
    unmount_with_kill(ven, 0);
  });

  for (const auto& module_dir : vendor_module_dirs) TryAndLoadModules(module_dir, true);

  TryAndLoadModules(std::string(kVendorDlkmModuleDir), true);

  android::base::SetProperty(TW_MODULES_MOUNTED_PROP, "true");
  LOGINFO("Finished attempting to load requested kernel modules; "
      "unavailable modules are optional for this device\n");
  return true;
}

bool KernelModuleLoader::TryAndLoadModules(std::string module_dir, const bool vendor_is_mounted) {
  LOGINFO("Checking directory: %s\n", module_dir.c_str());
  const std::string dest_module_dir = fs::path("/tmp") / fs::path(module_dir).relative_path();
  TWFunc::RecursiveMkdir(dest_module_dir);
  if (!CopyModulesToTmpfs(module_dir)) {
    LOGINFO("Unable to copy modules from %s\n", module_dir.c_str());
    return false;
  }
  if (!WriteModuleList(dest_module_dir)) return false;
  /* When /vendor is not mounted we cannot bind onto it; remap to the
   * ramdisk /lib/modules target instead. */
  if (!vendor_is_mounted && std::string_view{ module_dir } ==
      kVendorModuleDir) module_dir = kVendorBootModuleDir;
  LOGINFO("mounting %s on %s\n", dest_module_dir.c_str(), module_dir.c_str());
  if (mount(dest_module_dir.c_str(), module_dir.c_str(), nullptr, MS_BIND, nullptr) == 0) {
    Modprobe modprobe({ module_dir }, "modules.load.twrp", false);
    const bool loaded = modprobe.LoadListedModules(false);
    PartitionManager.UnMount_By_Path(module_dir, false, MNT_DETACH);
    LOGINFO("libmodprobe processed %d modules from %s\n", modprobe.GetModuleCount(),
            module_dir.c_str());
    return loaded;
  }
  LOGINFO("Unable to mount %s on %s\n", dest_module_dir.c_str(), module_dir.c_str());
  return false;
}

std::vector<std::string> KernelModuleLoader::SkipLoadedKernelModules() {
  std::vector<std::string> modules = android::base::Split(TW_LOAD_VENDOR_MODULES, " ");

  /* /sys/module exposes one directory per module already in the kernel
   * (loadable modules loaded by anyone, plus built-in modules), named with
   * the canonical underscore form. Enumerate it directly instead of parsing
   * /proc/modules text; a missing /sys/module falls through to "no dedup"
   * (return the full requested list). */
  std::unordered_set<std::string> loaded;
  for (std::error_code ec; const auto& entry : fs::directory_iterator("/sys/module", ec)) {
    loaded.insert(entry.path().filename().string());
  }
  LOGINFO("number of modules already in kernel: %zu\n", loaded.size());
  if (loaded.empty()) return modules;

  /* Canonicalize requested filenames the same way libmodprobe does
   * (MakeCanonical): drop any path prefix, strip the ".ko" suffix, then
   * '-' -> '_'. /sys/module names are already canonical, so both sides
   * compare as bare module names. */
  auto canonicalize = [](const fs::path& name) {
    std::string stem = name.stem();
    std::ranges::replace(stem, '-', '_');
    LOGINFO("found module to dedupe: %s\n", stem.c_str());
    return stem;
  };
  std::erase_if(modules, [&loaded, &canonicalize](const std::string& m) {
    return loaded.contains(canonicalize(m));
  });
  return modules;
}

bool KernelModuleLoader::WriteModuleList(const std::string& module_dir) {
  std::error_code ec;
  auto dir = fs::directory_iterator(module_dir, ec);
  if (ec) {
    LOGINFO("Unable to open module directory: %s. Skipping\n", module_dir.c_str());
    return false;
  }

  const auto deduped = SkipLoadedKernelModules();
  if (deduped.empty()) {
    LOGINFO("Requested modules are loaded\n");
    return false;
  }
  const std::unordered_set wanted(deduped.begin(), deduped.end());

  std::vector<std::string> kernel_modules;
  for (const auto& entry : dir) {
    if (const auto name = entry.path().filename().string();
      entry.is_regular_file() && entry.path().extension() == ".ko" && wanted.contains(name)) {
      kernel_modules.push_back(name);
    }
  }
  if (kernel_modules.empty()) {
    LOGINFO("No requested modules found in %s\n", module_dir.c_str());
    return false;
  }

  /* libmodprobe reads one module name per line. WriteStringToFile truncates
   * the file, replacing the old truncate-then-append sequence. */
  std::string contents;
  for (const auto& name : kernel_modules) {
    contents += name;
    contents += '\n';
  }
  const auto module_file = fs::path(module_dir) / "modules.load.twrp";
  if (!android::base::WriteStringToFile(contents, module_file)) {
    LOGINFO("Unable to write module list: %s\n", module_file.c_str());
    return false;
  }
  return true;
}

bool KernelModuleLoader::CopyModulesToTmpfs(const std::string& module_dir) {
  const auto ramdisk_dir = fs::path("/tmp") / fs::path(module_dir).relative_path();
  std::error_code ec;

  /* Remove stale regular files left from a previous run. A missing
   * ramdisk_dir is fine: nothing is there to clean up. */
  for (const auto& entry : fs::directory_iterator(ramdisk_dir, ec)) {
    if (entry.is_regular_file()) fs::remove(entry.path(), ec);
  }
  ec.clear();

  auto dir = fs::directory_iterator(module_dir, ec);
  if (ec) {
    LOGINFO("Unable to open module directory: %s. Skipping\n", module_dir.c_str());
    return false;
  }
  for (const auto& entry : dir) {
    if (!entry.is_regular_file()) continue;
    const auto dest = fs::path(ramdisk_dir) / entry.path().filename();
    if (!fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec)) {
      LOGINFO("Unable to copy %s to %s\n", entry.path().c_str(), dest.c_str());
      return false;
    }
    fs::permissions(dest, fs::perms::owner_all, ec);
  }
  return true;
}
