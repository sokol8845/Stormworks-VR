#pragma once
#include "swvr/hand_recording.hpp"
namespace swvr {
struct ReplayObservation {
    LocomotionCommand command;
    ArmSwingStats swing;
    std::string status,event;
    bool emitted{},sample{},mismatch{},reserved{};
    std::uint64_t expected{},blocks{};
    std::array<std::uint64_t,2> physical{};
};
class LocomotionReplay {
public:
    LocomotionReplay(std::optional<GestureSettings> settings={},std::optional<LocomotionMode> mode={}):override_settings_(settings),override_mode_(mode){}
    ReplayObservation apply(const HandRecord&);
    LocomotionMode mode() const noexcept{return mode_;}
    std::uint64_t configurations() const noexcept{return configurations_;}
private:
    HandLocomotion source_;
    std::optional<GestureSettings> override_settings_;
    std::optional<LocomotionMode> override_mode_;
    LocomotionMode mode_{LocomotionMode::joystick};
    std::uint64_t configurations_{},sequence_{},blocks_{};
    std::int64_t last_sample_{-1},last_observed_{-1};
    bool suspended_{};
    std::array<std::uint64_t,2> physical_{};
    std::string block_reason_;
};
}
