#pragma once
#include "swvr/hand_locomotion.hpp"
#include "swvr/hand_steering.hpp"
#include "swvr/hand_interaction.hpp"
#include <string>
#include <vector>
namespace swvr {
struct ControlDiagnostics {
    std::uint64_t frame{},accepted{},denied{},pending{};
    bool camera_current{},menu{},keypad{},calibration{},settings_pending{},seated{},blocked{},own_hold{};
    LocomotionCommand movement;
    SteeringCommand steering;
    float turn_degrees_per_second{};
    bool turning_hand{};
    std::array<bool,2> movement_hands{};
    HandInteractionSnapshot pointing;
    std::string last_denial{"none"};
};
// Read-only: never consumes a gesture, resets an input, or dereferences game pointers.
std::vector<std::string> control_diagnostic_lines(const XrRuntimeFrame&,const GestureSettings&,const ControlDiagnostics&);
}
