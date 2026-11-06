#pragma once
#include "swvr/gesture_settings.hpp"
#include "swvr/xr_runtime.hpp"
#include <optional>
namespace swvr {
// Current, active hand + tracked wrist. Finger tips may be runtime-inferred
// while occluded inside a fist, but must have finite, valid current positions.
// Index and at least two other fingers must be closed; thumb is irrelevant.
std::optional<bool> hand_closed(const XrTrackedHand&,bool was_closed,const ArmSwingSettings&);
}
