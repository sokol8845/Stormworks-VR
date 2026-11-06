#pragma once
#include "swvr/xr_runtime.hpp"
#include "swvr/gesture_settings.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace swvr {
enum class ArmSwingStatus { tracking,body_zone,open_hands,close_fists,alternate,running,rejected };
struct ArmSwingStats {
    std::uint64_t frames{},invalid_tracking{},fists_open{},outside_zone{},
        opposed_samples{},rejected_motion{},rigid_turns{},half_strokes{},starts{},stops{};
};
// Default: both closed fists run immediately; opening either hand stops.
// The older alternating-stroke algorithm remains for versioned replay only.
// The owner enforces focus, current native context and sample freshness.
class ArmSwingDetector {
public:
    bool update(const XrRuntimeFrame&,bool walking_allowed);
    void reset() noexcept;
    void configure(const ArmSwingSettings&);
    bool uses_hands() const noexcept {return uses_hands_;}
    ArmSwingStats stats() const noexcept {return stats_;}
    ArmSwingStatus status() const noexcept {return status_;}
private:
    void stop_motion() noexcept;
    ArmSwingStats stats_;
    ArmSwingSettings settings_;
    ArmSwingStatus status_{ArmSwingStatus::tracking};
    bool armed_{},running_{},uses_hands_{};
    std::array<bool,2> fists_{};
    std::optional<std::array<Vec3,2>> previous_;
    Quaternion previous_heading_;
    std::optional<std::int64_t> previous_time_,last_motion_,last_stroke_;
    std::int64_t phase_since_{};
    std::array<float,2> travel_{};
    int phase_{},last_confirmed_phase_{};
    unsigned streak_{};
    bool phase_confirmed_{};
};
}
