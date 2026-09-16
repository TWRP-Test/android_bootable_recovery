/*
 * Copyright (C) 2018 TeamWin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TWRPMTP_HPP
#define TWRPMTP_HPP

#include <sys/types.h>

#include <cstdint>
#include <string>

#include "TwrpMtpServer.hpp"

namespace android {

class TwrpMtp {
public:
    explicit TwrpMtp(int debug_enabled = 0);

    pid_t ForkServer(int mtp_pipe[2]);

    void AddStorage(const std::string& display, const std::string& path, int mtp_id,
                    uint64_t max_file_size);

private:
    void Start() const;

    Storages mtp_storages_;
    int mtp_read_pipe_ = -1;
};

}
#endif