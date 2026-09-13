#pragma once

#include <string>
#include <vector>

class Args {
public:
  static std::vector<std::string> GetArgs(const int* argc, char*** argv);
};
