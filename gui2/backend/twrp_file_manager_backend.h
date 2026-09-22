#ifndef GUI2_BACKEND_TWRP_FILE_MANAGER_BACKEND_H
#define GUI2_BACKEND_TWRP_FILE_MANAGER_BACKEND_H

#include <string>
#include <vector>

#include "file_manager_backend.h"

namespace gui2_backend {

class twrp_file_manager_backend final : public file_manager_backend {
 public:
  std::vector<file_entry> list(const std::string& path) override;
  bool remove(const std::string& path) override;
  bool rename(const std::string& path, const std::string& name) override;
  bool set_mode(const std::string& path, const std::string& mode) override;
  bool copy(const std::string& path, const std::string& destination) override;
  bool move(const std::string& path, const std::string& destination) override;
  std::string start_directory() override;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_FILE_MANAGER_BACKEND_H
