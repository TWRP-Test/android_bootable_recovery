#include "twrp_file_list.h"

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>

#include <android-base/strings.h>

#include "data.hpp"
#include "partitions.hpp"
#include "twcommon.h"
#include "twrp-functions.hpp"
#include "twrpadbbu/libtwrpadbbu.hpp"
#include "variables.h"

namespace gui2_backend {

// GUIFileSelector::fileSort, over mSortOrder = tw_gui_sort_order.
static int mSortOrder = 0;

static bool fileSort(const file_entry& d1, const file_entry& d2) {
  switch (mSortOrder) {
    case 3:  // by size largest first
      if (d1.size == d2.size || d1.directory)  // some directories report a different size than others - but this is not the size of the files inside the directory, so we just sort by name on directories
        return (strcasecmp(d1.name.c_str(), d2.name.c_str()) < 0);
      return d1.size < d2.size;
    case -3:  // by size smallest first
      if (d1.size == d2.size || d1.directory)  // some directories report a different size than others - but this is not the size of the files inside the directory, so we just sort by name on directories
        return (strcasecmp(d1.name.c_str(), d2.name.c_str()) > 0);
      return d1.size > d2.size;
    case 2:  // by last modified date newest first
      if (d1.modified == d2.modified) return (strcasecmp(d1.name.c_str(), d2.name.c_str()) < 0);
      return d1.modified < d2.modified;
    case -2:  // by date oldest first
      if (d1.modified == d2.modified) return (strcasecmp(d1.name.c_str(), d2.name.c_str()) > 0);
      return d1.modified > d2.modified;
    case -1:  // by name descending
      return (strcasecmp(d1.name.c_str(), d2.name.c_str()) > 0);
    default:  // should be a 1 - sort by name ascending
      return (strcasecmp(d1.name.c_str(), d2.name.c_str()) < 0);
  }
  return 0;
}

file_listing twrp_file_list(const std::string& folder, const file_filter& filter,
                            bool mPathCreate) {
  file_listing result;
  DIR* d;
  struct dirent* de;
  struct stat st;

  // A list that owns its folder would otherwise walk up to the parent and
  // leave the path variable pointing somewhere nobody asked for. Only make
  // it while the storage is up, or it lands on the ramdisk under a mount
  // point with nothing mounted on it.
  if (mPathCreate && !TWFunc::Path_Exists(folder) && PartitionManager.Is_Mounted_By_Path(folder))
    TWFunc::Recursive_Mkdir(folder);

  d = opendir(folder.c_str());
  if (d == NULL) {
    LOGINFO("Unable to open '%s'\n", folder.c_str());
    result.opened = false;
    return result;
  }

  const std::string mExtn = filter.extn;
  const std::string mPrfx = filter.prfx;
  while ((de = readdir(d)) != NULL) {
    file_entry data;
    bool match = false;

    data.name = de->d_name;
    if (data.name == "." || data.name == "..") continue;

    unsigned char fileType = de->d_type;

    std::string path = folder + "/" + data.name;
    stat(path.c_str(), &st);
    char mode[8];
    snprintf(mode, sizeof(mode), "%04o", st.st_mode & 07777);
    data.mode = mode;
    data.size = static_cast<uint64_t>(st.st_size);
    data.modified = static_cast<int64_t>(st.st_mtime);

    if (fileType == DT_UNKNOWN) {
      fileType = TWFunc::Get_D_Type_From_Stat(path);
    }
    if (fileType == DT_DIR) {
      data.directory = true;
      result.folders.push_back(data);
    } else if (fileType == DT_REG || fileType == DT_LNK || fileType == DT_BLK) {
      std::vector<std::string> mExtnResults = android::base::Split(mExtn, ";");
      for (const std::string& mExtnElement : mExtnResults) {
        std::string mExtnName = android::base::Trim(mExtnElement);
        if (mExtnName.empty() ||
            (data.name.length() >= mExtnName.length() &&
             data.name.substr(data.name.length() - mExtnName.length()) == mExtnName)) {
          if (mExtnName == ".ab" && twadbbu::Check_ADB_Backup_File(path)) {
            data.directory = true;
            result.folders.push_back(data);
          } else {
            result.files.push_back(data);
          }
          match = true;
          break;
        }
      }

      if (!match) {
        std::vector<std::string> mPrfxResults = android::base::Split(mPrfx, ";");
        for (const std::string& mPrfxElement : mPrfxResults) {
          std::string mPrfxName = android::base::Trim(mPrfxElement);
          if (!mPrfxName.empty() && data.name.length() >= mPrfxName.length() &&
              data.name.substr(0, mPrfxName.length()) == mPrfxName) {
            result.files.push_back(data);
          }
        }
      }
    }
  }
  closedir(d);

  DataManager::GetValue(TW_GUI_SORT_ORDER, mSortOrder);
  std::sort(result.folders.begin(), result.folders.end(), fileSort);
  std::sort(result.files.begin(), result.files.end(), fileSort);
  return result;
}

}  // namespace gui2_backend
