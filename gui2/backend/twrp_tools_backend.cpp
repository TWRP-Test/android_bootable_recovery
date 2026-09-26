#include "twrp_tools_backend.h"

#include <fstream>

#include "data.hpp"
#include "gui/gui.hpp"
#include "gui/twmsg.h"
#include "partitions.hpp"
#include "twcommon.h"
#include "twrp-functions.hpp"
#include "twrpRepacker.hpp"
#include "unit_conversion.hpp"
#include "variables.h"

namespace gui2_backend {

namespace {

bool flag(const char* name) {
  return DataManager::GetIntValue(name) != 0;
}

// GUIAction::applycustomtwrpfolder.
bool apply_twrp_folder(const std::string& name) {
  const std::string storage = DataManager::GetCurrentStoragePath();
  const std::string new_folder = storage + '/' + name;
  const std::string new_backups = new_folder + "/BACKUPS/" + DataManager::GetStrValue("device_id");
  const std::string previous = storage + DataManager::GetStrValue(TW_RECOVERY_FOLDER_VAR);

  if (TWFunc::Path_Exists(new_folder) || new_folder == previous) {
    gui_msg(Msg(msg::kError, "tw_folder_exists=A folder with that name already exists!"));
    return false;
  }
  if (TWFunc::Path_Exists(previous) &&
      TWFunc::Exec_Cmd("mv -f \"" + previous + "\" \"" + new_folder + '"') != 0)
    return false;
  if (!TWFunc::Recursive_Mkdir(new_backups)) return false;

  DataManager::SetValue(TW_RECOVERY_FOLDER_VAR, '/' + name);
  DataManager::SetValue(TW_BACKUPS_FOLDER_VAR, new_backups);
  // Marks the renamed folder as TWRP's after a reboot.
  std::ofstream(new_folder + "/.twrpcf").close();
  return true;
}

// GUIAction::fixabrecoverybootloop.
bool fix_recovery_bootloop() {
  if (!TWFunc::Path_Exists("/system/bin/magiskboot")) {
    LOGERR("Image repacking tool not present in this TWRP build!");
    return false;
  }
  DataManager::SetProgress(0);
  TWPartition* part = PartitionManager.Find_Partition_By_Path("/boot");
  if (part == nullptr) {
    gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/boot"));
    return false;
  }
  gui_msg(Msg("unpacking_image=Unpacking {1}...")(part->Get_Display_Name()));
  twrpRepacker repacker;
  if (!repacker.Backup_Image_For_Repack(part, REPACK_ORIG_DIR,
                                        DataManager::GetIntValue("tw_repack_backup_first") != 0,
                                        gui_lookup("repack", "Repack")))
    return false;
  DataManager::SetProgress(.25);
  gui_msg("fixing_recovery_loop_patch=Patching kernel...");
  std::string command = "cd " REPACK_ORIG_DIR
                        " && /system/bin/magiskboot hexpatch kernel "
                        "77616E745F696E697472616D667300 736B69705F696E697472616D667300";
  if (TWFunc::Exec_Cmd(command) != 0) {
    gui_msg(Msg(msg::kError, "fix_recovery_loop_patch_error=Error patching kernel."));
    return false;
  }
  if (TWFunc::Path_Exists(REPACK_ORIG_DIR "header")) {
    command = "cd " REPACK_ORIG_DIR
              " && sed -i \"s|$(grep '^cmdline=' header | cut -d= -f2-)|$(grep '^cmdline=' "
              "header | cut -d= -f2- | sed -e 's/skip_override//' -e 's/  */ /g' -e "
              "'s/[ \t]*$//')|\" header";
    if (TWFunc::Exec_Cmd(command) != 0) {
      gui_msg(Msg(msg::kError, "fix_recovery_loop_patch_error=Error patching kernel."));
      return false;
    }
  }
  DataManager::SetProgress(.5);
  gui_msg(Msg("repacking_image=Repacking {1}...")(part->Get_Display_Name()));
  command = "cd " REPACK_ORIG_DIR " && /system/bin/magiskboot repack " REPACK_ORIG_DIR "boot.img";
  if (TWFunc::Exec_Cmd(command) != 0) {
    gui_msg(Msg(msg::kError, "repack_error=Error repacking image."));
    return false;
  }
  DataManager::SetProgress(.75);
  DataManager::SetValue("tw_flash_partition", "/boot;");
  std::string path = REPACK_ORIG_DIR;
  std::string file = "new-boot.img";
  if (!PartitionManager.Flash_Image(path, file)) {
    LOGINFO("Error flashing new image\n");
    return false;
  }
  DataManager::SetProgress(1);
  TWFunc::removeDir(REPACK_ORIG_DIR, false);
  return true;
}

}  // namespace

twrp_tools_backend::~twrp_tools_backend() {
  if (worker_.joinable()) worker_.join();
}

// GUIAction::getpartitiondetails.
bool twrp_tools_backend::details(const std::string& mount_point, partition_details* out) {
  if (out == nullptr) return false;
  TWPartition* part = PartitionManager.Find_Partition_By_Path(mount_point);
  if (part == nullptr) return false;

  *out = {};
  out->name = part->Display_Name;
  out->mount_point = part->Mount_Point;
  out->file_system = part->Current_File_System;
  out->size = UnitConversion::FormatBytes(part->Size);
  out->used = UnitConversion::FormatBytes(part->Used);
  out->free = UnitConversion::FormatBytes(part->Free);
  out->backup_size = UnitConversion::FormatBytes(part->Backup_Size);
  out->present = part->Is_Present;
  out->removable = part->Removable;
  out->can_repair = part->Can_Repair();
  out->can_resize = part->Can_Resize();
  if (TWFunc::Path_Exists("/system/bin/mke2fs"))
    out->file_systems.insert(out->file_systems.end(), { "ext2", "ext3", "ext4" });
  if (TWFunc::Path_Exists("/system/bin/mkfs.fat")) out->file_systems.push_back("vfat");
  if (TWFunc::Path_Exists("/system/bin/mkfs.exfat")) out->file_systems.push_back("exfat");
  if (TWFunc::Path_Exists("/system/bin/make_f2fs")) out->file_systems.push_back("f2fs");
  return true;
}

// The conditions on the legacy advanced page's buttons.
tool_availability twrp_tools_backend::availability() {
  tool_availability result;
  result.twrp_folder = flag(TW_IS_DECRYPTED);
  result.fix_recovery_bootloop =
      flag("tw_has_boot_slots") && flag("tw_has_repack_tools") && flag("tw_uses_initramfs");
  result.merge_snapshots = flag(TW_VIRTUAL_AB_ENABLED);
  result.disable_avb2 = true;
  return result;
}

std::string twrp_tools_backend::twrp_folder() {
  return DataManager::GetStrValue(TW_RECOVERY_FOLDER_VAR);
}

std::string twrp_tools_backend::storage_path() {
  return DataManager::GetCurrentStoragePath();
}

bool twrp_tools_backend::path_exists(const std::string& path) {
  return TWFunc::Path_Exists(path);
}

bool twrp_tools_backend::users_locked() {
  return flag("tw_is_fbe") && !flag("tw_all_users_decrypted");
}

void twrp_tools_backend::join_finished_thread() {
  if (!running_.load() && worker_.joinable()) worker_.join();
}

bool twrp_tools_backend::start(tool_job job, const std::string& target, const std::string& value) {
  if (running_.load()) return false;
  join_finished_thread();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = tool_state::RUNNING;
  }
  DataManager::SetValue("ui_progress", 0);
  running_.store(true);
  worker_ = std::thread(&twrp_tools_backend::run, this, job, target, value);
  return true;
}

void twrp_tools_backend::run(tool_job job, std::string target, std::string value) {
  bool ok = false;
  switch (job) {
    case tool_job::REPAIR:
      ok = PartitionManager.Repair_By_Path(target, true);
      break;
    case tool_job::RESIZE:
      ok = PartitionManager.Resize_By_Path(target, true);
      break;
    case tool_job::CHANGE_FILE_SYSTEM:
      ok = PartitionManager.Wipe_By_Path(target, value);
      if (!ok) gui_err("change_fs_err=Error changing file system.");
      PartitionManager.Update_System_Details();
      break;
    case tool_job::RENAME_BACKUP: {
      const size_t slash = target.find_last_of('/');
      const std::string renamed =
          (slash == std::string::npos ? std::string() : target.substr(0, slash + 1)) + value;
      ok = TWFunc::Exec_Cmd("mv \"" + target + "\" \"" + renamed + '"') == 0;
      break;
    }
    case tool_job::DELETE_BACKUP:
      ok = TWFunc::Exec_Cmd("rm -rf \"" + target + '"') == 0;
      break;
    case tool_job::TWRP_FOLDER:
      ok = apply_twrp_folder(value);
      break;
    case tool_job::FIX_RECOVERY_BOOTLOOP:
      ok = fix_recovery_bootloop();
      break;
    case tool_job::MERGE_SNAPSHOTS:
      ok = PartitionManager.Check_Pending_Merges();
      break;
    case tool_job::DISABLE_AVB2:
      gui_highlight("disabling_AVB2=Disabling AVB2.0...");
      ok = PartitionManager.Disable_AVB2(true);
      break;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = ok ? tool_state::DONE : tool_state::FAILED;
  }
  running_.store(false);
}

tool_state twrp_tools_backend::status() {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void twrp_tools_backend::acknowledge() {
  join_finished_thread();
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != tool_state::RUNNING) state_ = tool_state::IDLE;
}

}  // namespace gui2_backend
