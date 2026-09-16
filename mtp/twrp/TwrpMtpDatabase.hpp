/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 */

#ifndef TWRP_MTP_DATABASE_HPP
#define TWRP_MTP_DATABASE_HPP

#include <map>

#include "../IMtpDatabase.h"
#include "TwrpMtpStorage.hpp"

namespace android {
class TwrpMtpDatabase : public IMtpDatabase {
    std::map<MtpStorageID, TwrpMtpStorage*> storage_map_;

public:
    TwrpMtpDatabase();

    ~TwrpMtpDatabase() override;

    void createDB(MtpStorage* storage, MtpStorageID storage_id) override;

    MtpObjectHandle beginSendObject(const char* path,
                                    MtpObjectFormat format,
                                    MtpObjectHandle parent,
                                    MtpStorageID storage_id) override;

    void endSendObject(MtpObjectHandle handle,
                       bool succeeded) override;

    MtpObjectHandleList* getObjectList(MtpStorageID storage_id,
                                       MtpObjectFormat format,
                                       MtpObjectHandle parent) override;

    int getNumObjects(MtpStorageID storage_id,
                      MtpObjectFormat format,
                      MtpObjectHandle parent) override;

    // callee should delete[] the results from these
    // results can be NULL
    MtpObjectFormatList* getSupportedPlaybackFormats() override;

    MtpObjectFormatList* getSupportedCaptureFormats() override;

    MtpObjectPropertyList* getSupportedObjectProperties(MtpObjectFormat format) override;

    MtpDevicePropertyList* getSupportedDeviceProperties() override;

    MtpResponseCode getObjectPropertyValue(MtpObjectHandle handle,
                                           MtpObjectProperty property,
                                           MtpDataPacket& packet) override;

    MtpResponseCode setObjectPropertyValue(MtpObjectHandle handle,
                                           MtpObjectProperty property,
                                           MtpDataPacket& packet) override;

    MtpResponseCode getDevicePropertyValue(MtpDeviceProperty property,
                                           MtpDataPacket& packet) override;

    MtpResponseCode setDevicePropertyValue(MtpDeviceProperty property,
                                           MtpDataPacket& packet) override;

    MtpResponseCode resetDeviceProperty(MtpDeviceProperty property) override;

    MtpResponseCode getObjectPropertyList(MtpObjectHandle handle,
                                          uint32_t format, uint32_t property,
                                          int group_code, int depth,
                                          MtpDataPacket& packet) override;

    MtpResponseCode getObjectInfo(MtpObjectHandle handle,
                                  MtpObjectInfo& info) override;

    void* getThumbnail(MtpObjectHandle handle, size_t& out_thumb_size) override;

    MtpResponseCode getObjectFilePath(MtpObjectHandle handle,
                                      MtpStringBuffer& out_file_path,
                                      int64_t& out_file_length,
                                      MtpObjectFormat& out_format) override;

    static bool getObjectPropertyInfo(MtpObjectProperty property, int& type);

    static bool getDevicePropertyInfo(MtpDeviceProperty property, int& type);

    int openFilePath(const char* path, bool transcode) override;

    MtpObjectHandleList* getObjectReferences(MtpObjectHandle handle) override;

    MtpResponseCode setObjectReferences(MtpObjectHandle handle,
                                        MtpObjectHandleList* references) override;

    MtpProperty* getObjectPropertyDesc(MtpObjectProperty property,
                                       MtpObjectFormat format) override;

    MtpProperty* getDevicePropertyDesc(MtpDeviceProperty property) override;

    MtpResponseCode beginDeleteObject(MtpObjectHandle handle) override;

    void endDeleteObject(MtpObjectHandle handle, bool succeeded) override;

    void rescanFile(const char* path,
                    MtpObjectHandle handle,
                    MtpObjectFormat format) override;

    MtpResponseCode beginMoveObject(MtpObjectHandle handle, MtpObjectHandle new_parent,
                                    MtpStorageID new_storage) override;

    void endMoveObject(MtpObjectHandle old_parent, MtpObjectHandle new_parent,
                       MtpStorageID old_storage, MtpStorageID new_storage,
                       MtpObjectHandle handle, bool succeeded) override;

    MtpResponseCode beginCopyObject(MtpObjectHandle handle, MtpObjectHandle new_parent,
                                    MtpStorageID new_storage) override;

    void endCopyObject(MtpObjectHandle handle, bool succeeded) override;
};
}
#endif