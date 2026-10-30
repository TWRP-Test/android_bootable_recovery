// TWRP-only: force-feedback fallback interface.
#pragma once
namespace twrpminui {
int play_force_feedback(int timeout_ms);
bool force_feedback_available();
}
