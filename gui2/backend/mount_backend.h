#ifndef GUI2_BACKEND_MOUNT_BACKEND_H
#define GUI2_BACKEND_MOUNT_BACKEND_H

#include <string>
#include <vector>

namespace gui2_backend {

struct mount_target {
  std::string name;
  std::string mount_point;
  bool mounted = false;
};

// Where backups are written and zips are read from. Only one is current.
struct storage_device {
  std::string name;
  std::string path;
  bool selected = false;
};

class mount_backend {
 public:
  virtual ~mount_backend() = default;

  virtual std::vector<mount_target> targets() = 0;

  // Mount and unmount are quick enough to run on the UI thread; both report
  // whether the partition ended up in the requested state.
  virtual bool set_mounted(const std::string& mount_point, bool mounted) = 0;

  // System is remounted read-only after every boot unless the user asks
  // otherwise, which the legacy UI exposes as its own toggle.
  virtual bool system_writable() = 0;
  virtual bool set_system_writable(bool writable) = 0;

  // Current storage, as the legacy header prints it: a name and the free space
  // already formatted for display.
  virtual std::vector<storage_device> storages() = 0;
  virtual bool select_storage(const std::string& path) = 0;
  virtual std::string storage_name() = 0;
  virtual std::string storage_free() = 0;

  virtual bool mtp_enabled() = 0;
  virtual bool set_mtp_enabled(bool enabled) = 0;

  // USB mass storage needs the kernel's lun files, which configfs devices do
  // not have; the row stays away rather than failing on every tap.
  virtual bool has_usb_storage() = 0;
  virtual bool usb_storage_enabled() = 0;
  virtual bool set_usb_storage_enabled(bool enabled) = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_MOUNT_BACKEND_H
