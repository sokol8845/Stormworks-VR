#pragma once
#include "swvr/avatar_pose_gl.hpp"
#include "swvr/native_avatar_identity.hpp"
#include <cstdint>
#include <string>

namespace swvr {
// Bodies of the "#<name>_totals" sidecar lines written at XR shutdown. Format:
// unique "key=value" items separated by single spaces. scripts/run-game-preview.ps1
// matches some keys with regexes; tests/trace_report_tests.cpp pins that contract.
struct HeadLimitTotals {std::uint64_t frames{},limited_frames{};float max_extent{},max_correction{};};
std::string format_avatar_pose_totals(const AvatarGlStats&);
std::string format_avatar_identity_totals(const AvatarIdentityStats&);
std::string format_head_limit_totals(const HeadLimitTotals&);
}
