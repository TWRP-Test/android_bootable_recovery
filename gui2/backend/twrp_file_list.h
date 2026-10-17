#ifndef GUI2_BACKEND_TWRP_FILE_LIST_H
#define GUI2_BACKEND_TWRP_FILE_LIST_H

#include <string>

#include "file_manager_backend.h"

namespace gui2_backend {

// GUIFileSelector::GetFileList and fileSort, without "." and "..": gui2 draws
// its own row for the way up. create is the <path create="1"> attribute.
file_listing twrp_file_list(const std::string& folder, const file_filter& filter, bool create);

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_FILE_LIST_H
