#pragma once
#include "swvr/xr_runtime.hpp"
#include "swvr/gesture_settings.hpp"
#include <optional>

namespace swvr {
class ControlRecording;
// Independent second-hand joystick: close to anchor, deflect sideways to turn,
// open to stop. update returns a game-yaw delta, at most once per XR frame.
// The caller supplies native walking/UI gates and applies it to body AND view.
class HandTurn {
public:
    void record_to(ControlRecording* recorder)noexcept{recorder_=recorder;}
    double update(const XrRuntimeFrame&,const GestureSettings&,bool allowed);
    void reset() noexcept;
    bool using_hand() const noexcept {return closed_;}
    float rate() const noexcept {return rate_;}
private:
    double update_impl(const XrRuntimeFrame&,const GestureSettings&,bool);
    ControlRecording* recorder_{};
    void clear_grip() noexcept;
    bool closed_{},snap_latched_{};
    float rate_{};
    std::optional<Vec3> neutral_;
    Quaternion axes_{};
    GestureHand movement_hand_{GestureHand::left};
    std::optional<std::int64_t> time_;
    std::uint64_t revision_{},frame_{};
};
}
