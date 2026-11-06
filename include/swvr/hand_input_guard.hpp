#pragma once
#include "swvr/xr_runtime.hpp"
namespace swvr {
// Respect reserved system input. The optional runtime flag is authoritative;
// otherwise a tracked palm facing the headset is a conservative fallback.
// Returns true when application hand input for the frame was suppressed.
bool suppress_system_hand_input(XrRuntimeFrame&,std::array<bool,2> runtime_system_gestures={});
}
