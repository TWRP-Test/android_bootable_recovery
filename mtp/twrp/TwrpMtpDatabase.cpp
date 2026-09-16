/*
 * Copyright (C) 2010 The Android Open Source Project
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
 *
 * Copyright (C) 2014 TeamWin - bigbiff and Dees_Troy mtp database conversion to C++
 */

#include "TwrpMtpDatabase.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <ranges>
#include <span>
#include <string>
#include <vector>

#include <android-base/properties.h>

#include "../MtpDataPacket.h"
#include "../MtpDebug.h"
#include "../MtpProperty.h"
#include "../MtpStringBuffer.h"
#include "../MtpUtils.h"
#include "../mtp.h"

namespace android {

namespace {

// MTP spells "everything below this point" as 0xFFFFFFFF.  The depth parameter
// is read out of the request as a signed int, so the value arrives as -1.
constexpr int kMtpDepthAll = -1;

constexpr std::array kDeviceProperties = {
    MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER,
    MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME,
    MTP_DEVICE_PROPERTY_IMAGE_SIZE
  };

constexpr std::array kFileProperties = {
    // NOTE must match beginning of kAudioProperties, kVideoProperties
    // and kImageProperties below
    MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
    MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_DATE_MODIFIED,
    MTP_PROPERTY_PARENT_OBJECT, MTP_PROPERTY_PERSISTENT_UID, MTP_PROPERTY_NAME,
    MTP_PROPERTY_DISPLAY_NAME, MTP_PROPERTY_DATE_ADDED
  };

constexpr std::array kAudioProperties = {
    // NOTE must match kFileProperties above
    MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
    MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_DATE_MODIFIED,
    MTP_PROPERTY_PARENT_OBJECT, MTP_PROPERTY_PERSISTENT_UID, MTP_PROPERTY_NAME,
    MTP_PROPERTY_DISPLAY_NAME, MTP_PROPERTY_DATE_ADDED,

    // audio specific properties
    MTP_PROPERTY_ARTIST, MTP_PROPERTY_ALBUM_NAME, MTP_PROPERTY_ALBUM_ARTIST, MTP_PROPERTY_TRACK,
    MTP_PROPERTY_ORIGINAL_RELEASE_DATE, MTP_PROPERTY_DURATION, MTP_PROPERTY_GENRE,
    MTP_PROPERTY_COMPOSER
  };

constexpr std::array kVideoProperties = {
    // NOTE must match kFileProperties above
    MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
    MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_DATE_MODIFIED,
    MTP_PROPERTY_PARENT_OBJECT, MTP_PROPERTY_PERSISTENT_UID, MTP_PROPERTY_NAME,
    MTP_PROPERTY_DISPLAY_NAME, MTP_PROPERTY_DATE_ADDED,

    // video specific properties
    MTP_PROPERTY_ARTIST, MTP_PROPERTY_ALBUM_NAME, MTP_PROPERTY_DURATION, MTP_PROPERTY_DESCRIPTION
  };

constexpr std::array kImageProperties = {
    // NOTE must match kFileProperties above
    MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
    MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_DATE_MODIFIED,
    MTP_PROPERTY_PARENT_OBJECT, MTP_PROPERTY_PERSISTENT_UID, MTP_PROPERTY_NAME,
    MTP_PROPERTY_DISPLAY_NAME, MTP_PROPERTY_DATE_ADDED,

    // image specific properties
    MTP_PROPERTY_DESCRIPTION
  };

constexpr std::array kAllProperties = {
    // NOTE must match kFileProperties above
    MTP_PROPERTY_STORAGE_ID, MTP_PROPERTY_OBJECT_FORMAT, MTP_PROPERTY_PROTECTION_STATUS,
    MTP_PROPERTY_OBJECT_SIZE, MTP_PROPERTY_OBJECT_FILE_NAME, MTP_PROPERTY_DATE_MODIFIED,
    MTP_PROPERTY_PARENT_OBJECT, MTP_PROPERTY_PERSISTENT_UID, MTP_PROPERTY_NAME,
    MTP_PROPERTY_DISPLAY_NAME, MTP_PROPERTY_DATE_ADDED,

    // image specific properties
    MTP_PROPERTY_DESCRIPTION,

    // audio specific properties
    MTP_PROPERTY_ARTIST, MTP_PROPERTY_ALBUM_NAME, MTP_PROPERTY_ALBUM_ARTIST, MTP_PROPERTY_TRACK,
    MTP_PROPERTY_ORIGINAL_RELEASE_DATE, MTP_PROPERTY_DURATION, MTP_PROPERTY_GENRE,
    MTP_PROPERTY_COMPOSER,

    // video specific properties
    MTP_PROPERTY_ARTIST, MTP_PROPERTY_ALBUM_NAME, MTP_PROPERTY_DURATION, MTP_PROPERTY_DESCRIPTION,

    // image specific properties
    MTP_PROPERTY_DESCRIPTION
  };

constexpr std::array kSupportedPlaybackFormats = {
    SUPPORTED_PLAYBACK_FORMAT_UNDEFINED,
    SUPPORTED_PLAYBACK_FORMAT_ASSOCIATION,
    SUPPORTED_PLAYBACK_FORMAT_TEXT,
    SUPPORTED_PLAYBACK_FORMAT_HTML,
    SUPPORTED_PLAYBACK_FORMAT_WAV,
    SUPPORTED_PLAYBACK_FORMAT_MP3,
    SUPPORTED_PLAYBACK_FORMAT_MPEG,
    SUPPORTED_PLAYBACK_FORMAT_EXIF_JPEG,
    SUPPORTED_PLAYBACK_FORMAT_TIFF_EP,
    SUPPORTED_PLAYBACK_FORMAT_BMP,
    SUPPORTED_PLAYBACK_FORMAT_GIF,
    SUPPORTED_PLAYBACK_FORMAT_JFIF,
    SUPPORTED_PLAYBACK_FORMAT_PNG,
    SUPPORTED_PLAYBACK_FORMAT_TIFF,
    SUPPORTED_PLAYBACK_FORMAT_WMA,
    SUPPORTED_PLAYBACK_FORMAT_OGG,
    SUPPORTED_PLAYBACK_FORMAT_AAC,
    SUPPORTED_PLAYBACK_FORMAT_MP4_CONTAINER,
    SUPPORTED_PLAYBACK_FORMAT_MP2,
    SUPPORTED_PLAYBACK_FORMAT_3GP_CONTAINER,
    SUPPORTED_PLAYBACK_FORMAT_ABSTRACT_AV_PLAYLIST,
    SUPPORTED_PLAYBACK_FORMAT_WPL_PLAYLIST,
    SUPPORTED_PLAYBACK_FORMAT_M3U_PLAYLIST,
    SUPPORTED_PLAYBACK_FORMAT_PLS_PLAYLIST,
    SUPPORTED_PLAYBACK_FORMAT_XML_DOCUMENT,
    SUPPORTED_PLAYBACK_FORMAT_FLAC
  };

struct PropertyTableEntry {
    MtpObjectProperty property;
    int type;
};

constexpr std::array kObjectPropertyTable = {
    PropertyTableEntry{ MTP_PROPERTY_STORAGE_ID, MTP_TYPE_UINT32 },
    PropertyTableEntry{ MTP_PROPERTY_OBJECT_FORMAT, MTP_TYPE_UINT16 },
    PropertyTableEntry{ MTP_PROPERTY_PROTECTION_STATUS, MTP_TYPE_UINT16 },
    PropertyTableEntry{ MTP_PROPERTY_OBJECT_SIZE, MTP_TYPE_UINT64 },
    PropertyTableEntry{ MTP_PROPERTY_OBJECT_FILE_NAME, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_DATE_MODIFIED, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_PARENT_OBJECT, MTP_TYPE_UINT32 },
    PropertyTableEntry{ MTP_PROPERTY_PERSISTENT_UID, MTP_TYPE_UINT128 },
    PropertyTableEntry{ MTP_PROPERTY_NAME, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_DISPLAY_NAME, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_DATE_ADDED, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_ARTIST, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_ALBUM_NAME, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_ALBUM_ARTIST, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_TRACK, MTP_TYPE_UINT16 },
    PropertyTableEntry{ MTP_PROPERTY_ORIGINAL_RELEASE_DATE, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_GENRE, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_COMPOSER, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_PROPERTY_DURATION, MTP_TYPE_UINT32 },
    PropertyTableEntry{ MTP_PROPERTY_DESCRIPTION, MTP_TYPE_STR },
  };

constexpr std::array kDevicePropertyTable = {
    PropertyTableEntry{ MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME, MTP_TYPE_STR },
    PropertyTableEntry{ MTP_DEVICE_PROPERTY_IMAGE_SIZE, MTP_TYPE_STR },
  };

}  // namespace

TwrpMtpDatabase::TwrpMtpDatabase() = default;

TwrpMtpDatabase::~TwrpMtpDatabase() {
    for (const auto& storage : storage_map_ | std::views::values) {
        delete storage;
    }
}

MtpObjectHandle TwrpMtpDatabase::beginSendObject(const char* path, const MtpObjectFormat format,
                                                 const MtpObjectHandle parent,
                                                 const MtpStorageID storage_id) {
    if (!storage_map_.contains(storage_id)) return kInvalidObjectHandle;
    return storage_map_[storage_id]->BeginSendObject(path, format, parent, 0, 0);
}

void TwrpMtpDatabase::endSendObject(const MtpObjectHandle handle, const bool succeeded) {
    MtpStringBuffer path_buf;
    MtpObjectFormat format = 0;
    auto path = "";
    if (int64_t file_length = 0; getObjectFilePath(handle, path_buf, file_length, format) ==
                                MTP_RESPONSE_OK)
        path = static_cast<const char*>(path_buf);
    // Unlinking a failed transfer's half-written file is MtpServer's job (it
    // does so in doSendObject).  Doing it here as well would also fire when
    // the transfer never started at all, deleting the untouched original of
    // an abandoned overwrite.
    MTPD("endSendObject() %s\n", path);
    for (const auto& storage : storage_map_ | std::views::values)
        storage->EndSendObject(path, handle, format, succeeded);
}

void TwrpMtpDatabase::createDB(MtpStorage* storage, const MtpStorageID storage_id) {
    auto* twrp_storage = reinterpret_cast<TwrpMtpStorage*>(storage);
    storage_map_[storage_id] = twrp_storage;
    twrp_storage->CreateDb();
}

MtpObjectHandleList* TwrpMtpDatabase::getObjectList(const MtpStorageID storage_id,
                                                    [[maybe_unused]] MtpObjectFormat format,
                                                    const MtpObjectHandle parent) {
    MTPD("TwrpMtpDatabase::getObjectList::storageID: %d\n", storage_id);
    // 0 and 0xFFFFFFFF stand for "any storage" here, exactly as they do in
    // MtpServer::getStorage(); resolving them to the first storage keeps the
    // answer consistent with the path SendObjectInfo would use for that id.
    auto it = storage_map_.find(storage_id);
    if (it == storage_map_.end() && (storage_id == 0 || storage_id == 0xFFFFFFFF)) {
        it = storage_map_.begin();
    }
    // An empty list, never NULL: doGetObjectHandles answers NULL with
    // MTP_RESPONSE_INVALID_OBJECT_HANDLE, and the operator[] lookup this
    // replaces dereferenced a null storage for an id that is not in the map.
    if (it == storage_map_.end()) {
        MTPE("getObjectList: storage_id %d not found\n", storage_id);
        return new MtpObjectHandleList();
    }
    MtpObjectHandleList* list = it->second->GetObjectList(storage_id, parent);
    MTPD("TwrpMtpDatabase::getObjectList::list size: %d\n", list->size());
    return list;
}

int TwrpMtpDatabase::getNumObjects(const MtpStorageID storage_id,
                                   [[maybe_unused]] MtpObjectFormat format,
                                   const MtpObjectHandle parent) {
    const auto it = storage_map_.find(storage_id);
    if (it == storage_map_.end()) {
        MTPE("getNumObjects: storage_id %d not found\n", storage_id);
        return 0;
    }
    return it->second->GetNumObjects(parent);
}

MtpObjectFormatList* TwrpMtpDatabase::getSupportedPlaybackFormats() {
    // This function tells the host PC which file formats the device supports
    auto* list = new MtpObjectFormatList();
    MTPD("TwrpMtpDatabase::getSupportedPlaybackFormats length: %zu\n", kSupportedPlaybackFormats.size());
    for (const int format : kSupportedPlaybackFormats) {
        MTPD("supported playback format: %x\n", format);
        list->push_back(format);
    }
    return list;
}

MtpObjectFormatList* TwrpMtpDatabase::getSupportedCaptureFormats() {
    // Android OS implementation of this function returns NULL
    // so we are not implementing this function either.
    return nullptr;
}

MtpObjectPropertyList* TwrpMtpDatabase::getSupportedObjectProperties(MtpObjectFormat format) {
    auto* list = new MtpObjectPropertyList();
    const std::span<const int> properties = [format]() -> std::span<const int> {
        switch (format) {
            case MTP_FORMAT_MP3:
            case MTP_FORMAT_WAV:
            case MTP_FORMAT_WMA:
            case MTP_FORMAT_OGG:
            case MTP_FORMAT_AAC:
                return kAudioProperties;
            case MTP_FORMAT_MPEG:
            case MTP_FORMAT_3GP_CONTAINER:
            case MTP_FORMAT_WMV:
                return kVideoProperties;
            case MTP_FORMAT_EXIF_JPEG:
            case MTP_FORMAT_GIF:
            case MTP_FORMAT_PNG:
            case MTP_FORMAT_BMP:
                return kImageProperties;
            case 0:
                return kAllProperties;
            default:
                return kFileProperties;
        }
    }();
    MTPD("TwrpMtpDatabase::getSupportedObjectProperties length is: %zu, format: %x", properties.size(),
         format);
    for (const int property : properties) {
        MTPD("supported object property: %x\n", property);
        list->push_back(property);
    }
    return list;
}

MtpDevicePropertyList* TwrpMtpDatabase::getSupportedDeviceProperties() {
    auto* list = new MtpDevicePropertyList();
    MTPD("TwrpMtpDatabase::getSupportedDeviceProperties length was: %zu\n", kDeviceProperties.size());
    for (const int property : kDeviceProperties) list->push_back(property);
    return list;
}

MtpResponseCode TwrpMtpDatabase::getObjectPropertyValue(const MtpObjectHandle handle,
                                                        const MtpObjectProperty property,
                                                        MtpDataPacket& packet) {
    MTPD("TwrpMtpDatabase::getObjectPropertyValue mtpid: %u, property: %x\n", handle, property);
    int type = 0;
    MtpResponseCode result = MTP_RESPONSE_INVALID_OBJECT_HANDLE;
    TwrpMtpStorage::PropEntry prop;
    if (!getObjectPropertyInfo(property, type)) {
        MTPE(
            "TwrpMtpDatabase::getObjectPropertyValue returning MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED\n");
        return MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;
    }
    for (const auto& storage : storage_map_ | std::views::values) {
        if (storage->GetObjectPropertyValue(handle, property, prop) == 0) {
            result = MTP_RESPONSE_OK;
            break;
        }
    }

    if (result != MTP_RESPONSE_OK) {
        MTPE("TwrpMtpDatabase::getObjectPropertyValue unable to locate handle: %u\n", handle);
        return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
    }

    const uint64_t long_value = prop.int_value;
    // special case date properties, which are strings to MTP
    // but stored internally as a uint64
    if (property == MTP_PROPERTY_DATE_MODIFIED || property == MTP_PROPERTY_DATE_ADDED) {
        char date[20];
        formatDateTime(static_cast<time_t>(long_value), date, sizeof(date));
        packet.putString(date);
        return MTP_RESPONSE_OK;
    }

    switch (type) {
        case MTP_TYPE_INT8:
            packet.putInt8(static_cast<int8_t>(long_value));
            break;
        case MTP_TYPE_UINT8:
            packet.putUInt8(long_value);
            break;
        case MTP_TYPE_INT16:
            packet.putInt16(static_cast<int16_t>(long_value));
            break;
        case MTP_TYPE_UINT16:
            packet.putUInt16(long_value);
            break;
        case MTP_TYPE_INT32:
            packet.putInt32(static_cast<int32_t>(long_value));
            break;
        case MTP_TYPE_UINT32:
            packet.putUInt32(long_value);
            break;
        case MTP_TYPE_INT64:
            packet.putInt64(static_cast<int64_t>(long_value));
            break;
        case MTP_TYPE_UINT64:
            packet.putUInt64(long_value);
            break;
        case MTP_TYPE_INT128:
            packet.putInt128(static_cast<int128_t>(long_value));
            break;
        case MTP_TYPE_UINT128:
            packet.putUInt128(long_value);
            break;
        case MTP_TYPE_STR: {
            packet.putString(prop.str_value.c_str());
            MTPD("MTP_TYPE_STR: %x = %s\n", prop.property, prop.str_value.c_str());
            break;
        }
        default:
            MTPE("unsupported type in getObjectPropertyValue\n");
            result = MTP_RESPONSE_INVALID_OBJECT_PROP_FORMAT;
    }
    return result;
}

MtpResponseCode TwrpMtpDatabase::setObjectPropertyValue(MtpObjectHandle handle,
                                                        MtpObjectProperty property,
                                                        MtpDataPacket& packet) {
    int type = 0;
    MTPD("TwrpMtpDatabase::setObjectPropertyValue start\n");
    if (!getObjectPropertyInfo(property, type)) {
        MTPE(
            "TwrpMtpDatabase::setObjectPropertyValue returning MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED\n");
        return MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;
    }
    MTPD("TwrpMtpDatabase::setObjectPropertyValue continuing\n");

    int8_t int8_value = 0;
    uint8_t uint8_value = 0;
    int16_t int16_value = 0;
    uint16_t uint16_value = 0;
    int32_t int32_value = 0;
    uint32_t uint32_value = 0;
    int64_t int64_value = 0;
    uint64_t uint64_value = 0;
    std::string string_value;

    switch (type) {
        case MTP_TYPE_INT8:
            MTPD("int8\n");
            packet.getInt8(int8_value);
            break;
        case MTP_TYPE_UINT8:
            MTPD("uint8\n");
            packet.getUInt8(uint8_value);
            break;
        case MTP_TYPE_INT16:
            MTPD("int16\n");
            packet.getInt16(int16_value);
            break;
        case MTP_TYPE_UINT16:
            MTPD("uint16\n");
            packet.getUInt16(uint16_value);
            break;
        case MTP_TYPE_INT32:
            MTPD("int32\n");
            packet.getInt32(int32_value);
            break;
        case MTP_TYPE_UINT32:
            MTPD("uint32\n");
            packet.getUInt32(uint32_value);
            break;
        case MTP_TYPE_INT64:
            MTPD("int64\n");
            packet.getInt64(int64_value);
            break;
        case MTP_TYPE_UINT64:
            MTPD("uint64\n");
            packet.getUInt64(uint64_value);
            break;
        case MTP_TYPE_STR: {
            MTPD("string\n");
            MtpStringBuffer buffer;
            packet.getString(buffer);
            string_value = buffer;
            break;
        }
        default:
            MTPE(
                "TwrpMtpDatabase::setObjectPropertyValue unsupported type %i in getObjectPropertyValue\n",
                type);
            return MTP_RESPONSE_INVALID_OBJECT_PROP_FORMAT;
    }

    MtpResponseCode result = MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;

    switch (property) {
        case MTP_PROPERTY_OBJECT_FILE_NAME: {
            MTPD("TwrpMtpDatabase::setObjectPropertyValue renaming file, handle: %d, new name: '%s'\n",
                 handle, string_value.c_str());
            for (const auto& storage : storage_map_ | std::views::values) {
                if (storage->RenameObject(handle, string_value) == 0) {
                    MTPD("MTP_RESPONSE_OK\n");
                    result = MTP_RESPONSE_OK;
                    break;
                }
            }
            break;
        }

        default: {
            MTPE("TwrpMtpDatabase::setObjectPropertyValue property %x not supported.\n", property);
            result = MTP_RESPONSE_OBJECT_PROP_NOT_SUPPORTED;
        }
    }
    MTPD("TwrpMtpDatabase::setObjectPropertyValue returning %d\n", result);
    return result;
}

MtpResponseCode TwrpMtpDatabase::getDevicePropertyValue(const MtpDeviceProperty property,
                                                        MtpDataPacket& packet) {
    int type = 0;
    MTPD("property %s\n", MtpDebug::getDevicePropCodeName(property));
    if (!getDevicePropertyInfo(property, type)) {
        MTPE("TwrpMtpDatabase::getDevicePropertyValue MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED\n");
        return MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED;
    }
    MTPD("property %s\n", MtpDebug::getDevicePropCodeName(property));
    MTPD("property %x\n", property);
    MTPD("MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME %x\n", MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME);
    MtpResponseCode result = MTP_RESPONSE_UNDEFINED;
    switch (property) {
        case MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER:
        case MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME:
            result = MTP_RESPONSE_OK;
            break;
        default: {
            MTPE("TwrpMtpDatabase::getDevicePropertyValue property %x not supported\n", property);
            result = MTP_RESPONSE_DEVICE_PROP_NOT_SUPPORTED;
            break;
        }
    }

    if (result != MTP_RESPONSE_OK) {
        MTPD("MTP_RESPONSE_OK NOT OK\n");
        return result;
    }

    switch (type) {
        case MTP_TYPE_INT8: { MTPD("MTP_TYPE_INT8\n"); packet.putInt8(0); break; }
        case MTP_TYPE_UINT8: { MTPD("MTP_TYPE_UINT8\n"); packet.putUInt8(0); break; }
        case MTP_TYPE_INT16: { MTPD("MTP_TYPE_INT16\n"); packet.putInt16(0); break; }
        case MTP_TYPE_UINT16: { MTPD("MTP_TYPE_UINT16\n"); packet.putUInt16(0); break; }
        case MTP_TYPE_INT32: { MTPD("MTP_TYPE_INT32\n"); packet.putInt32(0); break; }
        case MTP_TYPE_UINT32: { MTPD("MTP_TYPE_UINT32\n"); packet.putUInt32(0); break; }
        case MTP_TYPE_INT64: { MTPD("MTP_TYPE_INT64\n"); packet.putInt64(0); break; }
        case MTP_TYPE_UINT64: { MTPD("MTP_TYPE_UINT64\n"); packet.putUInt64(0); break; }
        case MTP_TYPE_INT128: { MTPD("MTP_TYPE_INT128\n"); packet.putInt128(0); break; }
        case MTP_TYPE_UINT128: { MTPD("MTP_TYPE_UINT128\n"); packet.putInt128(0); break; }
        case MTP_TYPE_STR: {
            MTPD("MTP_TYPE_STR\n");
            const std::string prop_value =
                android::base::GetProperty("ro.color597.product_name", "unknown manufacturer");
            packet.putString(prop_value.c_str());
            break;
        }
        default:
            MTPE(
                "TwrpMtpDatabase::getDevicePropertyValue unsupported type %i in getDevicePropertyValue\n",
                type);
            return MTP_RESPONSE_INVALID_DEVICE_PROP_FORMAT;
    }

    return MTP_RESPONSE_OK;
}

MtpResponseCode TwrpMtpDatabase::setDevicePropertyValue([[maybe_unused]] MtpDeviceProperty property,
                                                        [[maybe_unused]] MtpDataPacket& packet) {
    MTPE("TwrpMtpDatabase::setDevicePropertyValue not implemented, returning 0\n");
    return 0;
}

MtpResponseCode TwrpMtpDatabase::resetDeviceProperty([[maybe_unused]] MtpDeviceProperty property) {
    MTPE("TwrpMtpDatabase::resetDeviceProperty not implemented, returning -1\n");
    return -1;
}

MtpResponseCode TwrpMtpDatabase::getObjectPropertyList(const MtpObjectHandle handle, const uint32_t format,
                                                       const uint32_t property, const int group_code, const int depth,
                                                       MtpDataPacket& packet) {
    MTPD("getObjectPropertyList() handle: %u, format: %x, property: %x, group: %d, depth: %d\n",
         handle, format, property, group_code, depth);

    // property 0 asks for the properties of a property group.  Groups are not
    // implemented, so the answer is the two responses MTP has for that, exactly
    // the ones MtpDatabase.getObjectPropertyList() returns.  A group code on a
    // real property query is ignored there as well, so it is not an error here.
    if (property == 0) {
        return group_code == 0 ? MTP_RESPONSE_PARAMETER_NOT_SUPPORTED
                               : MTP_RESPONSE_SPECIFICATION_BY_GROUP_UNSUPPORTED;
    }

    // A depth of 0xFFFFFFFF means "everything from the starting point down".
    // Started at handle 0 or at 0xFFFFFFFF that is every object on every storage,
    // which is the handle 0xFFFFFFFF, depth 0 request below - the same
    // normalization MtpDatabase.getObjectPropertyList() does first.
    MtpObjectHandle selection = handle;
    int selection_depth = depth;
    if (depth == kMtpDepthAll && (handle == 0 || handle == 0xffffffff)) {
        selection = 0xffffffff;
        selection_depth = 0;
    }
    // Only depth 0 (the object itself) and depth 1 (its direct children) are
    // defined; MtpDatabase rejects everything else the same way.
    if (selection_depth != 0 && selection_depth != 1) {
        return MTP_RESPONSE_SPECIFICATION_BY_DEPTH_UNSUPPORTED;
    }

    // handle 0 (the children of every storage root) and 0xFFFFFFFF (every object
    // on every storage) are answered by all storages together, and the count is
    // written once for the whole packet, so the entries are collected from all of
    // them before they are serialized.
    const bool all_storages = selection == 0 || selection == 0xffffffff;
    std::vector<TwrpMtpStorage::PropEntry> results;
    bool found = false;
    for (const auto& storage : storage_map_ | std::views::values) {
        MTPD("TwrpMtpDatabase::getObjectPropertyList calling GetObjectPropertyList\n");
        if (storage->GetObjectPropertyList(selection, format, property, group_code,
                                           selection_depth, results) == 0) {
            found = true;
            // A real handle lives on one storage only; the two "all" forms are
            // what makes the remaining storages contribute to the same answer.
            if (!all_storages) break;
        }
    }
    if (!found) {
        MTPE("TwrpMtpDatabase::getObjectPropertyList MTP_RESPONSE_INVALID_OBJECT_HANDLE %u\n", handle);
        return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
    }
    TwrpMtpStorage::WritePropertyList(results, packet);
    MTPD("MTP_RESPONSE_OK\n");
    return MTP_RESPONSE_OK;
}

MtpResponseCode TwrpMtpDatabase::getObjectInfo(const MtpObjectHandle handle, MtpObjectInfo& info) {
    for (const auto& storage : storage_map_ | std::views::values) {
        if (storage->GetObjectInfo(handle, info) == 0) {
            MTPD("MTP_RESPONSE_OK\n");
            return MTP_RESPONSE_OK;
        }
    }
    MTPE("TwrpMtpDatabase::getObjectInfo MTP_RESPONSE_INVALID_OBJECT_HANDLE %u\n", handle);
    return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

void* TwrpMtpDatabase::getThumbnail([[maybe_unused]] MtpObjectHandle handle,
                                    [[maybe_unused]] size_t& out_thumb_size) {
    MTPE("TwrpMtpDatabase::getThumbnail not implemented, returning 0\n");
    return nullptr;
}

MtpResponseCode TwrpMtpDatabase::getObjectFilePath(const MtpObjectHandle handle,
                                                   MtpStringBuffer& out_file_path,
                                                   int64_t& out_file_length,
                                                   MtpObjectFormat& out_format) {
    for (const auto& storage : storage_map_ | std::views::values) {
        MTPD("TwrpMtpDatabase::getObjectFilePath calling getObjectFilePath\n");
        if (storage->GetObjectFilePath(handle, out_file_path, out_file_length, out_format) == 0) {
            MTPD("MTP_RESPONSE_OK\n");
            return MTP_RESPONSE_OK;
        }
    }
    MTPE("TwrpMtpDatabase::getObjectFilePath MTP_RESPONSE_INVALID_OBJECT_HANDLE %u\n", handle);
    return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

bool TwrpMtpDatabase::getObjectPropertyInfo(MtpObjectProperty property, int& type) {
    const auto entry = std::ranges::find_if(kObjectPropertyTable, [property](const auto& e) {
      return e.property == property;
    });
    if (entry == kObjectPropertyTable.end()) return false;
    type = entry->type;
    return true;
}

bool TwrpMtpDatabase::getDevicePropertyInfo(MtpDeviceProperty property, int& type) {
    const auto entry = std::ranges::find_if(kDevicePropertyTable, [property](const auto& e) {
      return e.property == property;
    });
    if (entry == kDevicePropertyTable.end()) return false;
    type = entry->type;
    MTPD("type: %x\n", type);
    return true;
}

MtpObjectHandleList* TwrpMtpDatabase::getObjectReferences(const MtpObjectHandle handle) {
    // call function and place files with associated handles into int array
    MTPD(
        "TwrpMtpDatabase::getObjectReferences returning null, this seems to be what Android always "
        "does.\n");
    MTPD("handle: %d\n", handle);
    // Windows + Android seems to always return a NULL in this function, c == null path
    // The way that this is handled in Android then is to do this:
    return nullptr;
}

MtpResponseCode TwrpMtpDatabase::setObjectReferences([[maybe_unused]] MtpObjectHandle handle,
                                                     [[maybe_unused]] MtpObjectHandleList* references) {
    MTPE("TwrpMtpDatabase::setObjectReferences not implemented, returning 0\n");
    return 0;
}

MtpProperty* TwrpMtpDatabase::getObjectPropertyDesc(MtpObjectProperty property,
                                                    MtpObjectFormat format) {
    MTPD("TwrpMtpDatabase::getObjectPropertyDesc start\n");
    MtpProperty* result = nullptr;
    switch (property) {
        case MTP_PROPERTY_OBJECT_FORMAT:
            // use format as default value
            result = new MtpProperty(property, MTP_TYPE_UINT16, false, format);
            break;
        case MTP_PROPERTY_PROTECTION_STATUS:
        case MTP_PROPERTY_TRACK:
            result = new MtpProperty(property, MTP_TYPE_UINT16);
            break;
        case MTP_PROPERTY_STORAGE_ID:
        case MTP_PROPERTY_PARENT_OBJECT:
        case MTP_PROPERTY_DURATION:
            result = new MtpProperty(property, MTP_TYPE_UINT32);
            break;
        case MTP_PROPERTY_OBJECT_SIZE:
            result = new MtpProperty(property, MTP_TYPE_UINT64);
            break;
        case MTP_PROPERTY_PERSISTENT_UID:
            result = new MtpProperty(property, MTP_TYPE_UINT128);
            break;
        case MTP_PROPERTY_NAME:
        case MTP_PROPERTY_DISPLAY_NAME:
        case MTP_PROPERTY_ARTIST:
        case MTP_PROPERTY_ALBUM_NAME:
        case MTP_PROPERTY_ALBUM_ARTIST:
        case MTP_PROPERTY_GENRE:
        case MTP_PROPERTY_COMPOSER:
        case MTP_PROPERTY_DESCRIPTION:
            result = new MtpProperty(property, MTP_TYPE_STR);
            break;
        case MTP_PROPERTY_DATE_MODIFIED:
        case MTP_PROPERTY_DATE_ADDED:
        case MTP_PROPERTY_ORIGINAL_RELEASE_DATE:
            result = new MtpProperty(property, MTP_TYPE_STR);
            result->setFormDateTime();
            break;
        case MTP_PROPERTY_OBJECT_FILE_NAME:
            // We allow renaming files and folders
            result = new MtpProperty(property, MTP_TYPE_STR, true);
            break;
    }
    return result;
}

int TwrpMtpDatabase::openFilePath(const char* path, bool transcode) {
    ALOGD("MtpDatabase %s: filePath=%s transcode=%d\n", __func__, path, transcode);
    return open(path, O_RDONLY);
}

MtpProperty* TwrpMtpDatabase::getDevicePropertyDesc(MtpDeviceProperty property) {
    MtpProperty* result = nullptr;
    switch (property) {
        case MTP_DEVICE_PROPERTY_SYNCHRONIZATION_PARTNER:
        case MTP_DEVICE_PROPERTY_DEVICE_FRIENDLY_NAME:
        case MTP_DEVICE_PROPERTY_IMAGE_SIZE:
            result = new MtpProperty(property, MTP_TYPE_STR,
                                     property != MTP_DEVICE_PROPERTY_IMAGE_SIZE);

            // get current value
            // TODO: add actual values
            result->setCurrentValue(static_cast<uint16_t*>(nullptr));
            result->setDefaultValue(nullptr);
            break;
    }

    return result;
}

MtpResponseCode TwrpMtpDatabase::beginDeleteObject(MtpObjectHandle handle) {
    MTPD("IMtoDatabase::beginDeleteObject handle: %u\n", handle);
    for (const auto& storage : storage_map_ | std::views::values) {
        if (storage->DeleteFile(handle) == 0) {
            MTPD("TwrpMtpDatabase::beginDeleteObject::MTP_RESPONSE_OK\n");
            return MTP_RESPONSE_OK;
        }
    }
    return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

void TwrpMtpDatabase::endDeleteObject([[maybe_unused]] MtpObjectHandle handle,
                                      [[maybe_unused]] bool succeeded) {
    MTPD("TwrpMtpDatabase::endDeleteObject not implemented yet\n");
}

void TwrpMtpDatabase::rescanFile([[maybe_unused]] const char* path,
                                 [[maybe_unused]] MtpObjectHandle handle,
                                 [[maybe_unused]] MtpObjectFormat format) {
    MTPD("TwrpMtpDatabase::rescanFile not implemented yet\n");
}

MtpResponseCode TwrpMtpDatabase::beginMoveObject([[maybe_unused]] MtpObjectHandle handle,
                                                 [[maybe_unused]] MtpObjectHandle new_parent,
                                                 [[maybe_unused]] MtpStorageID new_storage) {
    MTPD("TwrpMtpDatabase::beginMoveObject not implemented yet\n");
    return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

void TwrpMtpDatabase::endMoveObject([[maybe_unused]] MtpObjectHandle old_parent,
                                    [[maybe_unused]] MtpObjectHandle new_parent,
                                    [[maybe_unused]] MtpStorageID old_storage,
                                    [[maybe_unused]] MtpStorageID new_storage,
                                    [[maybe_unused]] MtpObjectHandle handle,
                                    [[maybe_unused]] bool succeeded) {
    MTPD("TwrpMtpDatabase::endMoveObject not implemented yet\n");
}

MtpResponseCode TwrpMtpDatabase::beginCopyObject([[maybe_unused]] MtpObjectHandle handle,
                                                 [[maybe_unused]] MtpObjectHandle new_parent,
                                                 [[maybe_unused]] MtpStorageID new_storage) {
    MTPD("TwrpMtpDatabase::beginCopyObject not implemented yet\n");
    return MTP_RESPONSE_INVALID_OBJECT_HANDLE;
}

void TwrpMtpDatabase::endCopyObject([[maybe_unused]] MtpObjectHandle handle,
                                    [[maybe_unused]] bool succeeded) {
    MTPD("TwrpMtpDatabase::endCopyObject not implemented yet\n");
}

}