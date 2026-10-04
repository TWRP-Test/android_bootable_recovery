#ifndef GUI2_BACKEND_TWRP_OPERATION_H
#define GUI2_BACKEND_TWRP_OPERATION_H

#include <string>

namespace gui2_backend {

// GUIAction::operation_start and operation_end, which wrap every long running
// job: busy flag, progress reset, perf workload, completion vibration.
void operation_start(const std::string& operation_name);
void operation_end(int operation_status);

// operation_end also wakes the screen (blankTimer.resetTimerAndUnblank). The
// jobs end on worker threads, so the UI thread picks that up here.
bool take_operation_ended();

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_TWRP_OPERATION_H
