/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 */

#ifndef TWRP_MTP_SERVER_HPP
#define TWRP_MTP_SERVER_HPP

#include <string>
#include <vector>

#include "../MtpServer.h"
#include "../MtpStorage.h"
#include "../MtpStringBuffer.h"

namespace android {

struct Storage {
    std::string display;
    std::string mount;
    int mtp_id = 0;
    uint64_t max_file_size = 0;
};

using Storages = std::vector<Storage>;

class TwrpMtpServer : public MtpServer {
public:
    // Factory: opens the MTP device, creates the TwrpMtpDatabase,
    // reads device info from system properties, and constructs the server.
    // Returns nullptr if the MTP device fd cannot be opened.
    static TwrpMtpServer* Create();

    ~TwrpMtpServer() override;

    void Start();
    void AddStorage();
    void RemoveStorage(MtpStorageID storage_id);
    void SetStorages(const Storages* mtp_storages);
    void SetReadPipe(int pipe);
    void SendObjectAdded(int handle);
    void SendObjectRemoved(int handle);

private:
    // Constructor is private — use Create() to construct.
    TwrpMtpServer(IMtpDatabase* database, int controlFd, bool ptp,
                  const char* deviceInfoManufacturer,
                  const char* deviceInfoModel,
                  const char* deviceInfoDeviceVersion,
                  const char* deviceInfoSerialNumber);

    int MtpPipeThread();

    const Storages* storages_ = nullptr;
    int mtp_read_pipe_ = -1;
};

}

#endif  // TWRP_MTP_SERVER_HPP