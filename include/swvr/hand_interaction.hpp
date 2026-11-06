#pragma once
#include "swvr/camera_bridge.hpp"
#include "swvr/xr_runtime.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>

namespace swvr {
enum class InteractionHand : unsigned { left=0, right=1 };
using HandInteractionClock=std::chrono::steady_clock;
struct HandCalibration {
    Pose local_origin{};
    std::uint64_t space_revision{};
};
struct WorldHandRay {
    std::array<double,3> origin{};
    std::array<double,3> direction{}; // Unit vector, never a point.
};
enum class HandInteractionStatus {
    no_sample, ready, inactive_frame, unfocused, invalid_head, invalid_hand,
    degenerate_direction, calibration_mismatch, invalid_calibration,
    invalid_game_transform, out_of_order, stale, clock_reversal, reset
};
struct HandInteractionSnapshot {
    std::uint64_t sequence{},xr_frame_id{},space_revision{},cancellation_generation{};
    // Monotonic source gesture identity, preserved through its release. This
    // distinguishes open->pinch->open->pinch between two logic reads.
    std::uint64_t pinch_gesture_id{};
    std::int64_t xr_time_ns{}; // OpenXR clock; never compared to published_at.
    HandInteractionClock::time_point published_at{};
    InteractionHand hand{InteractionHand::right};
    bool valid_ray{};
    bool pinch_down{}; // Read-only source state; false for invalid/stale input.
    WorldHandRay ray{};
    // Head gaze (stereo centre, view direction) in the same world space. Valid
    // with a tracked, focused head even when the hand cannot point; it never
    // carries a pinch and must not certify hand actions.
    bool valid_gaze{};
    WorldHandRay gaze{};
    PinchEvent pinch{PinchEvent::none};
    HandInteractionStatus status{HandInteractionStatus::no_sample};
};
struct HandInteractionOptions {
    InteractionHand hand{InteractionHand::right};
    HandInteractionClock::duration max_age{std::chrono::milliseconds(100)};
    float minimum_index_length_metres{0.01f};
    float pinch_press_metres{0.025f},pinch_release_metres{0.040f};
    // One native target at a time. The first deliberate index pinch selects a
    // hand until its release is consumed. Simultaneous pinches keep priority.
    bool both_hands{};
};
class HandInteractionBridge {
public:
    using Clock=HandInteractionClock;
    explicit HandInteractionBridge(HandInteractionOptions options={});
    ~HandInteractionBridge();
    HandInteractionBridge(const HandInteractionBridge&)=delete;
    HandInteractionBridge& operator=(const HandInteractionBridge&)=delete;
    // base_game_transform is the world-space anchor used by the VR camera,
    // before any XR head/eye offsets or renderer floating-origin translation.
    void publish(const XrRuntimeFrame& frame,const HandCalibration& calibration,
                 const Matrix4d& base_game_transform,Clock::time_point now=Clock::now());
    // view_origin includes continuous head-comfort correction. Only the stable
    // calibration identifies a recenter; view movement must not cancel input.
    void publish(const XrRuntimeFrame& frame,const HandCalibration& calibration,
                 const Pose& view_origin,const Matrix4d& base_game_transform,Clock::time_point now=Clock::now());
    // Convenience form: caller guarantees origin belongs to frame.space_revision.
    void publish(const XrRuntimeFrame& frame,const Pose& origin,
                 const Matrix4d& base_game_transform,Clock::time_point now=Clock::now());
    // One logical consumer; its calls may migrate across OS threads. Re-reading
    // a sequence never replays began/released. Stale/invalid input cancels once.
    // The default samples the clock after acquiring the mutex, avoiding a
    // concurrent publication becoming newer than a pre-lock read timestamp.
    HandInteractionSnapshot consume(Clock::time_point now=Clock::time_point::max());
    // Positive pinch transitions require the exact snapshot used by a completed
    // native target selection. A mismatch leaves them unconsumed for a later
    // matching selection. Releases/cancellation still flow to clean up a hold.
    HandInteractionSnapshot consume_selected(std::uint64_t selected_sequence,
                 Clock::time_point now=Clock::time_point::max());
    // Diagnostic copy; never consumes transitions or alters cancellation state.
    // Applies freshness checks and always returns pinch==none.
    HandInteractionSnapshot inspect(Clock::time_point now=Clock::time_point::max()) const;
    // Call on session stop/suppression, calibration reset or game-scene changes.
    // Clears source history and requires an open hand before another pinch.
    void reset(Clock::time_point now=Clock::now());
    void set_priority(InteractionHand,Clock::time_point now=Clock::now());
private:
    HandInteractionSnapshot consume_impl(std::uint64_t selected_sequence,bool selected,Clock::time_point now);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
const char* hand_interaction_status_name(HandInteractionStatus status) noexcept;
}
