#include "twrp_tools_backend.h"

#include <unistd.h>

#include <fstream>

#include "data.hpp"
#include "gui/gui.hpp"
#include "gui/twmsg.h"
#include "partitions.hpp"
#include "twcommon.h"
#include "twrp-functions.hpp"
#include "twrpRepacker.hpp"
#include "twrpinstall/include/set_metadata.h"
#include "twrp_operation.h"
#include "unit_conversion.hpp"
#include "variables.h"

namespace gui2_backend {

namespace {

bool flag(const char* name) {
  return DataManager::GetIntValue(name) != 0;
}

// Each returns the operation status the legacy handler hands operation_end:
// 0 for success.

// GUIAction::repair / resize, on tw_partition_mount_point.
int repair() {
  int op_status = 0;

  operation_start("Repair Partition");
  std::string part_path;
  DataManager::GetValue("tw_partition_mount_point", part_path);
  if (PartitionManager.Repair_By_Path(part_path, true)) {
    op_status = 0;  // success
  } else {
    op_status = 1;  // fail
  }

  operation_end(op_status);
  return op_status;
}

int resize() {
  int op_status = 0;

  operation_start("Resize Partition");
  std::string part_path;
  DataManager::GetValue("tw_partition_mount_point", part_path);
  if (PartitionManager.Resize_By_Path(part_path, true)) {
    op_status = 0;  // success
  } else {
    op_status = 1;  // fail
  }

  operation_end(op_status);
  return op_status;
}

// GUIAction::changefilesystem, on tw_partition_mount_point and
// tw_action_new_file_system.
int changefilesystem() {
  int op_status = 0;

  operation_start("Change File System");
  std::string part_path, file_system;
  DataManager::GetValue("tw_partition_mount_point", part_path);
  DataManager::GetValue("tw_action_new_file_system", file_system);
  if (PartitionManager.Wipe_By_Path(part_path, file_system)) {
    op_status = 0;  // success
  } else {
    gui_err("change_fs_err=Error changing file system.");
    op_status = 1;  // fail
  }
  PartitionManager.Update_System_Details();
  operation_end(op_status);
  return op_status;
}

// GUIAction::cmd
int cmd(std::string arg) {
  int op_status = 0;

  operation_start("Command");
  LOGINFO("Running command: '%s'\n", arg.c_str());
  op_status = TWFunc::Exec_Cmd(arg);
  if (op_status != 0) op_status = 1;

  operation_end(op_status);
  return op_status;
}

// GUIAction::copylog
int copylog() {
  operation_start("Copy Log");
  {
    std::string dst, curr_storage;
    int copy_kernel_log = 0;
    int copy_logcat = 1;

    DataManager::GetValue("tw_include_kernel_log", copy_kernel_log);
    DataManager::GetValue("tw_include_logcat", copy_logcat);
    PartitionManager.Mount_Current_Storage(true);
    curr_storage = DataManager::GetCurrentStoragePath();
    dst = curr_storage + "/recovery.log";
    TWFunc::copy_file("/tmp/recovery.log", dst.c_str(), 0755);
    tw_set_default_metadata(dst.c_str());
    if (copy_kernel_log) TWFunc::copy_kernel_log(curr_storage);
    if (copy_logcat) TWFunc::copy_logcat(curr_storage);
    sync();
    gui_msg(Msg("copy_log=Copied recovery log to {1}")(dst));
  }
  operation_end(0);
  return 0;
}

// GUIAction::fixcontexts
int fixcontexts() {
  int op_status = 0;

  operation_start("Fix Contexts");
  LOGINFO("fix contexts started!\n");
  op_status = PartitionManager.Fix_Contexts();
  if (op_status != 0) op_status = 1;  // failure
  operation_end(op_status);
  return op_status;
}

// GUIAction::unmapsuperdevices
int unmapsuperdevices() {
  int op_status = 1;

  operation_start("Remove Super Devices");
  if (PartitionManager.Unmap_Super_Devices()) {
    op_status = 0;
  }

  operation_end(op_status);
  return op_status;
}

// GUIAction::reflashtwrp
int reflashtwrp() {
  int op_status = 1;
  twrpRepacker repacker;

  operation_start("Repack Image");
  if (!repacker.Flash_Current_Twrp()) goto exit;
  op_status = 0;
exit:
  operation_end(op_status);
  return op_status;
}

// GUIAction::repackimage
int repackimage() {
  int op_status = 1;
  twrpRepacker repacker;

  operation_start("Repack Image");
  {
    std::string path = DataManager::GetStrValue("tw_filename");
    Repack_Options_struct Repack_Options;
    Repack_Options.Disable_Verity = false;
    Repack_Options.Disable_Force_Encrypt = false;
    Repack_Options.Backup_First = DataManager::GetIntValue("tw_repack_backup_first") != 0;
    if (DataManager::GetIntValue("tw_repack_kernel") == 1)
      Repack_Options.Type = REPLACE_KERNEL;
    else
      Repack_Options.Type = REPLACE_RAMDISK;
    if (!repacker.Repack_Image_And_Flash(path, Repack_Options)) goto exit;
  }
  op_status = 0;
exit:
  operation_end(op_status);
  return op_status;
}

// GUIAction::refreshsizes
int refreshsizes() {
  operation_start("Refreshing Sizes");
  PartitionManager.Update_System_Details();
  operation_end(0);
  return 0;
}

// GUIAction::applycustomtwrpfolder
int applycustomtwrpfolder(std::string arg) {
  operation_start("ChangingTWRPFolder");
  std::string storageFolder = DataManager::GetCurrentStoragePath();
  std::string newFolder = storageFolder + '/' + arg;
  std::string newBackupFolder = newFolder + "/BACKUPS/" + DataManager::GetStrValue("device_id");
  std::string prevFolder = storageFolder + DataManager::GetStrValue(TW_RECOVERY_FOLDER_VAR);
  bool ret = false;

  if (TWFunc::Path_Exists(newFolder)) {
    gui_msg(Msg(msg::kError, "tw_folder_exists=A folder with that name already exists!"));
  } else {
    ret = true;
  }

  if (newFolder != prevFolder && ret) {
    // Nothing has been put under the old name until a backup or a theme
    // lands there, so there may be nothing to move.
    if (TWFunc::Path_Exists(prevFolder))
      ret = TWFunc::Exec_Cmd("mv -f \"" + prevFolder + "\" \"" + newFolder + '\"') == 0;
  } else {
    gui_msg(Msg(msg::kError, "tw_folder_exists=A folder with that name already exists!"));
  }

  if (ret) ret = TWFunc::Recursive_Mkdir(newBackupFolder) ? true : false;

  if (ret) {
    DataManager::SetValue(TW_RECOVERY_FOLDER_VAR, '/' + arg);
    DataManager::SetValue(TW_BACKUPS_FOLDER_VAR, newBackupFolder);
    // Creates an empty file that marks which folder is TWRP with the renamed new name, after reboot.
    std::string path = newFolder + "/.twrpcf";
    std::ofstream twrpcf(path);
    twrpcf.close();
  }
  operation_end((int)!ret);
  return (int)!ret;
}

// GUIAction::fixabrecoverybootloop
int fixabrecoverybootloop() {
  int op_status = 1;
  twrpRepacker repacker;

  operation_start("Repack Image");
  {
    if (!TWFunc::Path_Exists("/system/bin/magiskboot")) {
      LOGERR("Image repacking tool not present in this TWRP build!");
      goto exit;
    }
    DataManager::SetProgress(0);
    TWPartition* part = PartitionManager.Find_Partition_By_Path("/boot");
    if (part)
      gui_msg(Msg("unpacking_image=Unpacking {1}...")(part->Get_Display_Name()));
    else {
      gui_msg(Msg(msg::kError, "unable_to_locate=Unable to locate {1}.")("/boot"));
      goto exit;
    }
    if (!repacker.Backup_Image_For_Repack(part, REPACK_ORIG_DIR,
                                          DataManager::GetIntValue("tw_repack_backup_first") != 0,
                                          gui_lookup("repack", "Repack")))
      goto exit;
    DataManager::SetProgress(.25);
    gui_msg("fixing_recovery_loop_patch=Patching kernel...");
    std::string command = "cd " REPACK_ORIG_DIR
                          " && /system/bin/magiskboot hexpatch kernel "
                          "77616E745F696E697472616D667300 736B69705F696E697472616D667300";
    if (TWFunc::Exec_Cmd(command) != 0) {
      gui_msg(Msg(msg::kError, "fix_recovery_loop_patch_error=Error patching kernel."));
      goto exit;
    }
    std::string header_path = REPACK_ORIG_DIR;
    header_path += "header";
    if (TWFunc::Path_Exists(header_path)) {
      command = "cd " REPACK_ORIG_DIR
                " && sed -i \"s|$(grep '^cmdline=' header | cut -d= -f2-)|$(grep '^cmdline=' "
                "header | cut -d= -f2- | sed -e 's/skip_override//' -e 's/  */ /g' -e "
                "'s/[ \t]*$//')|\" header";
      if (TWFunc::Exec_Cmd(command) != 0) {
        gui_msg(Msg(msg::kError, "fix_recovery_loop_patch_error=Error patching kernel."));
        goto exit;
      }
    }
    DataManager::SetProgress(.5);
    gui_msg(Msg("repacking_image=Repacking {1}...")(part->Get_Display_Name()));
    command = "cd " REPACK_ORIG_DIR " && /system/bin/magiskboot repack " REPACK_ORIG_DIR "boot.img";
    if (TWFunc::Exec_Cmd(command) != 0) {
      gui_msg(Msg(msg::kError, "repack_error=Error repacking image."));
      goto exit;
    }
    DataManager::SetProgress(.75);
    std::string path = REPACK_ORIG_DIR;
    std::string file = "new-boot.img";
    DataManager::SetValue("tw_flash_partition", "/boot;");
    if (!PartitionManager.Flash_Image(path, file)) {
      LOGINFO("Error flashing new image\n");
      goto exit;
    }
    DataManager::SetProgress(1);
    TWFunc::removeDir(REPACK_ORIG_DIR, false);
  }
  op_status = 0;
exit:
  operation_end(op_status);
  return op_status;
}

// GUIAction::mergesnapshots, which has no operation_start of its own.
int mergesnapshots() {
  int op_status = 1;
  if (PartitionManager.Check_Pending_Merges()) {
    op_status = 0;
  }
  operation_end(op_status);
  return op_status;
}

// GUIAction::disableAVB2
int disableAVB2() {
  int op_status = 1;
  operation_start("Disable AVB2.0");
  gui_highlight("disabling_AVB2=Disabling AVB2.0...");
  if (PartitionManager.Disable_AVB2(true)) {
    op_status = 0;
  }
  operation_end(op_status);
  return op_status;
}

std::string base_name(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

twrp_tools_backend::~twrp_tools_backend() {
  if (worker_.joinable()) worker_.join();
}

// GUIAction::getpartitiondetails, for one partition instead of the first one
// in tw_wipe_list.
bool twrp_tools_backend::details(const std::string& mount_point, partition_details* out) {
  if (out == nullptr) return false;
  *out = {};
  TWPartition* Part = PartitionManager.Find_Partition_By_Path(mount_point);
  if (Part == nullptr) {
    LOGERR("Unable to locate partition: '%s'\n", mount_point.c_str());
    DataManager::SetValue("tw_partition_name", "");
    DataManager::SetValue("tw_partition_file_system", "");
    // Set this to 0 to prevent trying to partition this device, just in case
    DataManager::SetValue("tw_partition_removable", 0);
    return false;
  }
  DataManager::SetValue("tw_partition_path", mount_point);
  DataManager::SetValue("tw_partition_name", Part->Display_Name);
  DataManager::SetValue("tw_partition_mount_point", Part->Mount_Point);
  DataManager::SetValue("tw_partition_file_system", Part->Current_File_System);
  DataManager::SetValue("tw_partition_size", UnitConversion::FormatBytes(Part->Size));
  DataManager::SetValue("tw_partition_used", UnitConversion::FormatBytes(Part->Used));
  DataManager::SetValue("tw_partition_free", UnitConversion::FormatBytes(Part->Free));
  DataManager::SetValue("tw_partition_backup_size", UnitConversion::FormatBytes(Part->Backup_Size));
  DataManager::SetValue("tw_partition_removable", Part->Removable);
  DataManager::SetValue("tw_partition_is_present", Part->Is_Present);

  if (Part->Can_Repair())
    DataManager::SetValue("tw_partition_can_repair", 1);
  else
    DataManager::SetValue("tw_partition_can_repair", 0);
  if (Part->Can_Resize())
    DataManager::SetValue("tw_partition_can_resize", 1);
  else
    DataManager::SetValue("tw_partition_can_resize", 0);
  if (TWFunc::Path_Exists("/system/bin/mkfs.fat"))
    DataManager::SetValue("tw_partition_vfat", 1);
  else
    DataManager::SetValue("tw_partition_vfat", 0);
  if (TWFunc::Path_Exists("/system/bin/mkfs.exfat"))
    DataManager::SetValue("tw_partition_exfat", 1);
  else
    DataManager::SetValue("tw_partition_exfat", 0);
  if (TWFunc::Path_Exists("/system/bin/make_f2fs"))
    DataManager::SetValue("tw_partition_f2fs", 1);
  else
    DataManager::SetValue("tw_partition_f2fs", 0);
  if (TWFunc::Path_Exists("/system/bin/mke2fs"))
    DataManager::SetValue("tw_partition_ext", 1);
  else
    DataManager::SetValue("tw_partition_ext", 0);

  out->name = DataManager::GetStrValue("tw_partition_name");
  out->mount_point = DataManager::GetStrValue("tw_partition_mount_point");
  out->file_system = DataManager::GetStrValue("tw_partition_file_system");
  out->size = DataManager::GetStrValue("tw_partition_size");
  out->used = DataManager::GetStrValue("tw_partition_used");
  out->free = DataManager::GetStrValue("tw_partition_free");
  out->backup_size = DataManager::GetStrValue("tw_partition_backup_size");
  out->present = flag("tw_partition_is_present");
  out->removable = flag("tw_partition_removable");
  out->can_repair = flag("tw_partition_can_repair");
  out->can_resize = flag("tw_partition_can_resize");
  // selectfilesystem's buttons, in its order.
  if (flag("tw_partition_ext"))
    out->file_systems.insert(out->file_systems.end(), { "ext2", "ext3", "ext4" });
  if (flag("tw_partition_vfat")) out->file_systems.push_back("vfat");
  if (flag("tw_partition_exfat")) out->file_systems.push_back("exfat");
  if (flag("tw_partition_f2fs")) out->file_systems.push_back("f2fs");
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
  result.fix_contexts = flag(TW_HAS_DATA_MEDIA);
  const bool repack = flag("tw_has_boot_slots") && flag("tw_has_repack_tools");
  result.install_ramdisk = repack && flag("tw_include_install_recovery_ramdisk");
  result.reflash_twrp =
      repack && !flag("tw_no_flash_current_twrp") && !flag("tw_is_vendor_boot_header_v3");
  result.install_kernel = repack;
  result.unmap_super_devices = flag("tw_is_super");
  result.sideload = flag(TW_HAS_DATA_MEDIA);
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
  // What the legacy buttons set before their confirm_action page.
  switch (job) {
    case tool_job::REPAIR:
    case tool_job::RESIZE:
      DataManager::SetValue("tw_partition_mount_point", target);
      break;
    case tool_job::CHANGE_FILE_SYSTEM:
      DataManager::SetValue("tw_partition_mount_point", target);
      DataManager::SetValue("tw_action_new_file_system", value);
      break;
    case tool_job::RENAME_BACKUP:
      DataManager::SetValue("tw_restore_name", base_name(target));
      DataManager::SetValue("tw_backup_rename", value);
      break;
    case tool_job::DELETE_BACKUP:
      DataManager::SetValue("tw_restore_name", base_name(target));
      break;
    default:
      break;
  }
  running_.store(true);
  worker_ = std::thread(&twrp_tools_backend::run, this, job, value);
  return true;
}

void twrp_tools_backend::run(tool_job job, std::string value) {
  const std::string backups = DataManager::GetStrValue(TW_BACKUPS_FOLDER_VAR);
  const std::string name = DataManager::GetStrValue("tw_restore_name");
  int op_status = 1;
  switch (job) {
    case tool_job::REPAIR:
      op_status = repair();
      break;
    case tool_job::RESIZE:
      op_status = resize();
      break;
    case tool_job::CHANGE_FILE_SYSTEM:
      op_status = changefilesystem();
      break;
    case tool_job::RENAME_BACKUP:
      op_status = cmd("cd " + backups + " && mv \"" + name + "\" \"" +
                      DataManager::GetStrValue("tw_backup_rename") + "\"");
      break;
    case tool_job::DELETE_BACKUP:
      op_status = cmd("cd " + backups + " && rm -rf \"" + name + "\"");
      break;
    case tool_job::TWRP_FOLDER:
      op_status = applycustomtwrpfolder(value);
      break;
    case tool_job::FIX_RECOVERY_BOOTLOOP:
      op_status = fixabrecoverybootloop();
      break;
    case tool_job::MERGE_SNAPSHOTS:
      op_status = mergesnapshots();
      break;
    case tool_job::DISABLE_AVB2:
      op_status = disableAVB2();
      break;
    case tool_job::REFRESH_SIZES:
      op_status = refreshsizes();
      break;
    case tool_job::COMMAND:
      op_status = cmd(value);
      break;
    case tool_job::COPY_LOG:
      op_status = copylog();
      break;
    case tool_job::FIX_CONTEXTS:
      op_status = fixcontexts();
      break;
    case tool_job::UNMAP_SUPER_DEVICES:
      op_status = unmapsuperdevices();
      break;
    case tool_job::REFLASH_TWRP:
      op_status = reflashtwrp();
      break;
    case tool_job::REPACK_IMAGE:
      op_status = repackimage();
      break;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = op_status == 0 ? tool_state::DONE : tool_state::FAILED;
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
