#include "twrp_apex.hpp"

#include <fcntl.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <ziparchive/zip_archive.h>

#include "twcommon.h"

namespace fs = std::filesystem;

bool TwrpApex::LoadApexImages() {
  if (!fs::exists(kApexDir)) {
    LOGERR("%s not exists\n", kApexDir.c_str());
    return false;
  }

  // Both the flattened bind mount and the per-apex mount points need /apex to
  // exist; fs::create_directory() below only creates the last component.
  if (std::error_code ec; !fs::create_directories(kApexBase, ec) && ec) {
    LOGERR("Unable to create %s. Reason: %s\n", kApexBase.c_str(), ec.message().c_str());
    return false;
  }

  // A flattened apex tree carries no top-level .apex files; bind it straight in.
  bool has_apex_files = false;
  for (const auto& entry : fs::directory_iterator(kApexDir)) {
    if (entry.is_regular_file()) {
      has_apex_files = true;
      break;
    }
  }
  if (!has_apex_files) {
    LOGINFO("Bind mounting flattened apex directory\n");
    if (mount(kApexDir.c_str(), kApexBase.c_str(), "", MS_BIND, nullptr) < 0) {
      LOGERR("Unable to bind mount flattened apex directory\n");
      return false;
    }
    android::base::SetProperty("twrp.apex.flattened", "true");
    flattened_mounted_ = true;
    return true;
  }

  constexpr std::array<const char*, 6> kDefaultApexFiles{
    "com.android.apex.cts.shim.apex",
    "com.google.android.tzdata2.apex",
    "com.android.tzdata.apex",
    "com.android.art.release.apex",
    "com.google.android.media.swcodec.apex",
    "com.android.media.swcodec.apex",
  };
  std::vector<fs::path> apex_files;
  for (const auto name : kDefaultApexFiles) {
    apex_files.emplace_back(kApexDir / name);
  }
#ifdef TW_ADDITIONAL_APEX_FILES
  for (const auto& name : android::base::Tokenize(EXPAND(TW_ADDITIONAL_APEX_FILES), " ")) {
    apex_files.emplace_back(kApexDir / name);
  }
#endif

  if (!MountApexOnLoopbackDevices(apex_files)) {
    LOGERR("Unable to create loop devices to mount apex files\n");
    return false;
  }
  return true;
}

std::optional<fs::path> TwrpApex::UnzipImage(const fs::path& file) {
  ZipArchiveHandle handle;
  if (const int32_t ret = OpenArchive(file.c_str(), &handle); ret != 0) {
    LOGINFO("unable to open zip archive %s. Reason: %s\n", file.c_str(), ErrorCodeString(ret));
    return std::nullopt;
  }
  // From here CloseArchive() runs automatically at scope exit, so every error
  // path below is a plain return.
  std::unique_ptr<ZipArchive, decltype(&CloseArchive)> archive(handle, &CloseArchive);

  ZipEntry entry;
  if (const int32_t ret = FindEntry(handle, kApexPayload, &entry); ret != 0) {
    LOGERR("unable to find %s in zip: %s\n", kApexPayload.c_str(), ErrorCodeString(ret));
    return std::nullopt;
  }

  fs::path path = fs::path("/tmp") / file.filename();
  const android::base::unique_fd fd(open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666));
  if (fd < 0) {
    LOGERR("unable to create %s. Reason: %s\n", path.c_str(),
           std::system_category().message(errno).c_str());
    return std::nullopt;
  }
  if (const int32_t ret = ExtractEntryToFile(handle, &entry, fd.get()); ret != 0) {
    LOGERR("unable to extract %s: %s\n", path.c_str(), ErrorCodeString(ret));
    return std::nullopt;
  }
  return path;
}

bool TwrpApex::MountApexOnLoopbackDevices(const std::vector<fs::path>& apex_files) {
  const android::base::unique_fd control_fd(open(kLoopControl.c_str(), O_RDWR | O_CLOEXEC));
  if (control_fd < 0) {
    LOGERR("Unable to open %s device. Reason: %s\n", kLoopControl.c_str(),
           std::system_category().message(errno).c_str());
    return false;
  }

  for (const auto& apex_file : apex_files) {
    // Unzip first and only allocate a loop slot when there is a payload to
    // mount: a missing apex no longer burns a /dev/block/loopN slot, and the
    // kernel-assigned number is reused for the node name, mknod minor and the
    // device LoadApexImage opens, so they can no longer drift apart.
    auto file_to_mount = UnzipImage(apex_file);
    if (!file_to_mount) {
      LOGINFO("Skipping non-existent apex file: %s\n", apex_file.c_str());
      continue;
    }

    const int num = ioctl(control_fd.get(), LOOP_CTL_GET_FREE);
    if (num < 0) {
      LOGERR("Unable to allocate loop device. Reason: %s\n",
             std::system_category().message(errno).c_str());
      return false;
    }
    const fs::path loop_device = kLoopBlockDeviceDir / std::format("loop{}", num);
    if (std::error_code ec; !fs::exists(loop_device, ec)) {
      if (mknod(loop_device.c_str(), S_IFBLK | S_IRUSR | S_IWUSR, makedev(7, num)) != 0) {
        LOGERR("Unable to create loop device: %s\n", loop_device.c_str());
        return false;
      }
    }
    if (!LoadApexImage(*file_to_mount, loop_device)) {
      return false;
    }
  }
  return true;
}

bool TwrpApex::LoadApexImage(const fs::path& file_to_mount, const fs::path& loop_device) {
  android::base::unique_fd fd(open(file_to_mount.c_str(), O_RDONLY | O_CLOEXEC));
  if (fd < 0) {
    LOGERR("unable to open apex file: %s. Reason: %s\n", file_to_mount.c_str(),
           std::system_category().message(errno).c_str());
    unlink(file_to_mount.c_str());
    return false;
  }

  const android::base::unique_fd loop_fd(open(loop_device.c_str(), O_RDONLY));
  if (loop_fd < 0) {
    LOGERR("unable to open loop device: %s\n", loop_device.c_str());
    unlink(file_to_mount.c_str());
    return false;
  }

  // Mount point is /apex/<module name>, i.e. the payload filename with its
  // trailing ".apex" extension removed.
  const fs::path bind_mount = kApexBase / file_to_mount.stem();
  // Best-effort rollback for the failure paths below: detach the loop device
  // and drop the /tmp extraction plus any mount-point directory already
  // created. The caller does not run Unmount() when LoadApexImages() fails,
  // so without this a half-set-up apex would leak for the whole session.
  // loop_fd is valid at every call site and stays open until return.
  const auto rollback = [&]() {
    ioctl(loop_fd.get(), LOOP_CLR_FD, 0);
    rmdir(bind_mount.c_str());
    unlink(file_to_mount.c_str());
  };

  if (ioctl(loop_fd.get(), LOOP_SET_FD, fd.get()) < 0) {
    LOGERR("failed to mount %s to loop device %s. Reason: %s\n", file_to_mount.c_str(),
           loop_device.c_str(), std::system_category().message(errno).c_str());
    rollback();
    return false;
  }
  // The loop driver keeps its own reference to the backing file once LOOP_SET_FD
  // succeeds, so the original descriptor can be released here.
  fd.reset();

  loop_info64 info{};
  // Label the loop device for twrp; the zero-init above null-terminates the
  // remainder of lo_crypt_name, so copying just the label bytes suffices.
  constexpr std::string_view kLoopLabel{ "twrp_apex" };
  std::ranges::copy(kLoopLabel, info.lo_crypt_name);
  // lo_sizelimit stays 0: the kernel exposes the full backing file.
  if (ioctl(loop_fd.get(), LOOP_SET_STATUS64, &info) != 0) {
    LOGERR("failed to mount loop: %s: %s\n", file_to_mount.c_str(),
           std::system_category().message(errno).c_str());
    rollback();
    return false;
  }
  if (ioctl(loop_fd.get(), BLKFLSBUF, 0) == -1) {
    LOGERR("Unable to flush loop device buffers\n");
    rollback();
    return false;
  }
  if (ioctl(loop_fd.get(), LOOP_SET_BLOCK_SIZE, 4096) == -1) {
    LOGINFO("Failed to set DIRECT_IO buffer size\n");
  }

  // create_directory tolerates an existing directory (remount after Unmount)
  // and applies a sane 0755-mode default.
  if (std::error_code ec; !fs::create_directory(bind_mount, ec) &&
                          !fs::is_directory(bind_mount, ec)) {
    LOGERR("Unable to create bind mount directory: %s. Reason: %s\n", bind_mount.c_str(),
           ec.message().c_str());
    rollback();
    return false;
  }

  if (mount(loop_device.c_str(), bind_mount.c_str(), "ext4", MS_RDONLY, nullptr) != 0) {
    LOGINFO("Trying mount with erofs\n");
    if (mount(loop_device.c_str(), bind_mount.c_str(), "erofs", MS_RDONLY, nullptr) != 0) {
      LOGERR("unable to mount loop device %s to %s. Reason: %s\n", loop_device.c_str(),
             bind_mount.c_str(), std::system_category().message(errno).c_str());
      rollback();
      return false;
    }
  }
  mounted_apexes_.push_back({ loop_device, bind_mount, file_to_mount });
  return true;
}

bool TwrpApex::Unmount() {
  bool ok = true;
  for (const auto& [loop_device, mount_point, payload] : mounted_apexes_) {
    // Unmount first (LOOP_CLR_FD on a still-mounted device returns EBUSY), then
    // detach the loop, then remove the now-empty mount point and the extracted
    // payload. EINVAL/ENOENT just mean it was already gone.
    if (umount2(mount_point.c_str(), MNT_DETACH) != 0 && errno != EINVAL && errno != ENOENT) {
      LOGERR("Unable to unmount apex %s. Reason: %s\n", mount_point.c_str(),
             std::system_category().message(errno).c_str());
      ok = false;
    }
    android::base::unique_fd loop_fd(open(loop_device.c_str(), O_RDONLY));
    if (loop_fd >= 0) {
      ioctl(loop_fd.get(), LOOP_CLR_FD, 0); // drops the kernel's ref to the /tmp inode
    }
    rmdir(mount_point.c_str());
    unlink(payload.c_str());
  }
  mounted_apexes_.clear();

  if (flattened_mounted_) {
    if (umount2(kApexBase.c_str(), MNT_DETACH) != 0 && errno != EINVAL && errno != ENOENT) {
      LOGERR("Unable to unmount flattened apex directory %s. Reason: %s\n", kApexBase.c_str(),
             std::system_category().message(errno).c_str());
      ok = false;
    }
    flattened_mounted_ = false;
  }
  return ok;
}