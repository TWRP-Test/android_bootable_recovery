#include "force_feedback.h"
#include <algorithm>
#include <mutex>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "common.h"
#include "twcommon.h"
namespace twrpminui {
static std::mutex ff_mutex;
static int ff_haptics_fd = -1;
static int ff_haptics_effect_id = -1;
static int ff_haptics_effect_type = -1;

static void reset_ff() {
    if (ff_haptics_fd >= 0) close(ff_haptics_fd);
    ff_haptics_fd = ff_haptics_effect_id = ff_haptics_effect_type = -1;
}

static bool has_ff_effect(int fd, int effect_type) {
    constexpr size_t bits_per_word = sizeof(unsigned long) * 8;
    unsigned long ff_bits[(FF_CNT + bits_per_word - 1) / bits_per_word] = {};

    if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff_bits)), ff_bits) < 0)
        return false;

    return ff_bits[effect_type / bits_per_word] &
           (1UL << (effect_type % bits_per_word));
}

static int open_ff_haptics() {
    if (ff_haptics_fd >= 0)
        return ff_haptics_fd;

    DIR* dir = opendir("/dev/input");
    if (dir == nullptr)
        return -1;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;

        char name[64] = {};
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (strstr(name, "haptic") == nullptr && strstr(name, "vibrat") == nullptr) {
            close(fd);
            continue;
        }

        if (has_ff_effect(fd, FF_CONSTANT)) {
            ff_haptics_effect_type = FF_CONSTANT;
        } else if (has_ff_effect(fd, FF_RUMBLE)) {
            ff_haptics_effect_type = FF_RUMBLE;
        } else {
            close(fd);
            continue;
        }

        LOGI("Using force-feedback haptics at %s (%s), effect type %d\n",
             path, name, ff_haptics_effect_type);
        ff_haptics_fd = fd;
        break;
    }

    closedir(dir);
    return ff_haptics_fd;
}

int play_force_feedback(int timeout_ms) {
    std::lock_guard<std::mutex> lock(ff_mutex);
    if (timeout_ms <= 0) return -1;
    timeout_ms = std::min(timeout_ms, 10000);
    int fd = open_ff_haptics();
    if (fd < 0)
        return -1;

    if (ff_haptics_effect_id >= 0) {
        if (ioctl(fd, EVIOCRMFF, ff_haptics_effect_id) < 0) {
            LOGE("Unable to remove force-feedback effect %d: %s\n",
                 ff_haptics_effect_id, strerror(errno));
            reset_ff();
            return -1;
        }
        ff_haptics_effect_id = -1;
    }

    struct ff_effect effect = {};
    effect.type = ff_haptics_effect_type;
    effect.id = -1;
    if (effect.type == FF_CONSTANT) {
        effect.u.constant.level = 0x7fff;
    } else {
        effect.u.rumble.strong_magnitude = 0x7fff;
        effect.u.rumble.weak_magnitude = 0x7fff;
    }
    effect.replay.length = timeout_ms;

    if (ioctl(fd, EVIOCSFF, &effect) < 0) {
        LOGE("Unable to upload force-feedback effect type %d: %s\n",
             effect.type, strerror(errno));
        reset_ff();
        return -1;
    }

    ff_haptics_effect_id = effect.id;
    struct input_event play = {};
    play.type = EV_FF;
    play.code = effect.id;
    play.value = 1;
    if (write(fd, &play, sizeof(play)) != static_cast<ssize_t>(sizeof(play))) {
        LOGE("Unable to play force-feedback effect %d: %s\n",
             effect.id, strerror(errno));
        reset_ff();
        return -1;
    }

    return 0;
}


bool force_feedback_available() {
    std::lock_guard<std::mutex> lock(ff_mutex);
    if (ff_haptics_fd >= 0) {
        unsigned long bits = 0;
        if (ioctl(ff_haptics_fd, EVIOCGBIT(0, sizeof(bits)), &bits) < 0) reset_ff();
    }
    return open_ff_haptics() >= 0;
}
}
