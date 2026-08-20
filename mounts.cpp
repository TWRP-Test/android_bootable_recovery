/*
 * Copyright (C) 2007 The Android Open Source Project
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

#include "mounts.h"

#include <mntent.h>
#include <stdio.h>
#include <string>
#include <sys/mount.h>
#include <vector>

#include <android-base/logging.h>

struct MountedVolume {
    std::string device;
    std::string mount_point;
    std::string filesystem;
    std::string flags;
};

static std::vector<MountedVolume*> g_mounts_state;

bool scan_mounted_volumes() {
    for (MountedVolume* volume : g_mounts_state) {
        delete volume;
    }
    g_mounts_state.clear();

    FILE* fp = setmntent("/proc/mounts", "re");
    if (fp == nullptr) {
        return false;
    }

    mntent* entry;
    while ((entry = getmntent(fp)) != nullptr) {
        MountedVolume* volume = new MountedVolume;
        volume->device = entry->mnt_fsname;
        volume->mount_point = entry->mnt_dir;
        volume->filesystem = entry->mnt_type;
        volume->flags = entry->mnt_opts;
        g_mounts_state.push_back(volume);
    }
    endmntent(fp);
    return true;
}

MountedVolume* find_mounted_volume_by_mount_point(const char* mount_point) {
    for (MountedVolume* volume : g_mounts_state) {
        if (volume->mount_point == mount_point) {
            return volume;
        }
    }
    return nullptr;
}

int unmount_mounted_volume(MountedVolume* volume) {
    std::string mount_point = volume->mount_point;
    volume->mount_point.clear();
    int result = umount(mount_point.c_str());
    if (result == -1) {
        PLOG(WARNING) << "Failed to umount " << mount_point;
    }
    return result;
}
