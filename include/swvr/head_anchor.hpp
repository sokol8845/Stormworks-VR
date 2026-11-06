#pragma once
#include "swvr/camera_bridge.hpp"
#include <array>
#include <cstdint>

namespace swvr {
// Compact flags copied from the named bytes of the native camera owner.
// Names intentionally describe offsets, not unverified user-facing modes.
inline constexpr std::uint8_t head_mode_1a0=1,head_mode_1a4=2,
                              head_mode_1a5=4,head_mode_1a6=8;
struct HeadAnchorSettings {
    std::uint8_t mode_bits{};
    float backoff_distance{}; // Native owner+0x214, including negative values.
    std::array<double,3> world_shift{}; // Owner+0x1E0/+0x1E8/+0x1F0.
};
// Inputs are column-major proper rigid transforms in native game coordinates.
// final_camera is AFTER 719F20. look is the matching current/previous transform,
// obtained at the correct time (previous look is overwritten by 71A140).
// Undo exactly the native translation branch, remove only look ROTATION, and
// retain the pre-backoff world eye point (including any look translation).
// Does not flatten vehicle bank or apply/convert any OpenXR pose.
Matrix4d make_head_anchor(const Matrix4d& final_camera,const Matrix4d& look,
                         const HeadAnchorSettings& settings);
}
