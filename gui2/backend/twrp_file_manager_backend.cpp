#include "twrp_file_manager_backend.h"

#include "twrp_file_list.h"

namespace gui2_backend {

file_listing twrp_file_manager_backend::list(const std::string& folder, const file_filter& filter) {
  return twrp_file_list(folder, filter, false);
}

}  // namespace gui2_backend
