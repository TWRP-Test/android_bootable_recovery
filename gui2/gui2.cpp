/*
 * Copyright (C) 2026 The Team Win Recovery Project
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

// GUI2: an independent LVGL 9.5.0 UI on top of libtwrpminui.
// Rendering uses LVGL's software renderer and the minui DRM buffers. Input
// events and monotonic time are supplied by libtwrpminui and CLOCK_MONOTONIC.

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <string>

#include <android-base/logging.h>

#include "twrpminui/minui.h"
#include "twrpperf/perf_manager.hpp"

#include "lvgl.h"

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;

// LVGL renders into two partial buffers, each one tenth of the screen.
constexpr int kDrawBufferCount = 2;
constexpr int kDrawBufferFraction = 10;
constexpr int kWheelStepPixels = 128;
constexpr int kWheelFrameMaxPixels = 48;
constexpr int kWheelMinPixels = 8;
constexpr int kMaxPendingWheelPixels = 8192;

// Input state shared between the ev_get() pump and the LVGL read callbacks.
struct InputState {
    std::atomic<int> pointer_x{0};
    std::atomic<int> pointer_y{0};
    std::atomic<bool> pointer_pressed{false};
    std::atomic<bool> quit_requested{false};
};

InputState g_input;
lv_obj_t* g_scroll_target = nullptr;
lv_timer_t* g_wheel_timer = nullptr;
int32_t g_wheel_pending = 0;
int g_fb_width = 0;
int g_fb_height = 0;
int g_lvgl_pixel_bytes = 4;

int64_t MonotonicMs() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

void FlushCb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
    const int w = area->x2 - area->x1 + 1;
    const int h = area->y2 - area->y1 + 1;
    const int stride = w * g_lvgl_pixel_bytes;
    if (gr_upload_pixels(area->x1, area->y1, w, h, stride, px_map,
                         g_lvgl_pixel_bytes) != 0) {
        LOG(ERROR) << "gui2: gr_upload_pixels failed for " << w << "x" << h
                   << " at " << area->x1 << "," << area->y1;
    }
    if (lv_display_flush_is_last(display)) {
        gr_flip_display();
    }
    lv_display_flush_ready(display);
}

void PointerReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    data->point.x = g_input.pointer_x.load();
    data->point.y = g_input.pointer_y.load();
    data->state = g_input.pointer_pressed.load() ? LV_INDEV_STATE_PRESSED
                                                 : LV_INDEV_STATE_RELEASED;
}

lv_obj_t* CreateMouseCursor(lv_obj_t* parent) {
    lv_obj_t* cursor = lv_label_create(parent);
    lv_label_set_text(cursor, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(cursor, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(cursor, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_clear_flag(cursor, LV_OBJ_FLAG_CLICKABLE);
    return cursor;
}

void QuitButtonCb(lv_event_t* event) {
    g_input.quit_requested.store(true);
}

void ProgressTimerCb(lv_timer_t* timer) {
    lv_obj_t* bar = static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));
    int32_t value = lv_bar_get_value(bar);
    value += 3;
    if (value > 100) value = 0;
    lv_bar_set_value(bar, value, LV_ANIM_ON);
}

void ClockTimerCb(lv_timer_t* timer) {
    lv_obj_t* label = static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));
    time_t now = time(nullptr);
    struct tm tm_now {};
    localtime_r(&now, &tm_now);
    lv_label_set_text_fmt(label, "%02d:%02d:%02d", tm_now.tm_hour, tm_now.tm_min,
                          tm_now.tm_sec);
}

void BuildFirstScreen(lv_obj_t* screen) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), LV_PART_MAIN);

    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "TWRP GUI2 (LVGL 9.5.0)");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t* clock = lv_label_create(screen);
    lv_label_set_text(clock, "--:--:--");
    lv_obj_set_style_text_color(clock, lv_color_hex(0x4FC3F7), LV_PART_MAIN);
    lv_obj_set_style_text_font(clock, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(clock, LV_ALIGN_TOP_MID, 0, 60);
    lv_timer_create(ClockTimerCb, 1000, clock);

    lv_obj_t* bar = lv_bar_create(screen);
    lv_obj_set_size(bar, 600, 32);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 130);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 30, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x2A2F36), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
    lv_timer_create(ProgressTimerCb, 100, bar);

    lv_obj_t* quit = lv_button_create(screen);
    lv_obj_set_size(quit, 360, 80);
    lv_obj_align(quit, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_style_bg_color(quit, lv_color_hex(0xC62828), LV_PART_MAIN);
    lv_obj_t* quit_label = lv_label_create(quit);
    lv_label_set_text(quit_label, "Exit to legacy GUI");
    lv_obj_center(quit_label);
    lv_obj_set_style_text_font(quit_label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_add_event_cb(quit, QuitButtonCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* list = lv_obj_create(screen);
    lv_obj_set_size(list, 860, 1200);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 320);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(list, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x1B2026), LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    g_scroll_target = list;

    for (int i = 0; i < 120; ++i) {
        lv_obj_t* item = lv_label_create(list);
        lv_label_set_text_fmt(item, "List item %03d", i);
        lv_obj_set_style_text_color(item, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
        lv_obj_set_style_text_font(item, &lv_font_montserrat_20, LV_PART_MAIN);
    }
}

bool PointerInsideScrollTarget() {
    if (!g_scroll_target)
        return false;

    lv_area_t area{};
    lv_obj_get_coords(g_scroll_target, &area);
    const int x = g_input.pointer_x.load();
    const int y = g_input.pointer_y.load();
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

void CancelWheelScroll() {
    g_wheel_pending = 0;
    if (g_wheel_timer) lv_timer_pause(g_wheel_timer);
}

void WheelScrollTimerCb(lv_timer_t*) {
    if (!PointerInsideScrollTarget() || g_wheel_pending == 0) {
        CancelWheelScroll();
        return;
    }

    int32_t delta = g_wheel_pending / 4;
    if (delta == 0)
        delta = g_wheel_pending > 0 ? kWheelMinPixels : -kWheelMinPixels;
    if (delta > kWheelFrameMaxPixels)
        delta = kWheelFrameMaxPixels;
    if (delta < -kWheelFrameMaxPixels)
        delta = -kWheelFrameMaxPixels;

    const int32_t before = lv_obj_get_scroll_y(g_scroll_target);
    lv_obj_scroll_by_bounded(g_scroll_target, 0, delta, LV_ANIM_OFF);
    const int32_t after = lv_obj_get_scroll_y(g_scroll_target);
    if (before == after) {
        // We reached the list boundary; discard the remaining throw.
        CancelWheelScroll();
        return;
    }
    g_wheel_pending -= delta;
    if (g_wheel_pending == 0 && g_wheel_timer)
        lv_timer_pause(g_wheel_timer);
}

// Translate one raw input event into LVGL input state.
void ProcessEvent(const struct input_event& ev) {
    if (ev.type == EV_REL) {
        if (ev.code == REL_X) {
            int nx = g_input.pointer_x.load() + ev.value * 3;
            if (nx < 0) nx = 0;
            if (nx >= g_fb_width) nx = g_fb_width - 1;
            g_input.pointer_x.store(nx);
            if (!PointerInsideScrollTarget()) CancelWheelScroll();
        } else if (ev.code == REL_Y) {
            int ny = g_input.pointer_y.load() + ev.value * 3;
            if (ny < 0) ny = 0;
            if (ny >= g_fb_height) ny = g_fb_height - 1;
            g_input.pointer_y.store(ny);
            if (!PointerInsideScrollTarget()) CancelWheelScroll();
        } else if (ev.code == REL_WHEEL) {
            if (PointerInsideScrollTarget()) {
                const int64_t requested = static_cast<int64_t>(g_wheel_pending) +
                                          static_cast<int64_t>(ev.value) *
                                                  kWheelStepPixels;
                if (requested > kMaxPendingWheelPixels)
                    g_wheel_pending = kMaxPendingWheelPixels;
                else if (requested < -kMaxPendingWheelPixels)
                    g_wheel_pending = -kMaxPendingWheelPixels;
                else
                    g_wheel_pending = static_cast<int32_t>(requested);
                if (g_wheel_timer) lv_timer_resume(g_wheel_timer);
            } else {
                CancelWheelScroll();
            }
        }
    } else if (ev.type == EV_KEY) {
        if (ev.code == BTN_LEFT) {
            if (ev.value != 0) {
                g_wheel_pending = 0;
                if (g_wheel_timer) lv_timer_pause(g_wheel_timer);
            }
            g_input.pointer_pressed.store(ev.value != 0);
        } else if (ev.code == BTN_RIGHT && ev.value != 0) {
            g_input.quit_requested.store(true);
        } else if (ev.code == BTN_TOUCH) {
            if (ev.value != 0) {
                g_wheel_pending = 0;
                if (g_wheel_timer) lv_timer_pause(g_wheel_timer);
            }
            g_input.pointer_pressed.store(ev.value != 0);
        }
    } else if (ev.type == EV_ABS) {
        if (ev.code == TWRP_ABS_MOUSE_POSITION) {
            const uint32_t packed = static_cast<uint32_t>(ev.value);
            int x = static_cast<int>((packed >> 16) & 0xFFFF);
            int y = static_cast<int>(packed & 0xFFFF);
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= g_fb_width) x = g_fb_width - 1;
            if (y >= g_fb_height) y = g_fb_height - 1;
            g_input.pointer_x.store(x);
            g_input.pointer_y.store(y);
            if (!PointerInsideScrollTarget()) CancelWheelScroll();
        } else if (ev.code == 0 || ev.code == 1) {
            const uint32_t packed = static_cast<uint32_t>(ev.value);
            int x = static_cast<int>((packed >> 16) & 0xFFFF);
            int y = static_cast<int>(packed & 0xFFFF);
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= g_fb_width) x = g_fb_width - 1;
            if (y >= g_fb_height) y = g_fb_height - 1;
            g_input.pointer_x.store(x);
            g_input.pointer_y.store(y);
            if (ev.code == 1) g_input.pointer_pressed.store(true);
        } else if (ev.code == ABS_X) {
            g_input.pointer_x.store(ev.value);
        } else if (ev.code == ABS_Y) {
            g_input.pointer_y.store(ev.value);
        } else if (ev.code == ABS_MT_POSITION_X) {
            g_input.pointer_x.store(ev.value);
        } else if (ev.code == ABS_MT_POSITION_Y) {
            g_input.pointer_y.store(ev.value);
        }
    }
}

// Drain pending input events and translate them into LVGL input state.
// The first call may block for timeout_ms; remaining events are drained
// without blocking. Returns true when at least one event was consumed.
bool PumpInput(int timeout_ms) {
    struct input_event ev {};
    const int ret = ev_get(&ev, timeout_ms);
    if (ret != 0) {
        if (ret == -2)
            LOG(ERROR) << "gui2: ev_get error";
        return false;
    }
    ProcessEvent(ev);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);

    LOG(INFO) << "gui2: starting independent LVGL GUI";

    if (gr_init_display() != 0) {
        LOG(ERROR) << "gui2: gr_init failed";
        return kExitFailure;
    }
    if (ev_init() != 0) {
        LOG(ERROR) << "gui2: ev_init failed";
        gr_exit();
        return kExitFailure;
    }

    int fb_width = gr_fb_width();
    int fb_height = gr_fb_height();
    g_fb_width = fb_width;
    g_fb_height = fb_height;
    LOG(INFO) << "gui2: framebuffer " << fb_width << "x" << fb_height;
    LOG(INFO) << "gui2: target pixel format " << gr_get_pixel_format();
    if (fb_width <= 0 || fb_height <= 0) {
        LOG(ERROR) << "gui2: invalid framebuffer size";
        ev_exit();
        gr_exit();
        return kExitFailure;
    }

    lv_init();

    lv_display_t* display = lv_display_create(fb_width, fb_height);
    if (!display) {
        LOG(ERROR) << "gui2: lv_display_create failed";
        ev_exit();
        gr_exit();
        return kExitFailure;
    }
    const int pixel_format = gr_get_pixel_format();
    const lv_color_format_t lv_format = pixel_format == GR_PIXEL_FORMAT_RGB565
            ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_XRGB8888;
    g_lvgl_pixel_bytes = pixel_format == GR_PIXEL_FORMAT_RGB565 ? 2 : 4;
    lv_display_set_color_format(display, lv_format);

    const uint32_t draw_buffer_size =
        static_cast<uint32_t>(fb_width) * fb_height / kDrawBufferFraction *
        g_lvgl_pixel_bytes;
    uint8_t* draw_buffers[kDrawBufferCount] = {
        static_cast<uint8_t*>(malloc(draw_buffer_size)),
        static_cast<uint8_t*>(malloc(draw_buffer_size)),
    };
    if (!draw_buffers[0] || !draw_buffers[1]) {
        LOG(ERROR) << "gui2: failed to allocate draw buffers";
        free(draw_buffers[0]);
        free(draw_buffers[1]);
        ev_exit();
        gr_exit();
        return kExitFailure;
    }
    lv_display_set_buffers(display, draw_buffers[0], draw_buffers[1],
                           draw_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, FlushCb);

    lv_indev_t* pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, PointerReadCb);

    lv_obj_t* screen = lv_obj_create(nullptr);
    lv_scr_load(screen);
    BuildFirstScreen(screen);
    if (ev_has_mouse()) {
        lv_indev_set_cursor(pointer, CreateMouseCursor(screen));
    }
    g_wheel_timer = lv_timer_create(WheelScrollTimerCb, 16, nullptr);
    lv_timer_pause(g_wheel_timer);

    auto& perf = twrp::TwrpPerfManager::Get();
    perf.Initialize();

    int64_t last_tick_ms = MonotonicMs();
    int input_timeout_ms = 0;
    int idle_frames = 0;

    while (!g_input.quit_requested.load()) {
        perf.Update();
        input_timeout_ms = perf.ClampTimeoutMs(input_timeout_ms);

        const int64_t now_ms = MonotonicMs();
        const int64_t elapsed_ms = now_ms - last_tick_ms;
        if (elapsed_ms > 0) {
            lv_tick_inc(static_cast<uint32_t>(elapsed_ms));
            last_tick_ms = now_ms;
        }

        const bool had_input = PumpInput(input_timeout_ms);
        if (had_input) perf.NotifyInteraction();

        const uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms > 0) perf.NotifyFrameActivity();

        if (sleep_ms > 0) {
            idle_frames = 0;
            input_timeout_ms = 0;
        } else {
            ++idle_frames;
            input_timeout_ms = idle_frames > 15 ? 1000 : 0;
        }

    }

    LOG(INFO) << "gui2: exiting, handing back to legacy GUI";
    perf.Release();
    lv_deinit();
    ev_exit();
    gr_exit();
    return kExitSuccess;
}
