/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
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
 */

#ifndef TWRP_MTP_STORAGE_HPP
#define TWRP_MTP_STORAGE_HPP

#include "../MtpObjectInfo.h"
#include "../MtpServer.h"
#include "../MtpStorage.h"
#include "../MtpTypes.h"

#include <sys/inotify.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace android {

// An event raised by the inotify thread and sent to the host once the tree
// lock has been released, so a stalled USB write cannot block the MTP thread.
struct PendingEvent {
    MtpEventCode code;
    MtpObjectHandle handle;
};

// Per-property storage for an MTP filesystem entry.
struct MtpPropertyEntry {
    MtpPropertyCode property = 0;
    MtpDataType data_type = 0;
    uint64_t value_int = 0;
    std::string value_str;
};

// A filesystem entry (file or directory).  Directory entries own their
// children via unique_ptr; the root_ member owns the whole tree through
// this chain.
struct MtpEntry {
    MtpObjectHandle handle = 0;
    MtpObjectHandle parent = 0;
    std::string name;             // name only, without path
    bool is_dir = false;
    std::vector<MtpPropertyEntry> properties;
    // Only meaningful for directories:
    std::map<MtpObjectHandle, std::unique_ptr<MtpEntry>> children;
    bool already_read = false;
};

class TwrpMtpStorage : public MtpStorage {

public:
    struct PropEntry {
        MtpObjectHandle handle = 0;
        uint16_t property = 0;
        uint16_t data_type = 0;
        uint64_t int_value = 0;
        std::string str_value;
    };

private:
    // Serializes tree and index access between the MTP thread and the inotify
    // thread.  Recursive because the tree helpers below call each other.
    mutable std::recursive_mutex tree_mutex_;
    MtpEntry root_;                                      // handle 0, owns the whole tree
    std::unordered_map<MtpObjectHandle, MtpEntry*> dir_index_;    // directory handle -> MtpEntry*
    std::unordered_map<MtpObjectHandle, MtpEntry*> node_index_;   // every handle -> MtpEntry* (O(1) lookup)
    std::string mtp_storage_parent_;
    MtpObjectHandle handle_currently_sending_ = 0;
    int inotify_fd_ = -1;
    std::map<int, MtpObjectHandle> inotify_map_;                   // inotify wd -> directory handle
    bool send_events_ = false;
    MtpServer* server_ = nullptr;
    std::atomic_bool inotify_thread_kill_{};
    std::thread inotify_thread_;

    MtpEntry* FindNode(MtpObjectHandle handle);
    MtpEntry* FindTree(MtpObjectHandle parent);
    std::string GetNodePath(const MtpEntry* entry);
    MtpEntry* AddNewNode(bool is_dir, MtpEntry* parent_dir, const std::string& name);
    void QueryNodeProperties(std::vector<PropEntry>& results, const MtpEntry* entry,
                             uint32_t property, int group_code, MtpStorageID storage_id);
    void QueryChildProperties(std::vector<PropEntry>& results, const MtpEntry* dir,
                              uint32_t property, int group_code, bool recursive);
    void CollectSubtreeHandles(const MtpEntry* entry, std::vector<MtpObjectHandle>& handles);
    int AddInotify(MtpEntry* dir_entry);
    void HandleInotifyEvent(const inotify_event* event);
    void HandleInotifyEventLocked(const inotify_event* event, std::vector<PendingEvent>& events);
    void StartInotifyThread();
    int InotifyLoop();

public:
    TwrpMtpStorage(MtpStorageID id, const char* file_path,
                   const char* description, bool removable, uint64_t max_file_size,
                   MtpServer* ref_server);
    ~TwrpMtpStorage() override;

    int RenameObject(MtpObjectHandle handle, const std::string& new_name);
    MtpObjectHandle BeginSendObject(const char* path, MtpObjectFormat format,
                                    MtpObjectHandle parent, uint64_t size, time_t modified);
    MtpObjectHandleList* GetObjectList(MtpStorageID storage_id, MtpObjectHandle parent);
    int GetNumObjects(MtpObjectHandle parent);
    // Collects the property entries the request selects on this storage into
    // `results`: the entry itself for a handle, its direct children for depth 1,
    // and the children of the storage root for handle 0, recursively for handle
    // 0xFFFFFFFF.  Returns 0 on success, -1 when the handle is unknown.
    // Serializing the answer is WritePropertyList()'s job, because a request that
    // spans storages has to be answered with a single count.
    int GetObjectPropertyList(MtpObjectHandle handle, uint32_t format, uint32_t property,
                              int group_code, int depth, std::vector<PropEntry>& results);
    // Writes a collected property list into the packet, count first.
    static void WritePropertyList(const std::vector<PropEntry>& results, MtpDataPacket& packet);
    int ReadDir(const std::string& path, MtpEntry* dir_entry);
    int GetObjectPropertyValue(MtpObjectHandle handle, MtpObjectProperty property,
                               PropEntry& prop_entry);
    int GetObjectInfo(MtpObjectHandle handle, MtpObjectInfo& info);
    void EndSendObject(const char* path, MtpObjectHandle handle, MtpObjectFormat format,
                       bool succeeded);
    int GetObjectFilePath(MtpObjectHandle handle, MtpStringBuffer& out_file_path,
                          int64_t& out_file_length, MtpObjectFormat& out_format);
    int DeleteFile(MtpObjectHandle handle);
    int CreateDb();
};

}

#endif  // TWRP_MTP_STORAGE_HPP