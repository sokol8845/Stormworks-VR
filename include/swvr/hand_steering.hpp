#pragma once
#include "swvr/gesture_settings.hpp"
#include "swvr/xr_runtime.hpp"
#include <chrono>
#include <mutex>
#include <optional>
namespace swvr {
struct SteeringCommand {float axis{};std::array<bool,2> hands{};bool active{};};
// The first two-hand grip establishes the rim in LOCAL space. Afterwards either
// hand can steer/regrip. Native seat identity and fresh tracking are mandatory.
class HandSteering {
public:
    using Clock=std::chrono::steady_clock;
    void publish(const XrRuntimeFrame&,const GestureSettings&,std::uintptr_t seat,bool allowed,Clock::time_point now=Clock::now());
    SteeringCommand inspect(Clock::time_point now=Clock::time_point::max());
    void reset();
private:
    void clear(bool geometry);
    std::mutex mutex_;
    SteeringCommand command_;
    std::array<bool,2> armed_{},closed_{};
    std::array<float,2> last_angle_{};
    std::optional<Pose> wheel_;
    float radius_{},angle_{};
    std::uintptr_t seat_{};std::uint64_t revision_{},frame_{};
    std::optional<std::int64_t> time_;
    Clock::time_point published_{};
};
}
