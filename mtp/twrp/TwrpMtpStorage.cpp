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

#define LOG_TAG "TwrpMtpStorage"

#include "TwrpMtpStorage.hpp"

#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <ranges>
#include <span>
#include <thread>
#include <vector>

#include "../MtpDebug.h"

namespace fs = std::filesystem;

namespace android {
namespace {
constexpr uint32_t kWatchFlags = IN_CREATE | IN_DELETE | IN_MOVE | IN_MODIFY;

const MtpPropertyEntry* FindProperty(const MtpEntry* entry, MtpPropertyCode property) {
    for (const auto& p : entry->properties) {
        if (p.property == property) return &p;
    }
    return nullptr;
}

void UpdateProperty(MtpEntry* entry, const MtpPropertyCode property, const uint64_t value_int,
                    const std::string& value_str, const MtpDataType data_type) {
    for (auto& [property_, data_type_, value_int_, value_str_] : entry->properties) {
        if (property_ == property) {
            value_int_ = value_int;
            value_str_ = value_str;
            data_type_ = data_type;
            return;
        }
    }
    entry->properties.push_back({
            .property = property,
            .data_type = data_type,
            .value_int = value_int,
            .value_str = value_str
    });
}

void AddProperties(MtpEntry* entry, const std::string& path, const MtpStorageID storageID) {
    MTPD("addProperties: handle: %u, filename: '%s'\n", entry->handle, entry->name.c_str());
    // The properties are a complete snapshot of the entry, so a re-read of the
    // same entry (re-sent object, renamed object) replaces them instead of
    // appending a second, stale set.
    entry->properties.clear();
    struct stat st{};
    int mFormat = MTP_FORMAT_UNDEFINED; // file
    off_t file_size = 0;

    if (lstat(path.c_str(), &st) == 0) {
        file_size = st.st_size;
        if (S_ISDIR(st.st_mode)) mFormat = MTP_FORMAT_ASSOCIATION; // folder
    }

    const uint64_t puid = (static_cast<uint64_t>(storageID) << 32) + entry->handle;

    entry->properties.push_back({MTP_PROPERTY_STORAGE_ID, MTP_TYPE_UINT32,
                                 static_cast<uint64_t>(storageID), ""});
    entry->properties.push_back(
            {MTP_PROPERTY_OBJECT_FORMAT, MTP_TYPE_UINT16, static_cast<uint64_t>(mFormat), ""});
    entry->properties.push_back({MTP_PROPERTY_PROTECTION_STATUS, MTP_TYPE_UINT16, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_OBJECT_SIZE, MTP_TYPE_UINT64,
                                 static_cast<uint64_t>(file_size), ""});
    entry->properties.push_back({MTP_PROPERTY_OBJECT_FILE_NAME, MTP_TYPE_STR, 0, entry->name});
    entry->properties.push_back({MTP_PROPERTY_DATE_MODIFIED, MTP_TYPE_UINT64,
                                 static_cast<uint64_t>(st.st_mtime), ""});
    entry->properties.push_back({MTP_PROPERTY_PARENT_OBJECT, MTP_TYPE_UINT32, entry->parent, ""});
    entry->properties.push_back({MTP_PROPERTY_PERSISTENT_UID, MTP_TYPE_UINT128, puid, ""});
    entry->properties.push_back({MTP_PROPERTY_NAME, MTP_TYPE_STR, 0, entry->name});
    entry->properties.push_back({MTP_PROPERTY_DISPLAY_NAME, MTP_TYPE_STR, 0, entry->name});
    entry->properties.push_back({MTP_PROPERTY_DATE_ADDED, MTP_TYPE_UINT64,
                                 static_cast<uint64_t>(st.st_mtime), ""});
    entry->properties.push_back({MTP_PROPERTY_DESCRIPTION, MTP_TYPE_STR, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_ARTIST, MTP_TYPE_STR, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_ALBUM_NAME, MTP_TYPE_STR, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_ALBUM_ARTIST, MTP_TYPE_STR, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_TRACK, MTP_TYPE_UINT16, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_ORIGINAL_RELEASE_DATE, MTP_TYPE_UINT64, 2014, ""});
    entry->properties.push_back({MTP_PROPERTY_DURATION, MTP_TYPE_UINT32, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_GENRE, MTP_TYPE_STR, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_COMPOSER, MTP_TYPE_STR, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_ARTIST, MTP_TYPE_STR, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_ALBUM_NAME, MTP_TYPE_STR, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_DURATION, MTP_TYPE_UINT32, 0, ""});
    entry->properties.push_back({MTP_PROPERTY_DESCRIPTION, MTP_TYPE_STR, 0, ""});
}

MtpEntry* FindEntryByName(const MtpEntry* dir_entry, const std::string& name) {
    for (const auto& child : dir_entry->children | std::views::values) {
        if (child->name == name && child->handle > 0) return child.get();
    }
    return nullptr;
}

void RenameEntry(MtpEntry* entry, const std::string& new_name) {
    entry->name = new_name;
    UpdateProperty(entry, MTP_PROPERTY_OBJECT_FILE_NAME, 0, entry->name, MTP_TYPE_STR);
    UpdateProperty(entry, MTP_PROPERTY_NAME, 0, entry->name, MTP_TYPE_STR);
    UpdateProperty(entry, MTP_PROPERTY_DISPLAY_NAME, 0, entry->name, MTP_TYPE_STR);
}

// Raise the queued inotify events.  Called without the tree lock held.
void SendEvents(MtpServer* server, const std::vector<PendingEvent>& events) {
    for (const auto& event : events) {
        switch (event.code) {
            case MTP_EVENT_OBJECT_ADDED:
                server->sendObjectAdded(event.handle);
                break;
            case MTP_EVENT_OBJECT_REMOVED:
                server->sendObjectRemoved(event.handle);
                break;
            case MTP_EVENT_OBJECT_INFO_CHANGED:
                server->sendObjectInfoChanged(event.handle);
                break;
            case MTP_EVENT_OBJECT_PROP_CHANGED:
                server->sendObjectUpdated(event.handle);
                break;
            default:
                MTPE("unknown event code %x for handle %u\n", event.code, event.handle);
                break;
        }
    }
}
} // namespace

TwrpMtpStorage::TwrpMtpStorage(const MtpStorageID id, const char* file_path,
                               const char* description, const bool removable,
                               const uint64_t max_file_size,
                               MtpServer* ref_server)
    : MtpStorage(id, file_path, description, removable, max_file_size), server_(ref_server) {
    MTPD("TwrpMtpStorage id: %d path: %s\n", id, file_path);
}

TwrpMtpStorage::~TwrpMtpStorage() {
    if (inotify_thread_.joinable()) {
        inotify_thread_kill_ = true;
        MTPD("joining inotify_thread_ after sending the kill notification.\n");
        inotify_thread_.join();
        MTPD("~TwrpMtpStorage removing inotify watches and closing inotify_fd_\n");
        for (const auto& wd : inotify_map_ | std::views::keys) {
            inotify_rm_watch(inotify_fd_, wd);
        }
        close(inotify_fd_);
        inotify_map_.clear();
    }
    // root_ destructor handles all tree cleanup automatically via unique_ptr cascade
}

int TwrpMtpStorage::RenameObject(const MtpObjectHandle handle, const std::string& new_name) {
    MTPD("TwrpMtpStorage::RenameObject, handle: %u, new name: '%s'\n", handle, new_name.c_str());
    // The inotify thread reports this same rename as an IN_MOVED_FROM/IN_MOVED_TO
    // pair.  Holding the lock across the rename and the entry update makes those
    // two events no-ops (neither the old nor the new name resolves to a node
    // that still needs changing) instead of a delete plus a re-add that would
    // hand the object a new handle behind the host's back.
    const std::lock_guard guard(tree_mutex_);
    // Root cannot be renamed.
    if (handle == MTP_PARENT_ROOT) {
        MTPE("cannot rename root\n");
        return -1;
    }
    // MTP names must be non-empty and must not contain '/'.
    if (new_name.empty() || new_name.find('/') != std::string::npos) {
        MTPE("invalid object name: '%s'\n", new_name.c_str());
        return -1;
    }
    MtpEntry* entry = FindNode(handle);
    if (!entry) {
        MTPE("handle %u not found\n", handle);
        return -1;
    }
    // No-op: name unchanged.
    if (new_name == entry->name) {
        MTPD("name unchanged, nothing to do\n");
        return 0;
    }

    const MtpEntry* parent_entry = FindNode(entry->parent);
    if (!parent_entry) {
        MTPE("parent of handle %u not found\n", handle);
        return -1;
    }
    const auto parentPath = fs::path(GetNodePath(parent_entry));
    const auto oldPath = parentPath / entry->name;
    const auto newPath = parentPath / new_name;
    MTPD("old: '%s', new: '%s'\n", oldPath.c_str(), newPath.c_str());
    std::error_code ec;
    fs::rename(oldPath, newPath, ec);
    if (ec) {
        MTPE("rename(%s, %s) failed: %s\n", oldPath.c_str(), newPath.c_str(), ec.message().c_str());
        return -1;
    }
    RenameEntry(entry, new_name);
    return 0;
}

MtpObjectHandle TwrpMtpStorage::BeginSendObject(const char* path, const MtpObjectFormat format,
                                                const MtpObjectHandle parent,
                                                [[maybe_unused]] uint64_t size,
                                                [[maybe_unused]] time_t modified) {
    MTPD("TwrpMtpStorage::BeginSendObject(), path: '%s', parent: %u, format: %04x\n",
         path, parent, format);
    const std::lock_guard guard(tree_mutex_);
    MtpEntry* parent_entry = FindTree(parent);
    if (!parent_entry) {
        MTPE("parent node not found, returning error\n");
        return kInvalidObjectHandle;
    }

    const fs::path file_path(path);
    if (!file_path.has_parent_path()) {
        MTPE("path has no parent directory, returning error\n");
        return kInvalidObjectHandle;
    }
    const std::string parent_dir = file_path.parent_path();
    const std::string basename = file_path.filename();
    if (parent != 0 && parent_dir != GetNodePath(parent_entry)) {
        MTPE("BeginSendObject into path '%s' but parent tree has path '%s', returning error\n",
             parent_dir.c_str(), GetNodePath(parent_entry).c_str());
        return kInvalidObjectHandle;
    }

    MTPD("TwrpMtpStorage::BeginSendObject() parentdir: %s basename: %s\n", parent_dir.c_str(),
         basename.c_str());
    const MtpEntry* entry = AddNewNode(format == MTP_FORMAT_ASSOCIATION, parent_entry, basename);
    handle_currently_sending_ = entry->handle;

    return entry->handle;
}

int TwrpMtpStorage::CreateDb() {
    const std::lock_guard guard(tree_mutex_);
    mtp_storage_parent_ = getPath();
    // root directory is special: handle 0, parent 0, and empty path
    root_.is_dir = true;
    dir_index_[0] = &root_;
    node_index_[0] = &root_;
    send_events_ = true;
    MTPD("inotify_init\n");
    inotify_fd_ = inotify_init();
    if (inotify_fd_ < 0) {
        MTPE("Can't run inotify_init for mtp server: %s\n", strerror(errno));
    } else {
        MTPD("Starting inotify thread\n");
        StartInotifyThread();
    }
    // for debugging and caching purposes, read the root dir already now
    ReadDir(mtp_storage_parent_, &root_);
    // all other dirs are read on demand
    MTPD("TwrpMtpStorage::CreateDb DONE\n");
    return 0;
}

MtpEntry* TwrpMtpStorage::FindNode(const MtpObjectHandle handle) {
    const std::lock_guard guard(tree_mutex_);
    if (const auto it = node_index_.find(handle); it != node_index_.end()) {
        MtpEntry* entry = it->second;
        MTPD("FindNode: found entry %p for handle %u, name: %s\n", entry, handle,
             entry->name.c_str());
        return entry;
    }
    MTPD("TwrpMtpStorage::FindNode: no entry found for handle %u on storage %u\n",
         handle, getStorageID());
    return nullptr;
}

MtpEntry* TwrpMtpStorage::FindTree(MtpObjectHandle parent) {
    const std::lock_guard guard(tree_mutex_);
    if (parent == MTP_PARENT_ROOT) {
        MTPD("parent == MTP_PARENT_ROOT\n");
        parent = 0;
    }

    const auto it = dir_index_.find(parent);
    if (it == dir_index_.end()) {
        MTPE("parent handle not found\n");
        return nullptr;
    }

    MtpEntry* dir = it->second;
    if (!dir->already_read) {
        const std::string path = GetNodePath(dir);
        MTPD("reading directory on demand for entry %p (%u), path: %s\n", dir, dir->handle,
             path.c_str());
        ReadDir(path, dir);
    }
    return dir;
}

std::string TwrpMtpStorage::GetNodePath(const MtpEntry* entry) {
    const std::lock_guard guard(tree_mutex_);
    MTPD("GetNodePath: entry %p, handle %u\n", entry, entry->handle);
    fs::path path = entry->name;
    while (entry->parent != 0) {
        entry = FindNode(entry->parent);
        path = entry->name / path;
    }
    path = mtp_storage_parent_ / path;
    MTPD("GetNodePath: path %s\n", path.c_str());
    return path;
}

MtpObjectHandleList* TwrpMtpStorage::GetObjectList([[maybe_unused]] MtpStorageID storage_id,
                                                   const MtpObjectHandle parent) {
    MTPD("TwrpMtpStorage::GetObjectList, parent: %u\n", parent);
    const std::lock_guard guard(tree_mutex_);
    auto* list = new MtpObjectHandleList();

    MtpEntry* dir = FindTree(parent);
    if (!dir) return list;

    for (const auto& handle : dir->children | std::views::keys) list->push_back(handle);
    MTPD("returning %u objects in %s.\n", list->size(), dir->name.c_str());
    return list;
}

int TwrpMtpStorage::GetNumObjects(const MtpObjectHandle parent) {
    MTPD("TwrpMtpStorage::GetNumObjects, parent: %u\n", parent);

    const std::lock_guard guard(tree_mutex_);
    if (const MtpEntry* dir = FindTree(parent)) {
        return static_cast<int>(dir->children.size());
    }
    return 0;
}

MtpEntry* TwrpMtpStorage::AddNewNode(const bool is_dir, MtpEntry* parent_dir,
                                     const std::string& name) {
    const std::lock_guard guard(tree_mutex_);
    // Handle IDs are session-unique across all storages, and every storage's
    // inotify thread allocates concurrently with the server thread, so this
    // counter escapes the reach of this instance's tree_mutex_ and must be
    // atomic or two storages hand out the same handle.
    static std::atomic<MtpObjectHandle> mtp_id{0};

    // A directory never holds two entries with the same name.  Reuse the node
    // that is already there (for example the one BeginSendObject created) so
    // that the name keeps mapping to a single handle.
    if (MtpEntry* existing = FindEntryByName(parent_dir, name)) {
        MTPD("reusing handle %u for existing node '%s'\n", existing->handle, name.c_str());
        existing->is_dir = existing->is_dir || is_dir;
        return existing;
    }

    const MtpObjectHandle handle = ++mtp_id;
    MTPD("adding new %s node for %s, new handle: %u\n",
         is_dir ? "dir" : "file", name.c_str(), handle);
    MTPD("parent dir: %x, handle: %u, name: %s\n",
         parent_dir, parent_dir->handle, parent_dir->name.c_str());

    auto entry = std::make_unique<MtpEntry>();
    entry->handle = handle;
    entry->parent = parent_dir->handle;
    entry->name = name;
    entry->is_dir = is_dir;

    MtpEntry* raw = entry.get();
    node_index_[handle] = raw;
    if (is_dir) dir_index_[handle] = raw;
    parent_dir->children[handle] = std::move(entry);
    return raw;
}

int TwrpMtpStorage::ReadDir(const std::string& path, MtpEntry* dir_entry) {
    const std::lock_guard guard(tree_mutex_);
    const auto storageID = getStorageID();
    const MtpObjectHandle parent = dir_entry->handle;
    MTPD("reading dir '%s', parent handle %u\n", path.c_str(), parent);

    // directory_iterator skips "." and ".." automatically.  symlink_status
    // mirrors the original lstat (does not follow symlinks), keeping the
    // is_dir decision consistent with AddProperties' own lstat.
    std::error_code ec;
    fs::directory_iterator it(path, ec);
    if (ec) {
        MTPE("error opening '%s' -- error: %s\n", path.c_str(), ec.message().c_str());
        return -1;
    }
    const fs::directory_iterator end;
    while (it != end) {
        const fs::directory_entry& de = *it;
        const auto st = de.symlink_status(ec);
        if (ec) {
            MTPE("Error running lstat on '%s'\n", de.path().c_str());
            return -1;
        }
        const bool is_dir = st.type() == fs::file_type::directory;
        const fs::path& item = de.path();
        MtpEntry* node = AddNewNode(is_dir, dir_entry, item.filename());
        AddProperties(node, item, storageID);
        it.increment(ec);
        if (ec) {
            MTPE("error iterating '%s' -- error: %s\n", path.c_str(), ec.message().c_str());
            break;
        }
    }
    dir_entry->already_read = true;
    AddInotify(dir_entry);
    return 0;
}

int TwrpMtpStorage::AddInotify(MtpEntry* dir_entry) {
    const std::lock_guard guard(tree_mutex_);
    if (inotify_fd_ < 0) {
        MTPE("inotify_fd_ not set or error: %i\n", inotify_fd_);
        return -1;
    }
    const std::string path = GetNodePath(dir_entry);
    MTPD("adding inotify for dir %x, dir: %s\n", dir_entry, path.c_str());
    const int wd = inotify_add_watch(inotify_fd_, path.c_str(), kWatchFlags);
    if (wd < 0) {
        MTPE("inotify_add_watch failed: %s\n", strerror(errno));
        return -1;
    }
    inotify_map_[wd] = dir_entry->handle;
    return 0;
}

void TwrpMtpStorage::StartInotifyThread() {
    inotify_thread_ = std::thread(&TwrpMtpStorage::InotifyLoop, this);
    MTPD("inotify thread started\n");
}

int TwrpMtpStorage::InotifyLoop() {
    constexpr size_t kEventSize = sizeof(inotify_event);
    constexpr size_t kEventBufLen = 1024 * (kEventSize + 16);

    MTPD("inotify thread starting.\n");

    std::array<char, kEventBufLen> buffer{};
    fd_set read_set;
    while (!inotify_thread_kill_) {
        FD_ZERO(&read_set);
        FD_SET(inotify_fd_, &read_set);
        timeval timeout{.tv_sec = 0, .tv_usec = 25000};
        const int select_result = select(inotify_fd_ + 1, &read_set, nullptr, nullptr, &timeout);
        if (select_result < 0) {
            if (errno == EINTR) continue;
            MTPE("select failed: %s\n", strerror(errno));
            continue;
        }

        if (select_result > 0) {
            const ssize_t bytes_read = read(inotify_fd_, buffer.data(), buffer.size());
            if (bytes_read < 0) {
                if (errno == EINTR) continue;
                MTPE("can't read inotify events: %s\n", strerror(errno));
                continue;
            }

            std::span<const char> events(buffer.data(), static_cast<size_t>(bytes_read));
            size_t offset = 0;
            while (offset < events.size() && !inotify_thread_kill_) {
                const auto* event = reinterpret_cast<const inotify_event*>(events.data() + offset);
                // An event that is about the watch itself - IN_IGNORED,
                // IN_DELETE_SELF, IN_MOVE_SELF, IN_UNMOUNT, IN_Q_OVERFLOW - has no
                // name, so the len test below keeps it out of HandleInotifyEvent().
                // Of those, only the overflow needs reporting: it means events were
                // dropped and what the tree shows may be out of date.
                if ((event->mask & IN_Q_OVERFLOW) != 0) {
                    MTPE("inotify queue overflowed, events were lost\n");
                } else if (event->len != 0) {
                    MTPD("inotify event: wd: %i, mask: %x, name: %s\n", event->wd, event->mask,
                         event->name);
                    HandleInotifyEvent(event);
                }
                offset += kEventSize + event->len;
            }
        }
    }
    MTPD("inotify_thread_kill_ received!\n");
    return 0;
}

void TwrpMtpStorage::HandleInotifyEvent(const inotify_event* event) {
    std::vector<PendingEvent> events;
    {
        const std::lock_guard guard(tree_mutex_);
        HandleInotifyEventLocked(event, events);
    }
    // Raised after the lock is released, so a stalled USB write cannot block the
    // MTP thread, which is waiting for that same lock.
    SendEvents(server_, events);
}

void TwrpMtpStorage::HandleInotifyEventLocked(const inotify_event* event,
                                              std::vector<PendingEvent>& events) {
    const auto it = inotify_map_.find(event->wd);
    if (it == inotify_map_.end()) {
        MTPE("unable to locate inotify_wd: %i\n", event->wd);
        return;
    }
    const MtpObjectHandle dir_handle = it->second;
    const auto dir_it = dir_index_.find(dir_handle);
    MtpEntry* dir = dir_it != dir_index_.end() ? dir_it->second : nullptr;
    if (dir == nullptr) {
        MTPE("inotify watch %i maps to unknown dir handle %u\n", event->wd, dir_handle);
        return;
    }
    MTPD("inotify dir: %p '%s'\n", static_cast<void*>(dir), dir->name.c_str());

    // fs::path::filename replaces POSIX basename()
    const std::string event_name = fs::path(event->name).filename().string();
    const auto mask = event->mask;
    MtpEntry* entry = FindEntryByName(dir, event_name);

    // The send guard exists only to hide the file the host is uploading right
    // now, so it applies to the events such a transfer raises (IN_CREATE,
    // IN_MODIFY) and never to a delete or a move.  Swallowing those leaves a
    // node in the tree for a file that is gone, and every handle the host
    // still holds for it fails with MTP_RESPONSE_INVALID_OBJECT_HANDLE.
    if (entry != nullptr && entry->handle == handle_currently_sending_ &&
        mask & (IN_CREATE | IN_MODIFY)) {
        MTPD("ignoring inotify event for currently uploading file, handle: %u\n", entry->handle);
        return;
    }

    // A rename done on the device itself arrives as IN_MOVED_FROM (old name)
    // followed by IN_MOVED_TO (new name) in the same batch, and is handled as
    // the two things the host understands: the old object is removed and the
    // new name is added under a fresh handle.  MtpStorageManager does exactly
    // the same - its observer calls handleRemovedObject() for MOVED_FROM and
    // handleAddedObject() (new id, OBJECT_ADDED) for MOVED_TO.  Reusing the old
    // handle instead would leave every host that only re-reads an object when
    // its handle changes showing the old name.
    if (mask & (IN_CREATE | IN_MOVED_TO)) {
        const bool is_dir = (mask & IN_ISDIR) != 0;
        MTPD("inotify create is %s\n", is_dir ? "dir" : "file");
        if (entry == nullptr) {
            entry = AddNewNode(is_dir, dir, event_name);
            const std::string item = (fs::path(GetNodePath(dir)) / event_name).string();
            AddProperties(entry, item, getStorageID());
            if (send_events_) events.push_back({MTP_EVENT_OBJECT_ADDED, entry->handle});
        } else {
            MTPD("inotify item already exists.\n");
        }
    } else if (mask & (IN_DELETE | IN_MOVED_FROM)) {
        const bool is_dir = (mask & IN_ISDIR) != 0;
        MTPD("inotify %s %s deleted\n", is_dir ? "directory" : "file", event->name);
        if (entry != nullptr) {
            // Every handle that goes away with this entry has to be reported:
            // a host that keeps asking about a descendant of a directory that
            // was deleted or renamed away only gets
            // MTP_RESPONSE_INVALID_OBJECT_HANDLE for it.
            std::vector<MtpObjectHandle> removed;
            CollectSubtreeHandles(entry, removed);
            const MtpObjectHandle handle = entry->handle;
            // DeleteFile also drops the watches and the index entries of the
            // directory itself and of everything below it.
            DeleteFile(handle);
            if (send_events_) {
                for (const MtpObjectHandle gone : removed) {
                    events.push_back({MTP_EVENT_OBJECT_REMOVED, gone});
                }
            }
        } else {
            MTPD("inotify already removed.\n");
        }
    } else if (mask & IN_MODIFY) {
        MTPD("inotify item %s modified.\n", event->name);
        if (entry != nullptr) {
            const MtpPropertyEntry* size_prop = FindProperty(entry, MTP_PROPERTY_OBJECT_SIZE);
            const uint64_t original_size = size_prop != nullptr ? size_prop->value_int : 0;
            std::error_code ec;
            uint64_t new_size = 0;
            if (const auto sz = fs::file_size(GetNodePath(entry), ec); !ec) {
                new_size = static_cast<uint64_t>(sz);
            }
            if (original_size != new_size) {
                MTPD("size changed from %llu to %llu on mtp_id: %u\n", original_size, new_size,
                     entry->handle);
                UpdateProperty(entry, MTP_PROPERTY_OBJECT_SIZE, new_size, "", MTP_TYPE_UINT64);
                if (send_events_) events.push_back({MTP_EVENT_OBJECT_PROP_CHANGED, entry->handle});
            }
        } else {
            MTPE("inotify modified item not found\n");
        }
    } else if (mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
        // TODO: is this always already handled by IN_DELETE for the parent dir?
        // Yes, for every entry but the storage root: neither of these two carries
        // a name, so InotifyLoop never forwards them here, and a directory that
        // goes away is always reported by its parent's IN_DELETE / IN_MOVED_FROM.
        // That is the same set of events MtpStorageManager watches: its observer
        // mask has no DELETE_SELF and no MOVE_SELF either.
    }
}

int TwrpMtpStorage::GetObjectPropertyValue(const MtpObjectHandle handle,
                                           const MtpObjectProperty property,
                                           PropEntry& prop_entry) {
    const std::lock_guard guard(tree_mutex_);
    const MtpEntry* entry = FindNode(handle);
    if (!entry) {
        return -1;
    }
    const MtpPropertyEntry* prop = FindProperty(entry, property);
    if (!prop) {
        MTPD("GetObjectPropertyValue: unknown property %x for handle %u\n", property, handle);
        return -1;
    }
    prop_entry.data_type = prop->data_type;
    prop_entry.int_value = prop->value_int;
    prop_entry.str_value = prop->value_str;
    prop_entry.handle = handle;
    prop_entry.property = property;
    return 0;
}

void TwrpMtpStorage::EndSendObject(const char* path, const MtpObjectHandle handle,
                                   [[maybe_unused]] MtpObjectFormat format,
                                   const bool succeeded) {
    std::vector<PendingEvent> events;
    {
        const std::lock_guard guard(tree_mutex_);
        // The guard is always released at the end of a transfer, even when the
        // node is gone by now: a guard that outlives its transfer would keep
        // swallowing every later inotify event for that node.
        if (handle_currently_sending_ == handle) handle_currently_sending_ = 0;

        if (MtpEntry* entry = FindNode(handle)) {
            if (succeeded) {
                AddProperties(entry, path, getStorageID());
                if (send_events_) events.push_back({MTP_EVENT_OBJECT_ADDED, entry->handle});
            } else if (std::error_code ec; !entry->is_dir && !fs::exists(GetNodePath(entry), ec)) {
                // The failed upload's file is gone (MtpServer unlinked it), so
                // the node goes away with it: keeping it would leave a handle
                // behind that shadows the next push of the same name.  A
                // directory, or a file that is still there, keeps its node.
                MTPD("EndSendObject: dropping failed transfer of handle %u\n", handle);
                DeleteFile(handle);
            }
        }
    }
    SendEvents(server_, events);
}

int TwrpMtpStorage::GetObjectPropertyList(const MtpObjectHandle handle, const uint32_t format,
                                          const uint32_t property, const int group_code,
                                          const int depth, std::vector<PropEntry>& results) {
    MTPD("TwrpMtpStorage::GetObjectPropertyList handle: %u, format: %x, property: %x, depth: %d\n",
         handle, format, property, depth);
    // There is no group support here and no group check either: whether a group
    // code is an error depends on the property the request asked for, which is
    // the whole request's business, so TwrpMtpDatabase decides it - exactly where
    // MtpDatabase.getObjectPropertyList() decides it.

    const std::lock_guard guard(tree_mutex_);

    if (handle == 0xffffffff) {
        // TODO: all object on all storages
        // This storage answers for its own tree; TwrpMtpDatabase collects the
        // other storages into the same list and writes the total count.
        QueryChildProperties(results, &root_, property, group_code, true);
    } else if (handle == 0) {
        QueryChildProperties(results, &root_, property, group_code, false);
    } else {
        // depth 1 asks for the children of the object, depth 0 for the object.
        const MtpEntry* entry = depth == 1 ? FindTree(handle) : FindNode(handle);
        if (!entry) return -1;
        if (depth == 1) {
            QueryChildProperties(results, entry, property, group_code, false);
        } else {
            QueryNodeProperties(results, entry, property, group_code, getStorageID());
        }
    }
    return 0;
}

void TwrpMtpStorage::WritePropertyList(const std::vector<PropEntry>& results,
                                       MtpDataPacket& packet) {
    MTPD("TwrpMtpStorage::WritePropertyList::count: %u\n", results.size());
    packet.putUInt32(results.size());

    for (const auto& [handle, property, data_type, int_value, str_value] : results) {
        MTPD("handle: %u, propertyCode: %x = %s, data_type: %x, value: %llu\n", handle, property,
             MtpDebug::getObjectPropCodeName(property), data_type, int_value);
        packet.putUInt32(handle);
        packet.putUInt16(property);
        packet.putUInt16(data_type);
        switch (data_type) {
            case MTP_TYPE_INT8:
                packet.putInt8(static_cast<int8_t>(int_value));
                break;
            case MTP_TYPE_UINT8:
                packet.putUInt8(int_value);
                break;
            case MTP_TYPE_INT16:
                packet.putInt16(static_cast<int16_t>(int_value));
                break;
            case MTP_TYPE_UINT16:
                packet.putUInt16(int_value);
                break;
            case MTP_TYPE_INT32:
                packet.putInt32(static_cast<int32_t>(int_value));
                break;
            case MTP_TYPE_UINT32:
                packet.putUInt32(int_value);
                break;
            case MTP_TYPE_INT64:
                packet.putInt64(static_cast<int64_t>(int_value));
                break;
            case MTP_TYPE_UINT64:
                packet.putUInt64(int_value);
                break;
            case MTP_TYPE_INT128:
                packet.putInt128(static_cast<int128_t>(int_value));
                break;
            case MTP_TYPE_UINT128:
                packet.putUInt128(int_value);
                break;
            case MTP_TYPE_STR:
                packet.putString(str_value.c_str());
                break;
            default:
                MTPE("bad or unsupported data type: %x in GetObjectPropertyList", data_type);
                break;
        }
    }
}

int TwrpMtpStorage::GetObjectInfo(const MtpObjectHandle handle, MtpObjectInfo& info) {
    MTPD("TwrpMtpStorage::GetObjectInfo, handle: %u\n", handle);
    const std::lock_guard guard(tree_mutex_);
    const MtpEntry* entry = FindNode(handle);
    if (!entry) return -1;

    // lstat() supplies only the size and the modification time; the type comes
    // from the tree, which is also what GetObjectFilePath reports, so the two
    // answers for one object cannot disagree once its file is gone.
    struct stat st{};
    const std::string path = GetNodePath(entry);
    if (lstat(path.c_str(), &st) != 0) {
        MTPD("GetObjectInfo: lstat '%s' failed: %s\n", path.c_str(), strerror(errno));
    }

    info.mStorageID = getStorageID();
    info.mParent = entry->parent;
    info.mFormat = entry->is_dir ? MTP_FORMAT_ASSOCIATION : MTP_FORMAT_UNDEFINED;
    // st stays zeroed when lstat() fails, so a vanished file reports size 0.
    const uint64_t size = st.st_size > 0 ? st.st_size : 0;
    info.mCompressedSize = static_cast<uint32_t>(std::min<uint64_t>(size, 0xFFFFFFFF));
    info.mDateModified = st.st_mtime;
    // MtpObjectInfo owns the name and free()s it; every caller passes a freshly
    // constructed info, so this is the only assignment to it.
    info.mName = strdup(entry->name.c_str());
    if (!info.mName) {
        MTPE("GetObjectInfo: out of memory for the name of handle %u\n", handle);
        return -1;
    }
    MTPD("TwrpMtpStorage::GetObjectInfo found '%s', size: %u, dir: %d\n", info.mName,
         info.mCompressedSize, entry->is_dir);
    return 0;
}

int TwrpMtpStorage::GetObjectFilePath(const MtpObjectHandle handle, MtpStringBuffer& out_file_path,
                                      int64_t& out_file_length, MtpObjectFormat& out_format) {
    MTPD("TwrpMtpStorage::GetObjectFilePath handle: %u\n", handle);
    const std::lock_guard guard(tree_mutex_);
    const MtpEntry* entry = FindNode(handle);
    if (!entry) return -1;

    std::error_code ec;
    const auto size = fs::file_size(GetNodePath(entry), ec);
    out_file_length = ec ? 0 : static_cast<int64_t>(size);
    out_file_path.set(GetNodePath(entry).c_str());
    MTPD("outFilePath: %s\n", static_cast<const char*>(out_file_path));
    out_format = entry->is_dir ? MTP_FORMAT_ASSOCIATION : MTP_FORMAT_UNDEFINED;
    return 0;
}

void TwrpMtpStorage::CollectSubtreeHandles(const MtpEntry* entry,
                                           std::vector<MtpObjectHandle>& handles) {
    const std::lock_guard guard(tree_mutex_);
    handles.push_back(entry->handle);
    for (const auto& child : entry->children | std::views::values) {
        if (child->is_dir) {
            CollectSubtreeHandles(child.get(), handles);
        } else {
            handles.push_back(child->handle);
        }
    }
}

// Drops an entry, and the subtree below it when it is a directory, out of the
// tree.  The files on disk are the caller's business: MtpServer::doDeleteObject
// unlinks them after this returns, and the inotify event that unlink raises then
// finds no node left to report.  Returns 0 when the handle was known, -1 else.
int TwrpMtpStorage::DeleteFile(const MtpObjectHandle handle) {
    MTPD("TwrpMtpStorage::DeleteFile handle: %u\n", handle);
    const std::lock_guard guard(tree_mutex_);
    const MtpEntry* entry = FindNode(handle);
    if (!entry) return -1;

    // Handle 0 is the storage root.  It is not a child of anything, so the
    // erase below would not even reach it, while beginDeleteObject() would take
    // the answer for a go-ahead to delete the storage itself.
    if (handle == 0) {
        MTPE("DeleteFile: refusing to delete the storage root\n");
        return -1;
    }

    const auto parent_it = dir_index_.find(entry->parent);
    if (parent_it == dir_index_.end()) {
        MTPE("parent dir for handle %u not found\n", entry->parent);
        return -1;
    }
    MtpEntry* parent_dir = parent_it->second;

    // The guard must never outlive the node it points at.
    if (handle_currently_sending_ == handle) handle_currently_sending_ = 0;

    // Every handle that goes away with this entry: the entry itself, plus the
    // descendants when it is a directory.  Their inotify watches and their index
    // entries have to go before the unique_ptr cascade below frees the nodes,
    // because anything left behind is a dangling pointer for the next FindNode().
    std::vector<MtpObjectHandle> removed;
    CollectSubtreeHandles(entry, removed);

    for (auto watch_it = inotify_map_.begin(); watch_it != inotify_map_.end();) {
        if (std::ranges::find(removed, watch_it->second) == removed.end()) {
            ++watch_it;
            continue;
        }
        MTPD("inotify removing watch on handle %u\n", watch_it->second);
        inotify_rm_watch(inotify_fd_, watch_it->first);
        watch_it = inotify_map_.erase(watch_it);
    }

    for (const MtpObjectHandle gone : removed) {
        node_index_.erase(gone);
        dir_index_.erase(gone);
    }

    MTPD("deleting handle: %u\n", handle);
    parent_dir->children.erase(handle);  // unique_ptr cascade deletes the subtree
    MTPD("deleted\n");
    return 0;
}

void TwrpMtpStorage::QueryNodeProperties(std::vector<PropEntry>& results, const MtpEntry* entry,
                                         const uint32_t property, [[maybe_unused]] int group_code,
                                         const MtpStorageID storage_id) {
    MTPD("QueryNodeProperties handle %u, path: %s\n", entry->handle, GetNodePath(entry).c_str());
    PropEntry pe;
    pe.handle = entry->handle;
    pe.property = property;

    if (property == 0xffffffff) {
        MTPD("TwrpMtpStorage::QueryNodeProperties for all properties\n");
        for (const auto& prop : entry->properties) {
            pe.property = prop.property;
            pe.data_type = prop.data_type;
            pe.int_value = prop.value_int;
            pe.str_value = prop.value_str;
            results.push_back(pe);
        }
        return;
    }
    if (property == 0) {
        // TODO: use groupCode
    }

    switch (property) {
        case MTP_PROPERTY_STORAGE_ID:
            pe.data_type = MTP_TYPE_UINT32;
            pe.int_value = storage_id;
            break;

        case MTP_PROPERTY_PROTECTION_STATUS:
            pe.data_type = MTP_TYPE_UINT16;
            pe.int_value = 0;
            break;

        case MTP_PROPERTY_OBJECT_SIZE: {
            pe.data_type = MTP_TYPE_UINT64;
            std::error_code ec;
            pe.int_value = 0;
            if (const auto sz = fs::file_size(GetNodePath(entry), ec); !ec) pe.int_value = sz;
            break;
        }

        default: {
            if (const MtpPropertyEntry* prop = FindProperty(entry, property)) {
                pe.data_type = prop->data_type;
                pe.int_value = prop->value_int;
                pe.str_value = prop->value_str;
            } else {
                MTPD("QueryNodeProperties: unknown property %x\n", property);
                return;
            }
        }
    }
    results.push_back(pe);
}

// Adds the entries of every child of `dir` to the result set, and with
// `recursive` also the entries of everything below it.  The directory itself is
// not an object of the answer, which is the set MtpStorageManager.getObjects()
// walks for these requests.  The caller holds the tree lock.
void TwrpMtpStorage::QueryChildProperties(std::vector<PropEntry>& results, const MtpEntry* dir,
                                          const uint32_t property, const int group_code,
                                          const bool recursive) {
    for (const auto& child : dir->children | std::views::values) {
        QueryNodeProperties(results, child.get(), property, group_code, getStorageID());
        if (recursive && child->is_dir) {
            QueryChildProperties(results, child.get(), property, group_code, recursive);
        }
    }
}
}