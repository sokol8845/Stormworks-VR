#pragma once
#include "swvr/gesture_settings.hpp"
#include "swvr/xr_runtime.hpp"
#include <string>
#include <vector>
namespace swvr {
struct HandRecordingSample {std::int64_t elapsed_ns{};bool walking_allowed{},suspended{};XrRuntimeFrame frame;};
enum class LocomotionEventKind { configuration,sample,reset,gate,suspend,inspect,native_state };
struct LocomotionEvent {
    LocomotionEventKind kind{};
    std::uint64_t sequence{};std::int64_t observed_ns{},source_ns{};
    GestureSettings settings;bool arm_swing{},flag{};
    HandRecordingSample sample;
    std::string reason;
    std::uint64_t bits{},blocks{};
    std::array<std::uint64_t,2> physical{};
};
struct LocomotionJournalBatch {std::vector<LocomotionEvent> events;bool overflow{};};
}
