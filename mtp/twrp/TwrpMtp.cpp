/*
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
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

#include "TwrpMtp.hpp"

#include <cstdlib>
#include <unistd.h>

#include "../MtpDebug.h"

namespace android {

TwrpMtp::TwrpMtp(const int debug_enabled) {
  if (debug_enabled) MtpDebug::enableDebug();
}

void TwrpMtp::Start() const {
  MTPI("Starting MTP\n");
  auto* mtp = TwrpMtpServer::Create();
  if (!mtp) {
    MTPE("Failed to create MTP server\n");
    return;
  }
  mtp->SetStorages(&mtp_storages_);
  mtp->SetReadPipe(mtp_read_pipe_);
  mtp->Start();
}

pid_t TwrpMtp::ForkServer(int mtp_pipe[2]) {
  const pid_t pid = fork();
  if (pid == -1) {
    MTPE("MTP fork failed.\n");
    return 0;
  }
  if (pid == 0) {
    // Child process
    close(mtp_pipe[1]); // Child closes write side
    mtp_read_pipe_ = mtp_pipe[0];
    Start();
    MTPD("MTP child process exited.\n");
    close(mtp_pipe[0]);
    std::_Exit(0);
  }
  MTPD("MTP child PID: %d\n", pid);
  return pid;
}

void TwrpMtp::AddStorage(const std::string& display, const std::string& path, const int mtp_id,
                         const uint64_t max_file_size) {
  mtp_storages_.push_back({display, path, mtp_id, max_file_size});
  MTPD("TwrpMtp mtpid: %d\n", mtp_id);
}

}