// TWRP: feed real Type-A/Type-B parser logic without evdev hardware.
#include <cassert>
#include "../twrp_input.cpp"

int gr_fb_width() { return 100; }
int gr_fb_height() { return 200; }
GRRotation gr_get_rotation() { return GRRotation::NONE; }
int property_get(const char*, char* value, const char* fallback) {
  strcpy(value, fallback);
  return strlen(value);
}
int vibrate(int) { return 0; }
int ev_init(ev_callback, bool) { return 0; }
void ev_exit() {}
std::deque<input_event> raw_events;
int ev_get_input(int, uint32_t, input_event* event) {
  if (raw_events.empty()) return -1;
  *event = raw_events.front();
  raw_events.pop_front();
  return 0;
}
int ev_wait(int) { return -1; }
void ev_dispatch() {}

namespace {
void emit(ev* device, uint16_t type, uint16_t code, int value) {
  input_event event{};
  event.type = type; event.code = code; event.value = value;
  mt_process_event(device, &event);
}
ev device(bool type_b) {
  ev result{};
  result.mt_type_b = type_b;
  result.mt_p.xi.minimum = result.mt_p.yi.minimum = 0;
  result.mt_p.xi.maximum = 99;
  result.mt_p.yi.maximum = 199;
  return result;
}
}
int main() {
  TWRPTouchPoint points[TWRP_MAX_TOUCH_EVENTS];
  auto type_b = device(true);
  emit(&type_b, EV_ABS, ABS_MT_SLOT, 0);
  emit(&type_b, EV_ABS, ABS_MT_TRACKING_ID, 7);
  emit(&type_b, EV_ABS, ABS_MT_POSITION_X, 10);
  emit(&type_b, EV_ABS, ABS_MT_POSITION_Y, 20);
  emit(&type_b, EV_SYN, SYN_REPORT, 0);
  emit(&type_b, EV_ABS, ABS_MT_TRACKING_ID, -1);
  emit(&type_b, EV_SYN, SYN_REPORT, 0);
  // A tap's press/release must both survive within a single UI iteration.
  assert(twrp_input_take_touch_events(points, TWRP_MAX_TOUCH_EVENTS) == 1);
  assert(points[0].pressed && points[0].x == 10 && points[0].y == 20);
  assert(twrp_input_take_touch_events(points, TWRP_MAX_TOUCH_EVENTS) == 1);
  assert(!points[0].pressed);
  assert(twrp_input_take_touch_events(points, TWRP_MAX_TOUCH_EVENTS) == -1);
  for (int slot = 0; slot < 2; ++slot) {
    emit(&type_b, EV_ABS, ABS_MT_SLOT, slot);
    emit(&type_b, EV_ABS, ABS_MT_TRACKING_ID, slot + 30);
    emit(&type_b, EV_ABS, ABS_MT_POSITION_X, 30 + slot);
    emit(&type_b, EV_ABS, ABS_MT_POSITION_Y, 40 + slot);
  }
  emit(&type_b, EV_SYN, SYN_REPORT, 0);
  assert(twrp_input_take_touch_events(points, TWRP_MAX_TOUCH_EVENTS) == 2);
  assert(points[0].pressed && points[1].pressed && points[0].id != points[1].id);
  auto type_a = device(false);
  emit(&type_a, EV_ABS, ABS_MT_POSITION_X, 50);
  emit(&type_a, EV_ABS, ABS_MT_POSITION_Y, 60);
  emit(&type_a, EV_SYN, SYN_MT_REPORT, 0);
  emit(&type_a, EV_SYN, SYN_REPORT, 0);
  assert(twrp_input_take_touch_events(points, TWRP_MAX_TOUCH_EVENTS) == 1);
  assert(points[0].pressed && points[0].x == 50 && points[0].y == 60);
  // GPIO key SYN_REPORT must not become a fake touch-up or input activity.
  ev_count = 1;
  evs[0] = {};
  evs[0].fd = 17;
  for (int value : {1, 0}) {
    input_event key{};
    key.type = EV_KEY; key.code = KEY_POWER; key.value = value;
    raw_events.push_back(key);
    input_event sync{};
    sync.type = EV_SYN; sync.code = SYN_REPORT;
    raw_events.push_back(sync);
  }
  while (!raw_events.empty()) input_callback(17, EPOLLIN);
  assert(translated_events.size() == 2);
  assert(translated_events[0].event.type == EV_KEY && translated_events[0].event.value == 1);
  assert(translated_events[1].event.type == EV_KEY && translated_events[1].event.value == 0);
  assert(pending_touch_queued == 0);
  twrp_input_exit();
  puts("PASS: Type-A/Type-B contacts, two-finger input, same-tick tap press/release, GPIO SYN filtering, cleanup");
}
