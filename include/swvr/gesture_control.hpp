#pragma once
#include "swvr/hand_locomotion.hpp"
namespace swvr {
struct GestureChange {GestureSettings settings;LocomotionMode mode{LocomotionMode::joystick};};
enum class GestureChangeStatus { idle,waiting,ready,fault,timeout };
// Render-thread coordinator. The caller inhibits publication atomically with
// native handoff BEFORE requesting. Invalid input leaves the old config intact.
class GestureChangeQueue {
public:
    using Clock=std::chrono::steady_clock;
    void request(GestureChange,Clock::time_point now);
    GestureChangeStatus poll(bool native_holding,bool native_fault,Clock::time_point now);
    std::optional<GestureChange> take();
    bool pending() const noexcept {return pending_.has_value();}
private:
    std::optional<GestureChange> pending_;
    Clock::time_point started_{};
    GestureChangeStatus status_{GestureChangeStatus::idle};
};
}
