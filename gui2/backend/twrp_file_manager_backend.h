#ifndef GUI2_BACKEND_TWRP_FILE_MANAGER_BACKEND_H
#define GUI2_BACKEND_TWRP_FILE_MANAGER_BACKEND_H

#include <string>
#include <vector>

#include "file_manager_backend.h"

namespace gui2_backend {

class twrp_file_manager_backend final : public file_manager_backend {
 public:
  file_listing list(const std::string& folder, const file_filter& filter) override;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_FILE_MANAGER_BACKEND_H
