#ifndef TWRP_APEX_HPP
#define TWRP_APEX_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// TwrpApex mounts flattened or loop-backed APEX images from the system tree into
// /apex/ so that ART and other APEX-backed components are available in recovery.
class TwrpApex {
public:
  // On-device directory holding the .apex images; exposed for diagnostics.
  static inline const fs::path kApexDir{ "/system_root/system/apex" };

  static bool LoadApexImages();
  static bool Unmount();

private:
  static inline const fs::path kApexBase{ "/apex/" };
  static inline const std::string kApexPayload{ "apex_payload.img" };
  static inline const fs::path kLoopBlockDeviceDir{ "/dev/block/" };
  static inline const fs::path kLoopControl{ "/dev/loop-control" };

  // A successfully loop-mounted apex, recorded at mount time so Unmount() can
  // detach the loop device, drop the /tmp extraction and remove the mount
  // point. Kept as class-level state: recovery's apex path is single threaded
  // and Unmount() clears it, so the two stay in sync.
  struct MountedApex {
    fs::path loop_device;  // /dev/block/loopN
    fs::path mount_point;  // /apex/<module>
    fs::path payload;      // /tmp/<name> extraction (unlinked after detach)
  };
  static inline std::vector<MountedApex> mounted_apexes_{};
  static inline bool flattened_mounted_{ false };

  static std::optional<fs::path> UnzipImage(const fs::path& file);
  static bool MountApexOnLoopbackDevices(const std::vector<fs::path>& apex_files);
  static bool LoadApexImage(const fs::path& file_to_mount, const fs::path& loop_device);
};

#endif  // TWRP_APEX_HPP
