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
 * Additional Copyright (C) 2018 TeamWin
 */

#include "TwrpMtpServer.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <thread>

#include <android-base/properties.h>

#include "../MtpDebug.h"
#include "../MtpDescriptors.h"
#include "MtpMessage.hpp"
#include "TwrpMtpDatabase.hpp"

using namespace std::chrono_literals;
static constexpr auto kMtpInitDelay = 800ms;

namespace android {

TwrpMtpServer* TwrpMtpServer::Create() {
    // Gather device info from system properties.
    const std::string manufacturer =
            base::GetProperty("ro.build.product", "unknown manufacturer");
    const std::string model =
            base::GetProperty("ro.color597.product_name", "unknown model");
    const std::string serial_number =
            base::GetProperty("ro.serialno", "unknown serial number");

    // Sleep before opening the MTP USB device — some kernels aren't ready
    // immediately after sysfs requests.
    std::this_thread::sleep_for(kMtpInitDelay);

    int control_fd = -1;

#ifdef USB_MTP_DEVICE
#define STRINGIFY(x) #x
#define EXPAND(x) STRINGIFY(x)
    constexpr const char* kMtpDevice = EXPAND(USB_MTP_DEVICE);
    MTPI("Using '%s' for MTP device.\n", kMtpDevice);
#undef EXPAND
#undef STRINGIFY
#else
    constexpr auto kMtpDevice = "/dev/mtp_usb";
#endif

    if (access(FFS_MTP_EP0, W_OK) == 0) {
        MTPD("Opening FFS EP0\n");
        control_fd = open(FFS_MTP_EP0, O_RDWR);
    } else {
        control_fd = open(kMtpDevice, O_WRONLY);
    }

    if (control_fd < 0) {
        MTPE("could not open MTP driver, errno: %d\n", errno);
        return nullptr;
    }
    MTPD("MTP fd: %d\n", control_fd);

    auto* mtp_database = new TwrpMtpDatabase();
    auto* server = new TwrpMtpServer(mtp_database, control_fd, false, manufacturer.c_str(),
                                     model.c_str(), "None", serial_number.c_str());
    MTPI("created new TwrpMtpServer object\n");
    return server;
}

TwrpMtpServer::TwrpMtpServer(IMtpDatabase* database, const int controlFd, const bool ptp,
                             const char* deviceInfoManufacturer,
                             const char* deviceInfoModel,
                             const char* deviceInfoDeviceVersion,
                             const char* deviceInfoSerialNumber)
    : MtpServer(database, controlFd, ptp,
                deviceInfoManufacturer, deviceInfoModel,
                deviceInfoDeviceVersion, deviceInfoSerialNumber) {
}

TwrpMtpServer::~TwrpMtpServer() = default;

void TwrpMtpServer::Start() {
    AddStorage();
    MTPD("Starting add / remove mtppipe monitor thread\n");
    std::thread(&TwrpMtpServer::MtpPipeThread, this).detach();
    // Restart the MTP process if the device is unplugged and replugged in.
    for (;;) {
        run();
        std::this_thread::sleep_for(kMtpInitDelay);
    }
}

void TwrpMtpServer::SetStorages(const Storages* mtp_storages) {
    storages_ = mtp_storages;
}

void TwrpMtpServer::SendObjectAdded(const int handle) {
    sendObjectAdded(handle);
}

void TwrpMtpServer::SendObjectRemoved(const int handle) {
    sendObjectRemoved(handle);
}

void TwrpMtpServer::AddStorage() {
    MTPD("TwrpMtpServer::AddStorage count of storage devices: %zu\n", storages_->size());
    for (const auto& [display, mount, mtp_id, max_file_size] : *storages_) {
        if (mount.empty() || display.empty()) continue;
        constexpr bool kRemovable = false;
        auto* storage = new TwrpMtpStorage(mtp_id, mount.c_str(), display.c_str(),
                                           kRemovable, max_file_size, this);
        addStorage(storage);
    }
}

void TwrpMtpServer::RemoveStorage(const MtpStorageID storage_id) {
    if (MtpStorage* storage = getStorage(storage_id)) {
        MTPD("TwrpMtpServer::RemoveStorage calling removeStorage\n");
        removeStorage(storage);
    }
    MTPD("TwrpMtpServer::RemoveStorage DONE\n");
}

void TwrpMtpServer::SetReadPipe(const int pipe) {
    mtp_read_pipe_ = pipe;
}

// ---------------------------------------------------------------------------
//  Pipe monitoring thread
// ---------------------------------------------------------------------------

int TwrpMtpServer::MtpPipeThread() {
    if (mtp_read_pipe_ == -1) {
        MTPD("MtppipeThread exiting because mtp_read_pipe not set\n");
        return 0;
    }
    MTPD("Starting TwrpMtpServer::MtppipeThread\n");
    MtpMessage mtp_message{};
    for (;;) {
        const ssize_t read_count = read(mtp_read_pipe_, &mtp_message, sizeof(mtp_message));
        MTPD("read %zd from mtppipe\n", read_count);
        if (read_count == static_cast<ssize_t>(sizeof(mtp_message))) {
            if (mtp_message.message_type == MTP_MESSAGE_ADD_STORAGE) {
                MTPI("mtppipe add storage %u '%s'\n", mtp_message.storage_id, mtp_message.path);
                if (mtp_message.storage_id) {
                    constexpr bool kRemovable = false;
                    auto* storage = new TwrpMtpStorage(mtp_message.storage_id, &mtp_message.path[0],
                                                       &mtp_message.display[0], kRemovable,
                                                       mtp_message.max_file_size, this);
                    addStorage(storage);
                    MTPD("mtppipe done adding storage\n");
                } else {
                    MTPE("Invalid storage ID %u specified\n", mtp_message.storage_id);
                }
            } else if (mtp_message.message_type == MTP_MESSAGE_REMOVE_STORAGE) {
                MTPI("mtppipe remove storage %i\n", mtp_message.storage_id);
                RemoveStorage(mtp_message.storage_id);
                MTPD("mtppipe done removing storage\n");
            } else {
                MTPE("Unknown mtppipe message value: %i\n", mtp_message.message_type);
            }
        } else {
            MTPE("TwrpMtpServer::MtppipeThread unexpected read_count %zd\n", read_count);
            close(mtp_read_pipe_);
            break;
        }
    }
    MTPD("TwrpMtpServer::MtppipeThread closing\n");
    return 0;
}

}