#pragma once

#include <string>
#include <vector>

extern std::string stage;

class args {
    public:
        static std::vector<std::string> get_args(const int *argc, char*** const argv);
};