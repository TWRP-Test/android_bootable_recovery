#include "i18n/console_strings.h"

#include <algorithm>

namespace gui2_i18n {

namespace {

// Ported from the legacy theme's language files. Loading those XMLs at runtime
// is not an option: their resource list starts with font overrides that expect
// the legacy font stack, which GUI2 has replaced, and walking into it crashes.
struct console_string {
  const char* key;
  const char* zh_cn;
  const char* zh_tw;
};

// Sorted by key so the lookup can bisect.
constexpr console_string kConsoleStrings[] = {
    { "available_space", " * 可用空间: {1}", " * 可用空間: {1}" },
    { "avg_backup_fs", "文件平均备份速度: {1}", "文件平均備份速度: {1}" },
    { "avg_backup_img", "镜像平均备份速度: {1}", "映像平均備份速度: {1}" },
    { "backing_up", "正在备份 {1}…", "正在備份 {1}…" },
    { "backup_clean", "备份失败。正在清理备份文件夹。", "備份失敗。正在清理備份資料夾。" },
    { "backup_completed", "[备份已完成，耗时 {1} 秒]", "[備份已完成，耗時 {1} 秒]" },
    { "backup_folder", " * 备份文件夹: {1}", " * 備份資料夾: {1}" },
    { "backup_name_exists", "使用该名称的备份已经存在！", "使用該名稱的備份已經存在！" },
    { "backup_name_invalid", "备份名称“{1}”中包含无效字符:“{1}”", "備份名稱“{1}”中包含無效字元:“{1}”" },
    { "backup_name_len", "备份名称太长。", "備份名稱太長。" },
    { "backup_started", "[已开始备份]", "[已開始備份]" },
    { "backup_storage_undecrypt_warning", "备份不包含一些属于用户 {1} 的文件，因为该用户没有解密", "備份不包含一些屬於使用者 {1} 的文件，因為該使用者沒有解密" },
    { "backup_storage_warning", "备份的 {1} 分区不包含内置存储空间上的任何文件，例如照片或下载的文件等等。", "備份的 {1} 分割區不包含內建儲存空間上的任何文件，例如照片或下載的文件等等。" },
    { "cache_dalvik_done", "-- Dalvik Cache 目录清除完成！", "-- Dalvik Cache 目錄清除完成！" },
    { "calc_restore", "正在计算恢复详情…", "正在計算復原詳情…" },
    { "cannot_resize", "无法更改 {1} 的大小。", "無法更改 {1} 的大小。" },
    { "cannot_wipe", "无法清除 {1} 分区。", "無法清除 {1} 分割區。" },
    { "cleaned", "已清除：{1}…", "已清除：{1}…" },
    { "copy_kernel_log", "已导出 Kernel 日志到 {1}", "已匯出 Kernel 日誌到 {1}" },
    { "copy_logcat", "已导出 Logcat 日志到 {1}", "已匯出 Logcat 日誌到 {1}" },
    { "create_folder_strerr", "无法创建“{1}”文件夹 ({2})。", "無法建立“{1}”資料夾 ({2})。" },
    { "create_part", "正在创建 {1} 分区…", "正在建立 {1} 分割區…" },
    { "dalvik_done", "-- Dalvik 目录清除完成！", "-- Dalvik 目錄清除完成！" },
    { "data_media_fbe_msg", "", "TWRP 不會在 FBE 裝置上重新建立 /data/media。請重新啟動到 ROM 以建立 /data/media" },
    { "datamedia_fs_restore", "警告：此/data 备份文件系统为 {1}! 除非把文件系统格式设置为 {1} 否则可能无法启动。", "警告：此/data 備份文件系統為 {1}! 除非把文件系統格式設定為 {1} 否則可能無法啟動。" },
    { "decrypt_success", "已使用默认密码解密成功。", "已使用預設密碼解密成功。" },
    { "decrypt_success_dev", "Data 分区成功解密，新增块设备:“{1}”", "Data 分割區成功解密，新增塊裝置:“{1}”" },
    { "decrypt_success_nodev", "Data 分区已成功解密", "Data 分割區已成功解密" },
    { "decrypt_user_fail_fbe", "用户 {1} 解密失败", "使用者 {1} 解密失敗" },
    { "decrypt_user_success_fbe", "用户 {1} 解密成功", "使用者 {1} 解密成功" },
    { "decrypting_user_fbe", "尝试为用户 {1} 解密 FBE…", "嘗試為使用者 {1} 解密 FBE…" },
    { "disable_avb2_fail_msg", "禁用 AVB2.0: 处理 '{1}' 失败！", "停用 AVB2.0: 處理 '{1}' 失敗！" },
    { "disable_avb2_success_msg", "禁用 AVB2.0: 处理 '{1}' 成功。", "停用 AVB2.0: 處理 '{1}' 成功。" },
    { "done", "完成。", "完成。" },
    { "error_opening_strerr", "打开出错:“{1}” ({2})", "打開出錯:“{1}” ({2})" },
    { "ext_swap_size", "EXT+Swap 大小超过 SDCard 容量。", "EXT+Swap 大小超過 SDCard 容量。" },
    { "fail_backup_folder", "建立备份文件夹失败。", "建立備份資料夾失敗。" },
    { "fail_mount", "挂载“{1}”失败 ({2})", "掛載“{1}”失敗 ({2})" },
    { "fail_unmount", "卸载“{1}”失败 ({2})", "移除“{1}”失敗 ({2})" },
    { "flash_done", "[镜像刷入完成]", "[映像刷入完成]" },
    { "flash_unable_locate", "未找到“{1}”分区。", "未找到“{1}”分割區。" },
    { "flashing", "正在刷入 {1}…", "正在刷入 {1}…" },
    { "format_data_err", "无法格式化并删除加密。", "無法格式化並刪除加密。" },
    { "format_data_msg", "您可能需要重启 Recovery 才能使用/data。", "您可能需要重啟 Recovery 才能使用/data。" },
    { "format_sdext_as", "正在将 SD-EXT 格式化为 {1}…", "正在將 SD-EXT 格式化為 {1}…" },
    { "formatting_using", "使用 {2} 格式化 {1}…", "使用 {2} 格式化 {1}…" },
    { "full_selinux", "完整 SELinux 支持。", "完整 SELinux 支援。" },
    { "image_flash_start", "[开始刷入镜像]", "[開始刷入映像]" },
    { "img_size_err", "镜像大小大于目标设备", "映像大小大於目標裝置" },
    { "img_to_flash", "刷入镜像:“{1}”", "刷入映像:“{1}”" },
    { "invalid_flash", "指定的刷入分区无效。", "指定的刷入分割區無效。" },
    { "mtp_already_enabled", "MTP 已经启用", "MTP 已經啟用" },
    { "mtp_fail", "启用 MTP 失败", "啟用 MTP 失敗" },
    { "no_andsec", "未发现 android secure 分区。", "未發現 android secure 分割區。" },
    { "no_crypto_support", "此版本不支持加密。", "此版本不支援加密。" },
    { "no_kernel_selinux", "内核不支持读取 SELinux Context。", " Kernel 不支援讀取 SELinux Context。" },
    { "no_mtp", "不支持 MTP", "不支援 MTP" },
    { "no_part_flash", "未选择刷入分区。", "未選擇刷入分割區。" },
    { "no_part_restore", "未选择要恢复的分区。", "未選擇要復原的分割區。" },
    { "no_partition_selected", "未选择备份分区。", "未選擇備份分割區。" },
    { "no_space", "存储器上没有足够的空间。", "儲存器上沒有足夠的空間。" },
    { "part_complete", "分区完成。", "分割區完成。" },
    { "partition_sd_locate", "未找到需要分区的设备。", "未找到需要分割區的裝置。" },
    { "pid_error", "{1} 过程结束，错误: {2}", "{1} 過程結束，錯誤: {2}" },
    { "pid_signal", "{1} 过程结束，标志: {2}", "{1} 過程結束，標誌: {2}" },
    { "reboot_after_restore", "建议在 Android 首次启动后再重启一次。", "建議在 Android 首次啟動後再重啟一次。" },
    { "recreate_folder_err", "无法重新创建 {1} 文件夹。", "無法重新建立 {1} 資料夾。" },
    { "remove_all", "移除“{1}”下的所有文件", "移除“{1}”下的所有文件" },
    { "remove_part_table", "正在删除分区表…", "正在刪除分割區表…" },
    { "rename_stock", "重命名 /system 下原版 Recovery 的补丁文件，避免原厂固件替换 TWRP。", "重新命名/system 下原版 Recovery 的補丁文件，避免原廠韌體取代 TWRP。" },
    { "repair_not_exist", "{1} 不存在！无法修复！", "{1} 不存在！無法修復！" },
    { "repair_resize", "在调整大小之前修复 {1}。", "在調整大小之前修復 {1}。" },
    { "repairing_using", "正在使用 {2} 修复 {1}…", "正在使用 {2} 修復 {1}…" },
    { "resizing", "正在调整…", "正在調整…" },
    { "restore_completed", "[恢复完成，耗时 {1} 秒]", "[復原完成，耗時 {1} 秒]" },
    { "restore_folder", "恢复文件夹:“{1}”", "復原資料夾:“{1}”" },
    { "restore_part_count", "正在恢复 {1} 个分区…", "正在復原 {1} 個分割區…" },
    { "restore_part_done", "[{1} 恢复完成（{2} 秒）]", "[{1} 復原完成（{2} 秒）]" },
    { "restore_read_only", "无法恢复 {1} -- 已挂载为只读。", "無法復原 {1} -- 已掛載為唯讀。" },
    { "restore_started", "[已开始还原]", "[已開始還原]" },
    { "restore_system_context", "无法获取 {1} 的默认 context -- Android 可能无法启动。", "無法取得 {1} 的預設 context -- Android 可能無法啟動。" },
    { "restore_unable_locate", "未找到“{1}”分区。", "未找到“{1}”分割區。" },
    { "restoring", "正在恢复 {1}…", "正在復原 {1}…" },
    { "run_script", "正在运行 {1} 脚本…", "正在執行 {1} 腳本…" },
    { "skip_digest", "基于用户设置，已跳过 Digest 检查。", "基於使用者設定，已跳過 Digest 檢查。" },
    { "sparse_import_err", "无法导入 sparse 镜像“{1}”", "無法匯入 sparse 映像“{1}”" },
    { "start_partition_sd", "正在给 SDCard 分区…", "正在給 SDCard 分割區…" },
    { "too_many_flash", "选择刷入的分区太多。", "選擇刷入的分割區太多。" },
    { "total_backed_size", "[总计备份 {1}]", "[總計備份 {1}]" },
    { "total_backup_size", " * 备份数据总计: {1}", " * 備份資料總計: {1}" },
    { "total_partitions_backup", " * 备份分区总数: {1}", " * 備份分割區總數: {1}" },
    { "total_restore_size", "恢复文件总计 {1}", "復原文件總計 {1}" },
    { "unable_find_part_path", "找不到分区路径“{1}”", "找不到分割區路徑“{1}”" },
    { "unable_locate_part_backup_name", "无法通过备份名称来查找分区:“{1}”", "無法透過備份名稱來尋找分割區:“{1}”" },
    { "unable_locate_storage", "未找到存储设备。", "未找到儲存裝置。" },
    { "unable_repair", "无法修复 {1}。", "無法修復 {1}。" },
    { "unable_resize", "无法调整 {1} 的大小。", "無法調整 {1} 的大小。" },
    { "unable_rm_part", "无法删除分区表。", "無法刪除分割區表。" },
    { "unable_set_boot_slot", "更改 Bootloader 启动槽位至 {1} 错误", "更改 Bootloader 啟動槽位至 {1} 錯誤" },
    { "unable_to_create_part", "无法创建 {1} 分区。", "無法建立 {1} 分割區。" },
    { "unable_to_decrypt", "无法使用默认密码来解密，您可能需要格式化 Data 分区。", "無法使用預設密碼來解密，您可能需要格式化 Data 分割區。" },
    { "unable_to_locate", "未找到 {1}。", "未找到 {1}。" },
    { "unable_to_locate_partition", "未找到“{1}”分区。", "未找到“{1}”分割區。" },
    { "unable_to_mount_storage", "无法挂载存储空间", "無法掛載儲存器" },
    { "unable_to_open", "无法打开“{1}”。", "無法打開“{1}”。" },
    { "unable_to_wipe", "无法清除 {1}。", "無法清除 {1}。" },
    { "update_part_details", "正在更新分区详情…", "正在更新分割區詳情…" },
    { "update_part_details_done", "…完成", "…完成" },
    { "verifying_digest", "正在校验 Digest", "正在校驗 Digest" },
    { "wiping", "正在清除 {1}", "正在清除 {1}" },
    { "wiping_cache_dalvik", "正在清除 Cache 和 Dalvik…", "正在清除 Cache 和 Dalvik…" },
    { "wiping_dalvik", "正在清除 Dalvik 目录…", "正在清除 Dalvik 目錄…" },
    { "wiping_data", "正在清除 Data 分区，跳过清除/data/media…", "正在清除 Data 分割區，跳過清除/data/media…" },
    { "wiping_datamedia", "清除内置存储空间 -- /data/media…", "清除內建儲存空間 -- /data/media…" },
};

}  // namespace

const char* console_string_for(language_id language, const std::string& key) {
  if (language == language_id::ENGLISH) return nullptr;

  const auto* first = std::begin(kConsoleStrings);
  const auto* last = std::end(kConsoleStrings);
  const auto* found = std::lower_bound(
      first, last, key,
      [](const console_string& entry, const std::string& value) { return entry.key < value; });
  if (found == last || key != found->key) return nullptr;

  const char* text = language == language_id::ZH_TW ? found->zh_tw : found->zh_cn;
  return (text != nullptr && text[0] != 0) ? text : nullptr;
}

}  // namespace gui2_i18n
