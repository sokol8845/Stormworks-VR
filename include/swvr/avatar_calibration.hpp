#pragma once
#include "swvr/avatar_pose.hpp"
#include <chrono>
#include <optional>
#include <deque>

namespace swvr {
struct StableAvatarCalibration {AvatarBodyCalibration body;Vec3 head_position;std::size_t samples{};};
class AvatarCalibrationWindow {
public:
    using Clock=std::chrono::steady_clock;
    bool observe(std::optional<AvatarBodyCalibration>,Vec3 head,Clock::time_point);
    std::optional<StableAvatarCalibration> result(std::uint64_t generation) const;
    void reset() noexcept {samples_.clear();last_seen_.reset();}
private:
    struct Sample {AvatarBodyCalibration body;Vec3 head;Clock::time_point time;};
    std::deque<Sample> samples_;
    std::optional<Clock::time_point> last_seen_;
};
struct AvatarCalibrationContext {
    std::uint64_t actor{},space_revision{};
    AvatarVariant variant{};
    bool operator==(const AvatarCalibrationContext&) const=default;
};
struct AvatarCalibrationHoldResult {bool holding{},request{};float progress{};};
// Static ASCII Polish strings for the headset; detailed codes stay in logs.
const char* avatar_calibration_hint(const AvatarTargets&,const char* issue) noexcept;
// Automatic T-pose: 1.5 s, with tracking gaps up to 300 ms. A gap exceeding
// that limit resets even if the first sample after the gap is valid.
// Actor/model/LOCAL changes end a hold; accepted body proportions are separate.
class AvatarCalibrationHold {
public:
    using Clock=std::chrono::steady_clock;
    AvatarCalibrationHoldResult update(bool valid,AvatarCalibrationContext,Clock::time_point now);
    void accepted() noexcept {latched_=true;}
    bool holding() const noexcept {return since_.has_value()&&!latched_;}
    void reset() noexcept;
    std::uint64_t holds() const noexcept {return holds_;}
    std::chrono::milliseconds longest() const noexcept {return longest_;}
private:
    std::optional<AvatarCalibrationContext> context_;
    std::optional<Clock::time_point> since_,last_valid_,last_observation_;
    std::chrono::milliseconds longest_{};
    std::uint64_t holds_{};
    bool latched_{};
};
}
