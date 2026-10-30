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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <limits.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <mutex>
#include <atomic>
#include "force_feedback.h"
#ifdef USE_QTI_AIDL_HAPTICS_FIX_OFF
#include <thread>
#endif

#ifdef USE_QTI_HAPTICS
#include <android/hardware/vibrator/1.2/IVibrator.h>
#endif

#ifdef USE_QTI_AIDL_HAPTICS
#include <aidl/android/hardware/vibrator/IVibrator.h>
#include <android/binder_manager.h>
using ::aidl::android::hardware::vibrator::IVibrator;
#ifdef USE_QTI_AIDL_HAPTICS_FQNAME
static const std::string kVibratorInstance = std::string("android.hardware.vibrator.") + USE_QTI_AIDL_HAPTICS_FQNAME;
#else
static const std::string kVibratorInstance = std::string(IVibrator::descriptor) + "/default";
#endif
#ifdef USE_QTI_AIDL_HAPTICS_FIX_OFF
static std::atomic_int vib_on_count = 0;
#endif
#endif

#include <cutils/properties.h>

#include "common.h"

#include "twrpminui/minui.h"

// TWRP: HIDL/AIDL, timed-output, LED, TSPDRV and FF haptics.
#define VIBRATOR_TIMEOUT_FILE	"/sys/class/timed_output/vibrator/enable"
#define VIBRATOR_TIME_MS    50

#define LEDS_HAPTICS_DURATION_FILE	"/sys/class/leds/vibrator/duration"
#define LEDS_HAPTICS_ACTIVATE_FILE	"/sys/class/leds/vibrator/activate"

static int write_to_file(const std::string& fn, const std::string& line) {
	FILE *file;
	file = fopen(fn.c_str(), "w");
	if (file != NULL) {
		fwrite(line.c_str(), line.size(), 1, file);
		fclose(file);
		return 0;
	}
	LOGI("Cannot find file %s\n", fn.c_str());
	return -1;
}

#ifndef TW_NO_HAPTICS
#ifndef TW_HAPTICS_TSPDRV
int vibrate(int timeout_ms)
{
    if (timeout_ms <= 0) return -1;
    if (timeout_ms > 10000) timeout_ms = 1000;
    char tout[6];
    sprintf(tout, "%i", timeout_ms);

#ifdef USE_QTI_HAPTICS
    android::sp<android::hardware::vibrator::V1_2::IVibrator> vib = android::hardware::vibrator::V1_2::IVibrator::getService();
    if (vib != nullptr) {
        vib->on((uint32_t)timeout_ms);
    }
#elif defined(USE_QTI_AIDL_HAPTICS)
    /* getService() blocks for seconds while the HAL is not up yet, and this
     * runs on the thread drawing the UI. */
    static std::mutex vibrator_mutex;
    std::lock_guard<std::mutex> guard(vibrator_mutex);
    static std::shared_ptr<IVibrator> cached_vib;
    if (cached_vib == nullptr)
        cached_vib = IVibrator::fromBinder(ndk::SpAIBinder(AServiceManager_checkService(kVibratorInstance.c_str())));
    std::shared_ptr<IVibrator> vib = cached_vib;
    if (vib != nullptr) {
#ifdef USE_QTI_AIDL_HAPTICS_FIX_OFF
        std::thread([vib, timeout_ms] {
            const auto status = vib->on((uint32_t)timeout_ms, nullptr);
            if (status.isOk()) {
                vib_on_count++;
                usleep(timeout_ms * 1000);
                vib_on_count--;
                if (!vib_on_count) vib->off();
            } else if (status.getStatus() == STATUS_OK) {
                // Only an explicit HAL rejection is safe to retry through FF.
                twrpminui::play_force_feedback(timeout_ms);
            }
        }).detach();
        return 0;
#else
        /* Look again next press if the HAL went away. */
        const auto status = vib->on((uint32_t)timeout_ms, nullptr);
        if (status.isOk()) return 0;
        cached_vib = nullptr;
        // A transport failure can follow a delivered request: avoid playing twice.
        if (status.getStatus() != STATUS_OK) return -1;
#endif
    }
    return twrpminui::play_force_feedback(timeout_ms);
#elif defined(USE_SAMSUNG_HAPTICS)
    /* Newer Samsung devices have duration file only
     0 in VIBRATOR_TIMEOUT_FILE means no vibration
     Anything else is the vibration running for X milliseconds */
    if (std::ifstream(VIBRATOR_TIMEOUT_FILE).good()) {
        write_to_file(VIBRATOR_TIMEOUT_FILE, tout);
    }
#else
    if (std::ifstream(LEDS_HAPTICS_ACTIVATE_FILE).good()) {
        write_to_file(LEDS_HAPTICS_DURATION_FILE, tout);
        write_to_file(LEDS_HAPTICS_ACTIVATE_FILE, "1");
    } else if (access(VIBRATOR_TIMEOUT_FILE, W_OK) == 0) {
        write_to_file(VIBRATOR_TIMEOUT_FILE, tout);
    } else {
        return twrpminui::play_force_feedback(timeout_ms);
    }
#endif
    return 0;
}
#endif
#endif

int haptics_available()
{
#ifdef TW_NO_HAPTICS
    return 0;
#elif defined(TW_HAPTICS_TSPDRV)
    return access("/dev/tspdrv", W_OK) == 0;
#elif defined(USE_QTI_AIDL_HAPTICS)
    ndk::SpAIBinder binder(AServiceManager_checkService(kVibratorInstance.c_str()));
    return binder.get() != nullptr || twrpminui::force_feedback_available();
#elif defined(USE_QTI_HAPTICS)
    return android::hardware::vibrator::V1_2::IVibrator::tryGetService() != nullptr;
#elif defined(USE_SAMSUNG_HAPTICS)
    return access(VIBRATOR_TIMEOUT_FILE, W_OK) == 0;
#else
    return twrpminui::force_feedback_available() ||
           access(LEDS_HAPTICS_ACTIVATE_FILE, W_OK) == 0 ||
           access(VIBRATOR_TIMEOUT_FILE, W_OK) == 0;
#endif
}
