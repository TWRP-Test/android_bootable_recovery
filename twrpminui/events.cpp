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
#include <fcntl.h>
#include <dirent.h>
#include <sys/poll.h>
#include <limits.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <stdio.h>
#include <string.h>
#include <fstream>
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

#include "common.h"
#include "twcommon.h"

#include "twrpminui/minui.h"

//#define _EVENT_LOGGING

#define MAX_DEVICES         32

#define VIBRATOR_TIMEOUT_FILE	"/sys/class/timed_output/vibrator/enable"
#define VIBRATOR_TIME_MS    50

#define LEDS_HAPTICS_DURATION_FILE	"/sys/class/leds/vibrator/duration"
#define LEDS_HAPTICS_ACTIVATE_FILE	"/sys/class/leds/vibrator/activate"

#ifndef SYN_REPORT
#define SYN_REPORT          0x00
#endif
#ifndef SYN_CONFIG
#define SYN_CONFIG          0x01
#endif
#ifndef SYN_MT_REPORT
#define SYN_MT_REPORT       0x02
#endif

#define ABS_MT_POSITION     0x2a /* Group a set of X and Y */
#define ABS_MT_AMPLITUDE    0x2b /* Group a set of Z and W */
#define ABS_MT_SLOT         0x2f
#define ABS_MT_TOUCH_MAJOR  0x30
#define ABS_MT_TOUCH_MINOR  0x31
#define ABS_MT_WIDTH_MAJOR  0x32
#define ABS_MT_WIDTH_MINOR  0x33
#define ABS_MT_ORIENTATION  0x34
#define ABS_MT_POSITION_X   0x35
#define ABS_MT_POSITION_Y   0x36
#define ABS_MT_TOOL_TYPE    0x37
#define ABS_MT_BLOB_ID      0x38
#define ABS_MT_TRACKING_ID  0x39
#define ABS_MT_PRESSURE     0x3a
#define ABS_MT_DISTANCE     0x3b

enum {
    DOWN_NOT,
    DOWN_SENT,
    DOWN_RELEASED,
};

struct virtualkey {
    int scancode;
    int centerx, centery;
    int width, height;
};

struct position {
    int x, y;
    int synced;
    struct input_absinfo xi, yi;
};

struct mt_slot {
    int tracking_id;
    int x, y;
    bool x_valid, y_valid;
    bool active;
};

struct mt_a_point {
    int x, y;
    bool x_valid, y_valid;
};

struct ev {
    struct pollfd *fd;

    struct virtualkey *vks;
    int vk_count;

    char deviceName[64];

    int ignored;

    struct position p, mt_p;
    int down;
    int absolute_mouse;

    // Independent Type-B state for consumers which need all contacts. The
    // legacy vk_modify() path continues to use mt_p unchanged.
    bool mt_type_b;
    int mt_current_slot;
    mt_slot mt_slots[TWRP_MAX_TOUCH_POINTS];
    TWRPTouchPoint mt_releases[TWRP_MAX_TOUCH_POINTS];
    int mt_release_count;

    // Type-A has no stable slots, so IDs are assigned by report order.
    mt_a_point mt_a_current;
    mt_a_point mt_a_frame[TWRP_MAX_TOUCH_POINTS];
    TWRPTouchPoint mt_a_previous[TWRP_MAX_TOUCH_POINTS];
    int mt_a_frame_count;
    int mt_a_previous_count;
    bool mt_a_release_pending;
};

static struct pollfd ev_fds[MAX_DEVICES];
static struct ev evs[MAX_DEVICES];
static unsigned ev_count = 0;
static struct timeval lastInputStat;
static time_t lastInputMTime;
static int has_mouse = 0;

static TWRPTouchPoint pending_touch_events[TWRP_MAX_TOUCH_EVENTS];
static int pending_touch_event_count = 0;
static bool pending_touch_frame = false;

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array) ((array[LONG(bit)] >> OFF(bit)) & 1)

static inline int ABS(int x) {
    return x<0?-x:x;
}

int write_to_file(const std::string& fn, const std::string& line) {
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
    static std::shared_ptr<IVibrator> cached_vib;
    if (cached_vib == nullptr)
        cached_vib = IVibrator::fromBinder(ndk::SpAIBinder(AServiceManager_checkService(kVibratorInstance.c_str())));
    std::shared_ptr<IVibrator> vib = cached_vib;
    if (vib != nullptr) {
#ifdef USE_QTI_AIDL_HAPTICS_FIX_OFF
        std::thread([vib, timeout_ms] {
            if (vib->on((uint32_t)timeout_ms, nullptr).isOk()) {
                vib_on_count++;
                usleep(timeout_ms * 1000);
                vib_on_count--;
                if (!vib_on_count) vib->off();
            }
        }).detach();
#else
        /* Look again next press if the HAL went away. */
        if (!vib->on((uint32_t)timeout_ms, nullptr).isOk())
            cached_vib = nullptr;
#endif
    }
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
    } else
        write_to_file(VIBRATOR_TIMEOUT_FILE, tout);
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
    return binder.get() != nullptr;
#elif defined(USE_QTI_HAPTICS)
    return android::hardware::vibrator::V1_2::IVibrator::tryGetService() != nullptr;
#elif defined(USE_SAMSUNG_HAPTICS)
    return access(VIBRATOR_TIMEOUT_FILE, W_OK) == 0;
#else
    return access(LEDS_HAPTICS_ACTIVATE_FILE, W_OK) == 0 ||
           access(VIBRATOR_TIMEOUT_FILE, W_OK) == 0;
#endif
}

/* Returns empty tokens */
static char *vk_strtok_r(char *str, const char *delim, char **save_str)
{
    if(!str)
    {
        if(!*save_str)
            return NULL;

        str = (*save_str) + 1;
    }
    *save_str = strpbrk(str, delim);

    if (*save_str)
        **save_str = '\0';

    return str;
}

static int vk_init(struct ev *e)
{
    char vk_path[PATH_MAX] = "/sys/board_properties/virtualkeys.";
    char vks[2048], *ts = NULL;
    ssize_t len;
    int vk_fd;
    int i;

    e->absolute_mouse = 0;

    e->vk_count = 0;

    len = strlen(vk_path);
    len = ioctl(e->fd->fd, EVIOCGNAME(sizeof(e->deviceName)), e->deviceName);
    if (len <= 0)
    {
        LOGE("Unable to query event object.\n");
        return -1;
    }
#ifdef _EVENT_LOGGING
    printf("Event object: %s\n", e->deviceName);
#endif

    unsigned long capabilities[EV_MAX][NBITS(KEY_MAX)];
    memset(capabilities, 0, sizeof(capabilities));
    ioctl(e->fd->fd, EVIOCGBIT(0, EV_MAX), capabilities[0]);
    if (test_bit(EV_ABS, capabilities[0]) && test_bit(EV_KEY, capabilities[0])) {
        ioctl(e->fd->fd, EVIOCGBIT(EV_ABS, KEY_MAX), capabilities[EV_ABS]);
        ioctl(e->fd->fd, EVIOCGBIT(EV_KEY, KEY_MAX), capabilities[EV_KEY]);
        const bool has_absolute_xy = test_bit(ABS_X, capabilities[EV_ABS]) &&
                                     test_bit(ABS_Y, capabilities[EV_ABS]);
        const bool has_mouse_button = test_bit(BTN_LEFT, capabilities[EV_KEY]);
        const bool has_touch_protocol = test_bit(BTN_TOUCH, capabilities[EV_KEY]) ||
                                        (test_bit(ABS_MT_POSITION_X, capabilities[EV_ABS]) &&
                                         test_bit(ABS_MT_POSITION_Y, capabilities[EV_ABS]));
        e->absolute_mouse = has_absolute_xy && has_mouse_button && !has_touch_protocol;
        e->mt_type_b = test_bit(ABS_MT_SLOT, capabilities[EV_ABS]) &&
                       test_bit(ABS_MT_TRACKING_ID, capabilities[EV_ABS]);
    }

    e->mt_current_slot = 0;
    e->mt_release_count = 0;
    e->mt_a_frame_count = 0;
    e->mt_a_previous_count = 0;
    e->mt_a_release_pending = false;
    for (int slot = 0; slot < TWRP_MAX_TOUCH_POINTS; ++slot) {
        e->mt_slots[slot].tracking_id = -1;
        e->mt_slots[slot].x = 0;
        e->mt_slots[slot].y = 0;
        e->mt_slots[slot].x_valid = false;
        e->mt_slots[slot].y_valid = false;
        e->mt_slots[slot].active = false;
    }

#ifdef WHITELIST_INPUT
    if (strcmp(e->deviceName, EXPAND(WHITELIST_INPUT)) != 0)
    {
        e->ignored = 1;
    }
#else
#ifndef TW_INPUT_BLACKLIST
    // Blacklist these "input" devices, use TW_INPUT_BLACKLIST := "accelerometer\x0atest1\x0atest2" using the \x0a as a separator between input devices
    if (strcmp(e->deviceName, "bma250") == 0 || strcmp(e->deviceName, "bma150") == 0)
    {
        LOGI("Blacklisting input device: %s\n", e->deviceName);
        e->ignored = 1;
    }
#else
    char* bl = strdup(EXPAND(TW_INPUT_BLACKLIST));
    char* blacklist = strtok(bl, "\n");

    while (blacklist != NULL) {
        if (strcmp(e->deviceName, blacklist) == 0) {
            LOGI("Blacklisting input device: %s\n", blacklist);
            e->ignored = 1;
        }
        blacklist = strtok(NULL, "\n");
    }
    free(bl);
#endif
#endif

    strcat(vk_path, e->deviceName);

    // Some devices split the keys from the touchscreen
    e->vk_count = 0;
    vk_fd = open(vk_path, O_RDONLY);
    if (vk_fd >= 0)
    {
        len = read(vk_fd, vks, sizeof(vks)-1);
        close(vk_fd);
        if (len <= 0)
            return -1;

        vks[len] = '\0';

        /* Parse a line like:
            keytype:keycode:centerx:centery:width:height:keytype2:keycode2:centerx2:...
        */
        for (ts = vks, e->vk_count = 1; *ts; ++ts) {
            if (*ts == ':')
                ++e->vk_count;
        }

        if (e->vk_count % 6) {
            LOGI("minui: %s is %d %% 6\n", vk_path, e->vk_count % 6);
        }
        e->vk_count /= 6;
        if (e->vk_count <= 0)
            return -1;

        e->down = DOWN_NOT;
    }

    ioctl(e->fd->fd, EVIOCGABS(ABS_X), &e->p.xi);
    ioctl(e->fd->fd, EVIOCGABS(ABS_Y), &e->p.yi);
    e->p.synced = 0;
#ifdef _EVENT_LOGGING
    printf("EV: ST minX: %d  maxX: %d  minY: %d  maxY: %d\n", e->p.xi.minimum, e->p.xi.maximum, e->p.yi.minimum, e->p.yi.maximum);
#endif

    ioctl(e->fd->fd, EVIOCGABS(ABS_MT_POSITION_X), &e->mt_p.xi);
    ioctl(e->fd->fd, EVIOCGABS(ABS_MT_POSITION_Y), &e->mt_p.yi);
    e->mt_p.synced = 0;
#ifdef _EVENT_LOGGING
    printf("EV: MT minX: %d  maxX: %d  minY: %d  maxY: %d\n", e->mt_p.xi.minimum, e->mt_p.xi.maximum, e->mt_p.yi.minimum, e->mt_p.yi.maximum);
#endif

    e->vks = (virtualkey *)malloc(sizeof(*e->vks) * e->vk_count);

    for (i = 0; i < e->vk_count; ++i) {
        char *token[6];
        int j;

        for (j = 0; j < 6; ++j) {
            token[j] = vk_strtok_r((i||j)?NULL:vks, ":", &ts);
        }

        if (strcmp(token[0], "0x01") != 0) {
            /* Java does string compare, so we do too. */
            LOGI("minui: %s: ignoring unknown virtual key type %s\n", vk_path, token[0]);
            continue;
        }

        e->vks[i].scancode = strtol(token[1], NULL, 0);
        e->vks[i].centerx = strtol(token[2], NULL, 0);
        e->vks[i].centery = strtol(token[3], NULL, 0);
        e->vks[i].width = strtol(token[4], NULL, 0);
        e->vks[i].height = strtol(token[5], NULL, 0);
    }

    return 0;
}

// Check for EV_REL (REL_X and REL_Y) and, because touchscreens can have those too,
// check also for EV_KEY (BTN_LEFT and BTN_RIGHT)
static void check_mouse(int fd, const char* deviceName)
{
	if(has_mouse)
		return;

	unsigned long bit[EV_MAX][NBITS(KEY_MAX)];
	memset(bit, 0, sizeof(bit));
	ioctl(fd, EVIOCGBIT(0, EV_MAX), bit[0]);

	if(!test_bit(EV_REL, bit[0]) || !test_bit(EV_KEY, bit[0]))
		return;

	ioctl(fd, EVIOCGBIT(EV_REL, KEY_MAX), bit[EV_REL]);
	if(!test_bit(REL_X, bit[EV_REL]) || !test_bit(REL_Y, bit[EV_REL]))
		return;

	ioctl(fd, EVIOCGBIT(EV_KEY, KEY_MAX), bit[EV_KEY]);
	if(!test_bit(BTN_LEFT, bit[EV_KEY]) || !test_bit(BTN_RIGHT, bit[EV_KEY]))
		return;

	LOGI("Found mouse '%s'\n", deviceName);
	has_mouse = 1;
}

int ev_has_mouse(void)
{
	return has_mouse;
}

int ev_init(void)
{
    DIR *dir;
    struct dirent *de;
    int fd;

    has_mouse = 0;
    pending_touch_event_count = 0;
    pending_touch_frame = false;

	dir = opendir("/dev/input");
    if(dir != 0) {
        while((de = readdir(dir))) {
#ifdef _EVENT_LOGGING
            fprintf(stderr,"/dev/input/%s\n", de->d_name);
#endif
            if(strncmp(de->d_name,"event",5)) continue;
            fd = openat(dirfd(dir), de->d_name, O_RDONLY);
            if(fd < 0) continue;

			ev_fds[ev_count].fd = fd;
            ev_fds[ev_count].events = POLLIN;
            evs[ev_count].fd = &ev_fds[ev_count];

            /* Load virtualkeys if there are any */
            vk_init(&evs[ev_count]);

            if (!evs[ev_count].ignored) {
                if (evs[ev_count].absolute_mouse) {
                    has_mouse = 1;
                    LOGI("Found absolute mouse '%s'\n", evs[ev_count].deviceName);
                } else {
                    check_mouse(fd, evs[ev_count].deviceName);
                }
            }

            ev_count++;
            if(ev_count == MAX_DEVICES) break;
        }
        closedir(dir);
    }

    struct stat st;
    if(stat("/dev/input", &st) >= 0)
        lastInputMTime = st.st_mtime;
    gettimeofday(&lastInputStat, NULL);

    return 0;
}

void ev_exit(void)
{
	while (ev_count-- > 0) {
		if (evs[ev_count].vk_count) {
			free(evs[ev_count].vks);
			evs[ev_count].vk_count = 0;
		}
		close(ev_fds[ev_count].fd);
	}
	ev_count = 0;
    pending_touch_event_count = 0;
    pending_touch_frame = false;
}

/*static int vk_inside_display(__s32 value, struct input_absinfo *info, int screen_size)
{
    int screen_pos;

    if (info->minimum == info->maximum)
        return 0;

    screen_pos = (value - info->minimum) * (screen_size - 1) / (info->maximum - info->minimum);
    return (screen_pos >= 0 && screen_pos < screen_size);
}*/

static int vk_tp_to_screen(struct position *p, int *x, int *y)
{
    if (p->xi.minimum == p->xi.maximum || p->yi.minimum == p->yi.maximum)
    {
        // In this case, we assume the screen dimensions are the same.
        *x = p->x;
        *y = p->y;
        return 0;
    }

#ifdef _EVENT_LOGGING
    printf("EV: p->x=%d  x-range=%d,%d  fb-width=%d\n", p->x, p->xi.minimum, p->xi.maximum, gr_fb_width());
#endif

#ifndef RECOVERY_TOUCHSCREEN_SWAP_XY
    int fb_width = gr_fb_width();
    int fb_height = gr_fb_height();
#else
    // We need to swap the scaling sizes, too
    int fb_width = gr_fb_height();
    int fb_height = gr_fb_width();
#endif

    *x = (p->x - p->xi.minimum) * (fb_width - 1) / (p->xi.maximum - p->xi.minimum);
    *y = (p->y - p->yi.minimum) * (fb_height - 1) / (p->yi.maximum - p->yi.minimum);

    if (*x >= 0 && *x < fb_width &&
        *y >= 0 && *y < fb_height)
    {
        return 0;
    }

    return 1;
}

static bool mt_point_to_screen(const struct ev *e, int raw_x, int raw_y, int *x, int *y)
{
    struct position point = {};
    point.x = raw_x;
    point.y = raw_y;
    point.xi = e->mt_p.xi;
    point.yi = e->mt_p.yi;

    if (vk_tp_to_screen(&point, x, y) != 0)
        return false;

#ifdef RECOVERY_TOUCHSCREEN_SWAP_XY
    const int old_x = *x;
    *x = *y;
    *y = old_x;
#endif
#ifdef RECOVERY_TOUCHSCREEN_FLIP_X
    *x = gr_fb_width() - *x;
#endif
#ifdef RECOVERY_TOUCHSCREEN_FLIP_Y
    *y = gr_fb_height() - *y;
#endif

    return true;
}

static void mt_publish_frame(const TWRPTouchPoint *points, int count)
{
    if (count < 0)
        count = 0;
    if (count > TWRP_MAX_TOUCH_EVENTS)
        count = TWRP_MAX_TOUCH_EVENTS;

    for (int i = 0; i < count; ++i)
        pending_touch_events[i] = points[i];
    pending_touch_event_count = count;
    pending_touch_frame = true;
}

static void mt_add_release(struct ev *e, int id, int raw_x, int raw_y)
{
    if (e->mt_release_count >= TWRP_MAX_TOUCH_POINTS)
        return;

    TWRPTouchPoint &release = e->mt_releases[e->mt_release_count];
    release.id = id;
    release.pressed = false;
    if (!mt_point_to_screen(e, raw_x, raw_y, &release.x, &release.y)) {
        release.x = 0;
        release.y = 0;
    }
    ++e->mt_release_count;
}

static void mt_publish_type_b(struct ev *e)
{
    TWRPTouchPoint frame[TWRP_MAX_TOUCH_EVENTS];
    int count = 0;

    // Send all active contacts on every report. LVGL's recognizers use these
    // as move events when the slot ID is already known.
    for (int slot = 0; slot < TWRP_MAX_TOUCH_POINTS; ++slot) {
        const mt_slot &contact = e->mt_slots[slot];
        if (!contact.active || !contact.x_valid || !contact.y_valid)
            continue;

        if (count >= TWRP_MAX_TOUCH_EVENTS)
            break;
        TWRPTouchPoint &point = frame[count++];
        point.id = slot;
        point.pressed = true;
        if (!mt_point_to_screen(e, contact.x, contact.y, &point.x, &point.y)) {
            --count;
            continue;
        }
    }

    for (int i = 0; i < e->mt_release_count && count < TWRP_MAX_TOUCH_EVENTS; ++i)
        frame[count++] = e->mt_releases[i];

    e->mt_release_count = 0;
    // Do not emit empty frames for unrelated SYN_REPORTs after all fingers
    // have been lifted. A non-empty release frame was already assembled above.
    if (count > 0)
        mt_publish_frame(frame, count);
}

static void mt_publish_type_a(struct ev *e)
{
    TWRPTouchPoint frame[TWRP_MAX_TOUCH_EVENTS];
    int count = 0;

    // Type-A contacts are converted to screen coordinates and assigned IDs
    // according to their order in the report. This is inherently less stable
    // than Type-B, but preserves multitouch for older input protocols.
    for (int i = 0; i < e->mt_a_frame_count; ++i) {
        const mt_a_point &raw = e->mt_a_frame[i];
        if (!raw.x_valid || !raw.y_valid || count >= TWRP_MAX_TOUCH_EVENTS)
            continue;

        TWRPTouchPoint &point = frame[count++];
        point.id = i;
        point.pressed = true;
        if (!mt_point_to_screen(e, raw.x, raw.y, &point.x, &point.y)) {
            --count;
            continue;
        }
    }

    // If a Type-A contact disappeared, synthesize releases for the trailing
    // IDs. There is no slot/tracking ID to do better matching with.
    for (int i = e->mt_a_frame_count; i < e->mt_a_previous_count && count < TWRP_MAX_TOUCH_EVENTS; ++i) {
        TWRPTouchPoint release = e->mt_a_previous[i];
        release.pressed = false;
        frame[count++] = release;
    }

    for (int i = 0; i < e->mt_a_frame_count && i < TWRP_MAX_TOUCH_POINTS; ++i) {
        if (i < count)
            e->mt_a_previous[i] = frame[i];
    }
    e->mt_a_previous_count = e->mt_a_frame_count;
    e->mt_a_frame_count = 0;
    e->mt_a_current = {};
    e->mt_a_release_pending = false;

    mt_publish_frame(frame, count);
}

static void mt_process_event(struct ev *e, const struct input_event *ev)
{
    if (e->ignored || e->absolute_mouse)
        return;

    if (e->mt_type_b) {
        if (ev->type == EV_ABS) {
            switch (ev->code) {
                case ABS_MT_SLOT:
                    if (ev->value >= 0 && ev->value < TWRP_MAX_TOUCH_POINTS)
                        e->mt_current_slot = ev->value;
                    break;
                case ABS_MT_TRACKING_ID: {
                    if (e->mt_current_slot < 0 || e->mt_current_slot >= TWRP_MAX_TOUCH_POINTS)
                        break;
                    mt_slot &contact = e->mt_slots[e->mt_current_slot];
                    if (ev->value < 0) {
                        if (contact.active)
                            mt_add_release(e, e->mt_current_slot, contact.x, contact.y);
                        contact.active = false;
                        contact.tracking_id = -1;
                        contact.x_valid = false;
                        contact.y_valid = false;
                    }
                    else {
                        contact.tracking_id = ev->value;
                        contact.active = true;
                        contact.x_valid = false;
                        contact.y_valid = false;
                    }
                    break;
                }
                case ABS_MT_POSITION_X:
                    if (e->mt_current_slot >= 0 && e->mt_current_slot < TWRP_MAX_TOUCH_POINTS) {
                        e->mt_slots[e->mt_current_slot].x = ev->value;
                        e->mt_slots[e->mt_current_slot].x_valid = true;
                    }
                    break;
                case ABS_MT_POSITION_Y:
                    if (e->mt_current_slot >= 0 && e->mt_current_slot < TWRP_MAX_TOUCH_POINTS) {
                        e->mt_slots[e->mt_current_slot].y = ev->value;
                        e->mt_slots[e->mt_current_slot].y_valid = true;
                    }
                    break;
                default:
                    break;
            }
        }
        else if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
            mt_publish_type_b(e);
        }
        return;
    }

    // Type-A compatibility path. Most current Android panels use Type-B, but
    // older kernels report contacts separated by SYN_MT_REPORT instead.
    if (ev->type == EV_ABS) {
        switch (ev->code) {
            case ABS_MT_POSITION:
                if (ev->value == (1 << 31)) {
                    e->mt_a_release_pending = true;
                    e->mt_a_current = {};
                }
                else {
                    e->mt_a_current.x = (ev->value & 0x7FFF0000) >> 16;
                    e->mt_a_current.y = ev->value & 0xFFFF;
                    e->mt_a_current.x_valid = true;
                    e->mt_a_current.y_valid = true;
                }
                break;
            case ABS_MT_POSITION_X:
                e->mt_a_current.x = ev->value;
                e->mt_a_current.x_valid = true;
                break;
            case ABS_MT_POSITION_Y:
                e->mt_a_current.y = ev->value;
                e->mt_a_current.y_valid = true;
                break;
            case ABS_MT_TOUCH_MAJOR:
            case ABS_MT_PRESSURE:
                if (ev->value == 0)
                    e->mt_a_release_pending = true;
                break;
            case ABS_MT_TRACKING_ID:
                if (ev->value < 0)
                    e->mt_a_release_pending = true;
                break;
            default:
                break;
        }
    }
    else if (ev->type == EV_SYN && ev->code == SYN_MT_REPORT) {
        if (e->mt_a_current.x_valid && e->mt_a_current.y_valid &&
            e->mt_a_frame_count < TWRP_MAX_TOUCH_POINTS) {
            e->mt_a_frame[e->mt_a_frame_count++] = e->mt_a_current;
        }
        e->mt_a_current = {};
    }
    else if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
        if (e->mt_a_current.x_valid && e->mt_a_current.y_valid &&
            e->mt_a_frame_count < TWRP_MAX_TOUCH_POINTS) {
            e->mt_a_frame[e->mt_a_frame_count++] = e->mt_a_current;
        }
        if (e->mt_a_frame_count > 0 || e->mt_a_release_pending)
            mt_publish_type_a(e);
        else
            e->mt_a_current = {};
    }
}

/* Translate a virtual key in to a real key event, if needed */
/* Returns non-zero when the event should be consumed */
static int vk_modify(struct ev *e, struct input_event *ev)
{
    static int downX = -1, downY = -1;
    static int discard = 0;
    static int last_virt_key = 0;
    static int lastWasSynReport = 0;
    static int touchReleaseOnNextSynReport = 0;
	static int use_tracking_id_negative_as_touch_release = 0; // On some devices, type: 3  code: 39  value: -1, aka EV_ABS ABS_MT_TRACKING_ID -1 indicates a true touch release
    int i;
    int x, y;

    // This is used to ditch useless event handlers, like an accelerometer
    if (e->ignored)     return 1;

    if (ev->type == EV_REL && ev->code == REL_Z)
    {
        // This appears to be an accelerometer or another strange input device. It's not the touchscreen.
#ifdef _EVENT_LOGGING
        printf("EV: Device disabled due to non-touchscreen messages.\n");
#endif
        e->ignored = 1;
        return 1;
    }

#ifdef _EVENT_LOGGING
    printf("EV: %s => type: %x  code: %x  value: %d\n", e->deviceName, ev->type, ev->code, ev->value);
#endif

	// Handle keyboard events, value of 1 indicates key down, 0 indicates key up
	if (ev->type == EV_KEY) {
		return 0;
	}

    if (e->absolute_mouse) {
        if (ev->type == EV_ABS) {
            if (ev->code == ABS_X) {
                e->p.synced |= 0x01;
                e->p.x = ev->value;
            } else if (ev->code == ABS_Y) {
                e->p.synced |= 0x02;
                e->p.y = ev->value;
            }
            return 1;
        }

        if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
            if ((e->p.synced & 0x03) != 0x03)
                return 1;

            if (vk_tp_to_screen(&e->p, &x, &y) != 0) {
                e->p.synced = 0;
                return 1;
            }

#ifdef RECOVERY_TOUCHSCREEN_SWAP_XY
            x ^= y;
            y ^= x;
            x ^= y;
#endif
#ifdef RECOVERY_TOUCHSCREEN_FLIP_X
            x = gr_fb_width() - x;
#endif
#ifdef RECOVERY_TOUCHSCREEN_FLIP_Y
            y = gr_fb_height() - y;
#endif

            e->p.synced = 0;
            ev->type = EV_ABS;
            ev->code = TWRP_ABS_MOUSE_POSITION;
            ev->value = (x << 16) | y;
            return 0;
        }
    }

    if (ev->type == EV_ABS) {
        switch (ev->code) {

        case ABS_X: //00
            e->p.synced |= 0x01;
            e->p.x = ev->value;
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS  ABS_X  %d\n", e->deviceName, ev->value);
#endif
            break;

        case ABS_Y: //01
            e->p.synced |= 0x02;
            e->p.y = ev->value;
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS  ABS_Y  %d\n", e->deviceName, ev->value);
#endif
            break;

        case ABS_MT_POSITION: //2a
            e->mt_p.synced = 0x03;
            if (ev->value == (1 << 31))
            {
#ifndef TW_IGNORE_MT_POSITION_0
                e->mt_p.x = 0;
                e->mt_p.y = 0;
                lastWasSynReport = 1;
#endif
#ifdef _EVENT_LOGGING
#ifndef TW_IGNORE_MT_POSITION_0
                printf("EV: %s => EV_ABS  ABS_MT_POSITION  %d, set x and y to 0 and lastWasSynReport to 1\n", e->deviceName, ev->value);
#else
                printf("Ignoring ABS_MT_POSITION 0\n", e->deviceName, ev->value);
#endif
#endif
            }
            else
            {
                lastWasSynReport = 0;
                e->mt_p.x = (ev->value & 0x7FFF0000) >> 16;
                e->mt_p.y = (ev->value & 0xFFFF);
#ifdef _EVENT_LOGGING
                printf("EV: %s => EV_ABS  ABS_MT_POSITION  %d, set x: %d and y: %d and lastWasSynReport to 0\n", e->deviceName, ev->value, (ev->value & 0x7FFF0000) >> 16, (ev->value & 0xFFFF));
#endif
            }
            break;

        case ABS_MT_TOUCH_MAJOR: //30
            if (ev->value == 0)
            {
#ifndef TW_IGNORE_MAJOR_AXIS_0
                // We're in a touch release, although some devices will still send positions as well
                e->mt_p.x = 0;
                e->mt_p.y = 0;
                touchReleaseOnNextSynReport = 1;
#endif
            }
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS  ABS_MT_TOUCH_MAJOR  %d\n", e->deviceName, ev->value);
#endif
            break;

		case ABS_MT_PRESSURE: //3a
                    if (ev->value == 0)
            {
                // We're in a touch release, although some devices will still send positions as well
                e->mt_p.x = 0;
                e->mt_p.y = 0;
                touchReleaseOnNextSynReport = 1;
            }
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS  ABS_MT_PRESSURE  %d\n", e->deviceName, ev->value);
#endif
            break;

		case ABS_MT_POSITION_X: //35
            e->mt_p.synced |= 0x01;
            e->mt_p.x = ev->value;
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS  ABS_MT_POSITION_X  %d\n", e->deviceName, ev->value);
#endif
            break;

        case ABS_MT_POSITION_Y: //36
            e->mt_p.synced |= 0x02;
            e->mt_p.y = ev->value;
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS  ABS_MT_POSITION_Y  %d\n", e->deviceName, ev->value);
#endif
            break;

        case ABS_MT_TOUCH_MINOR: //31
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS ABS_MT_TOUCH_MINOR %d\n", e->deviceName, ev->value);
#endif
            break;

        case ABS_MT_WIDTH_MAJOR: //32
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS ABS_MT_WIDTH_MAJOR %d\n", e->deviceName, ev->value);
#endif
            break;

        case ABS_MT_WIDTH_MINOR: //33
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS ABS_MT_WIDTH_MINOR %d\n", e->deviceName, ev->value);
#endif
            break;

        case ABS_MT_TRACKING_ID: //39
#ifdef TW_IGNORE_ABS_MT_TRACKING_ID
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS ABS_MT_TRACKING_ID %d ignored\n", e->deviceName, ev->value);
#endif
            return 1;
#endif
            if (ev->value < 0) {
                e->mt_p.x = 0;
                e->mt_p.y = 0;
                touchReleaseOnNextSynReport = 2;
                use_tracking_id_negative_as_touch_release = 1;
#ifdef _EVENT_LOGGING
                if (use_tracking_id_negative_as_touch_release)
                    printf("using ABS_MT_TRACKING_ID value -1 to indicate touch releases\n");
#endif
            }
#ifdef _EVENT_LOGGING
            printf("EV: %s => EV_ABS ABS_MT_TRACKING_ID %d\n", e->deviceName, ev->value);
#endif
            break;

#ifdef _EVENT_LOGGING
        // These are for touch logging purposes only
        case ABS_MT_ORIENTATION: //34
            printf("EV: %s => EV_ABS ABS_MT_ORIENTATION %d\n", e->deviceName, ev->value);
			return 1;
            break;

		case ABS_MT_TOOL_TYPE: //37
            LOGI("EV: %s => EV_ABS ABS_MT_TOOL_TYPE %d\n", e->deviceName, ev->value);
			return 1;
            break;

        case ABS_MT_BLOB_ID: //38
            printf("EV: %s => EV_ABS ABS_MT_BLOB_ID %d\n", e->deviceName, ev->value);
			return 1;
            break;

		case ABS_MT_DISTANCE: //3b
            printf("EV: %s => EV_ABS ABS_MT_DISTANCE %d\n", e->deviceName, ev->value);
			return 1;
            break;
        case ABS_MT_SLOT:
            printf("EV: %s => ABS_MT_SLOT %d\n", e->deviceName, ev->value);
			return 1;
			break;
#endif

        default:
            // This is an unhandled message, just skip it
            return 1;
        }

        if (ev->code != ABS_MT_POSITION)
        {
            lastWasSynReport = 0;
            return 1;
        }
    }

    // Check if we should ignore the message
    if (ev->code != ABS_MT_POSITION && (ev->type != EV_SYN || (ev->code != SYN_REPORT && ev->code != SYN_MT_REPORT)))
    {
        lastWasSynReport = 0;
        return 0;
    }

#ifdef _EVENT_LOGGING
    if (ev->type == EV_SYN && ev->code == SYN_REPORT)
        printf("EV: %s => EV_SYN  SYN_REPORT\n", e->deviceName);
    if (ev->type == EV_SYN && ev->code == SYN_MT_REPORT)
        printf("EV: %s => EV_SYN  SYN_MT_REPORT\n", e->deviceName);
#endif

    // Discard the MT versions
    if (ev->code == SYN_MT_REPORT)      return 0;

    if (((lastWasSynReport == 1 || touchReleaseOnNextSynReport == 1) && !use_tracking_id_negative_as_touch_release) || (use_tracking_id_negative_as_touch_release && touchReleaseOnNextSynReport == 2))
    {
        // Reset the value
        touchReleaseOnNextSynReport = 0;

        // We are a finger-up state
        if (!discard)
        {
            // Report the key up
            ev->type = EV_ABS;
            ev->code = 0;
            ev->value = (downX << 16) | downY;
        }
        downX = -1;
        downY = -1;
        if (discard)
        {
            discard = 0;

            // Send the keyUp event
            ev->type = EV_KEY;
            ev->code = last_virt_key;
            ev->value = 0;
        }
        return 0;
    }
    lastWasSynReport = 1;

    // Retrieve where the x,y position is
    if (e->p.synced & 0x03)
    {
        vk_tp_to_screen(&e->p, &x, &y);
    }
    else if (e->mt_p.synced & 0x03)
    {
        vk_tp_to_screen(&e->mt_p, &x, &y);
    }
    else
    {
        // We don't have useful information to convey
        return 1;
    }

#ifdef RECOVERY_TOUCHSCREEN_SWAP_XY
    x ^= y;
    y ^= x;
    x ^= y;
#endif
#ifdef RECOVERY_TOUCHSCREEN_FLIP_X
    x = gr_fb_width() - x;
#endif
#ifdef RECOVERY_TOUCHSCREEN_FLIP_Y
    y = gr_fb_height() - y;
#endif

#ifdef _EVENT_LOGGING
    printf("EV: x: %d  y: %d\n", x, y);
#endif

    // Clear the current sync states
    e->p.synced = e->mt_p.synced = 0;

    // If we have nothing useful to report, skip it
    if (x == -1 || y == -1)     return 1;

    // Special case, we'll ignore touches on 0,0 because it usually means
    // that we received extra data after our last sync and x and y were
    // reset to 0. We should not be using 0,0 anyway.
    if (x == 0 && y == 0)
        return 1;

    // On first touch, see if we're at a virtual key
    if (downX == -1)
    {
        // Attempt mapping to virtual key
        for (i = 0; i < e->vk_count; ++i)
        {
            int xd = ABS(e->vks[i].centerx - x);
            int yd = ABS(e->vks[i].centery - y);

            if (xd < e->vks[i].width/2 && yd < e->vks[i].height/2)
            {
                ev->type = EV_KEY;
                ev->code = e->vks[i].scancode;
                ev->value = 1;

                last_virt_key = e->vks[i].scancode;

#ifndef TW_NO_HAPTICS
                vibrate(VIBRATOR_TIME_MS);
#endif

                // Mark that all further movement until lift is discard,
                // and make sure we don't come back into this area
                discard = 1;
                downX = 0;
                return 0;
            }
        }
    }

    // If we were originally a button press, discard this event
    if (discard)
    {
        return 1;
    }

    // Record where we started the touch for deciding if this is a key or a scroll
    downX = x;
    downY = y;

    ev->type = EV_ABS;
    ev->code = 1;
    ev->value = (x << 16) | y;
    return 0;
}

int ev_get(struct input_event *ev, int timeout_ms)
{
    int r;
    unsigned n;
    struct timeval curr;

    gettimeofday(&curr, NULL);
    if(curr.tv_sec - lastInputStat.tv_sec >= 2)
    {
        struct stat st;
        stat("/dev/input", &st);
        if (st.st_mtime > lastInputMTime)
        {
            LOGI("Reloading input devices\n");
            ev_exit();
            ev_init();
            lastInputMTime = st.st_mtime;
        }
        lastInputStat = curr;
    }

    r = poll(ev_fds, ev_count, timeout_ms);

    if(r > 0) {
        for(n = 0; n < ev_count; n++) {
            if(ev_fds[n].revents & POLLIN) {
                r = read(ev_fds[n].fd, ev, sizeof(*ev));
                if(r == sizeof(*ev)) {
                    // Assemble the raw multitouch frame before vk_modify()
                    // compresses it into the legacy single-pointer event.
                    mt_process_event(&evs[n], ev);
                    if (!vk_modify(&evs[n], ev))
                        return 0;
                }
            }
        }
        return -1;
    }

    return -2;
}

int twrp_input_take_touch_events(TWRPTouchPoint* points, int max_points)
{
    if (!pending_touch_frame)
        return -1;

    const int count = pending_touch_event_count;
    if (points != nullptr && max_points > 0) {
        const int copy_count = count < max_points ? count : max_points;
        for (int i = 0; i < copy_count; ++i)
            points[i] = pending_touch_events[i];
    }

    pending_touch_event_count = 0;
    pending_touch_frame = false;
    return count;
}

int ev_wait(int timeout __unused)
{
    return -1;
}

void ev_dispatch(void)
{
    return;
}

int ev_get_input(int fd __unused, short revents __unused, struct input_event *ev __unused)
{
    return -1;
}
