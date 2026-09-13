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

BootMode KernelModuleLoader::Get_Boot_Mode() {
    std::string force_normal_boot;
    std::string twrpfastboot;
    android::fs_mgr::GetBootconfig("androidboot.force_normal_boot", &force_normal_boot);
    android::fs_mgr::GetKernelCmdline("twrpfastboot", &twrpfastboot);

    if (twrpfastboot == "1" && force_normal_boot == "1") return BootMode::RecoveryFastboot;

    if (android::base::GetProperty(TW_FASTBOOT_MODE_PROP, "0") == "1") return BootMode::Fastbootd;

    return BootMode::RecoveryInBoot;
}

bool KernelModuleLoader::Load_Vendor_Modules() {
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
        VENDOR_MODULE_DIR,
        VENDOR_MODULE_DIR / "1.1",
    };
    std::vector ramdisk_module_dirs = { VENDOR_BOOT_MODULE_DIR };

    std::string kernel_version = android::base::GetProperty("ro.kernel.version", "");
    if (!kernel_version.empty()) {
        ramdisk_module_dirs.push_back(VENDOR_BOOT_MODULE_DIR / kernel_version);
#ifndef TW_LOAD_VENDOR_MODULES_EXCLUDE_GKI
        const auto gki = std::format("{}-gki", kernel_version);
        ramdisk_module_dirs.push_back(VENDOR_BOOT_MODULE_DIR / gki);
        vendor_module_dirs.push_back(VENDOR_MODULE_DIR / gki);
#endif
    } else {
        LOGERR("Unable to query kernel for version info\n");
    }

#ifdef TW_LOAD_PREBUILT_MODULES_AT_FIRST
    /* Try ramdisk-provided vendor modules before any mode-specific source. */
    for (const auto& module_dir : vendor_module_dirs) Try_And_Load_Modules(module_dir, true);
#endif

    switch (Get_Boot_Mode()) {
        case BootMode::RecoveryFastboot:
            /* On bootmode the stock kernel is not always present, so try only
             * the TWRP prebuilt modules. */
            for (const auto& module_dir : vendor_module_dirs) Try_And_Load_Modules(
                module_dir, false);
            break;
        case BootMode::Fastbootd:
        case BootMode::RecoveryInBoot:
#ifdef TW_LOAD_VENDOR_BOOT_MODULES
            for (const auto& module_dir : ramdisk_module_dirs) Try_And_Load_Modules(
                module_dir, false);
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
        if (!partition || partition->UnMount(false))
            return;

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

    for (const auto& module_dir : vendor_module_dirs) Try_And_Load_Modules(module_dir, true);

    Try_And_Load_Modules(std::string(VENDOR_DLKM_MODULE_DIR), true);

    android::base::SetProperty(TW_MODULES_MOUNTED_PROP, "true");
    LOGINFO("Finished attempting to load requested kernel modules; "
        "unavailable modules are optional for this device\n");
    return true;
}

bool KernelModuleLoader::Try_And_Load_Modules(std::string module_dir, bool vendor_is_mounted) {
    LOGINFO("Checking directory: %s\n", module_dir.c_str());
    const std::string dest_module_dir = std::format("/tmp{}", module_dir);
    TWFunc::RecursiveMkdir(dest_module_dir);
    if (!Copy_Modules_To_Tmpfs(module_dir)) {
        LOGINFO("Unable to copy modules from %s\n", module_dir.c_str());
        return false;
    }
    if (!Write_Module_List(dest_module_dir)) return false;
    /* When /vendor is not mounted we cannot bind onto it; remap to the
     * ramdisk /lib/modules target instead. */
    if (!vendor_is_mounted && std::string_view{ module_dir } ==
        VENDOR_MODULE_DIR) module_dir = "/lib/modules";
    LOGINFO("mounting %s on %s\n", dest_module_dir.c_str(), module_dir.c_str());
    if (mount(dest_module_dir.c_str(), module_dir.c_str(), nullptr, MS_BIND, nullptr) == 0) {
        Modprobe m({ module_dir }, "modules.load.twrp", false);
        const bool loaded = m.LoadListedModules(false);
        PartitionManager.UnMount_By_Path(module_dir.c_str(), false, MNT_DETACH);
        LOGINFO("libmodprobe processed %d modules from %s\n", m.GetModuleCount(),
                module_dir.c_str());
        return loaded;
    }
    LOGINFO("Unable to mount %s on %s\n", dest_module_dir.c_str(), module_dir.c_str());
    return false;
}

std::vector<std::string> KernelModuleLoader::Skip_Loaded_Kernel_Modules() {
    std::vector<std::string> modules = android::base::Split(TW_LOAD_VENDOR_MODULES, " ");

    /* /sys/module exposes one directory per module already in the kernel
     * (loadable modules loaded by anyone, plus built-in modules), named with
     * the canonical underscore form. Enumerate it directly instead of parsing
     * /proc/modules text; a missing /sys/module falls through to "no dedup"
     * (return the full requested list). */
    std::unordered_set<std::string> loaded;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/module", ec)) loaded.insert(
        entry.path().filename().string());
    LOGINFO("number of modules already in kernel: %zu\n", loaded.size());
    if (loaded.empty()) return modules;

    /* Canonicalize requested filenames the same way libmodprobe does
     * (MakeCanonical): drop any path prefix, strip the ".ko" suffix, then
     * '-' -> '_'. /sys/module names are already canonical, so both sides
     * compare as bare module names. */
    auto canonical = [](std::string name) {
        name = std::filesystem::path{ name }.filename().string();
        if (name.ends_with(".ko")) name.resize(name.size() - 3);
        std::ranges::replace(name, '-', '_');
        LOGINFO("found module to dedupe: %s\n", name.c_str());
        return name;
    };
    std::erase_if(modules, [&loaded, &canonical](const std::string& m) {
        return loaded.contains(canonical(m));
    });
    return modules;
}

bool KernelModuleLoader::Write_Module_List(const std::string& module_dir) {
    std::error_code ec;
    auto dir = std::filesystem::directory_iterator(module_dir, ec);
    if (ec) {
        LOGINFO("Unable to open module directory: %s. Skipping\n", module_dir.c_str());
        return false;
    }

    const auto deduped = Skip_Loaded_Kernel_Modules();
    if (deduped.empty()) {
        LOGINFO("Requested modules are loaded\n");
        return false;
    }
    const std::unordered_set<std::string> wanted(deduped.begin(), deduped.end());

    std::vector<std::string> kernel_modules;
    for (const auto& entry : dir) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file() && entry.path().extension() == ".ko" && wanted.
            contains(name)) kernel_modules.push_back(name);
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
    const auto module_file = std::format("{}/modules.load.twrp", module_dir);
    if (!android::base::WriteStringToFile(contents, module_file)) {
        LOGINFO("Unable to write module list: %s\n", module_file.c_str());
        return false;
    }
    return true;
}

bool KernelModuleLoader::Copy_Modules_To_Tmpfs(const std::string& module_dir) {
    const auto ramdisk_dir = std::format("/tmp{}", module_dir);
    std::error_code ec;

    /* Remove stale regular files left from a previous run. A missing
     * ramdisk_dir is fine: nothing is there to clean up. */
    for (const auto& entry : std::filesystem::directory_iterator(ramdisk_dir, ec)) {
        if (entry.is_regular_file()) std::filesystem::remove(entry.path(), ec);
    }
    ec.clear();

    auto dir = std::filesystem::directory_iterator(module_dir, ec);
    if (ec) {
        LOGINFO("Unable to open module directory: %s. Skipping\n", module_dir.c_str());
        return false;
    }
    for (const auto& entry : dir) {
        if (!entry.is_regular_file()) continue;
        const auto dest = std::filesystem::path(ramdisk_dir) / entry.path().filename();
        if (!std::filesystem::copy_file(entry.path(), dest,
                                        std::filesystem::copy_options::overwrite_existing, ec)) {
            LOGINFO("Unable to copy %s to %s\n", entry.path().c_str(), dest.c_str());
            return false;
        }
        std::filesystem::permissions(dest, std::filesystem::perms::owner_all, ec);
    }
    return true;
}
