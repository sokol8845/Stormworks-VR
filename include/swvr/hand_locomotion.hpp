#pragma once
#include "swvr/xr_runtime.hpp"
#include "swvr/arm_swing.hpp"
#include "swvr/locomotion_event.hpp"
#include "swvr/input_visuals.hpp"
#include <deque>
#include <chrono>
#include <mutex>
#include <optional>

namespace swvr {
enum class LocomotionMode { joystick, arm_swing };
struct LocomotionCommand {
    float forward{},sideways{}; // -1..1, relative to the walking/body heading.
    bool active{},sprint{};
};
// Measured keyboard sampler 0x947760: forward/back/left/right, sprint.
inline constexpr std::uint64_t move_forward=4,move_back=8,move_left=1,move_right=2,move_sprint=0x80;
inline constexpr std::uint64_t move_mask=0x18f; // Includes native slow-walk.
std::uint64_t locomotion_buttons(LocomotionCommand) noexcept;
struct NativeMovementRequest {
    std::uintptr_t vtable{};
    std::uint32_t type{100},metadata{};
    std::array<std::uint64_t,2> buttons{};
};
static_assert(sizeof(NativeMovementRequest)==0x20&&offsetof(NativeMovementRequest,buttons)==0x10);
// A whole-hand grip anchors the selected hand's virtual joystick. Open to stop;
// grip at neutral and move 3+ cm to walk, 12 cm forward to sprint (off at 9 cm).
// No game pointers or OS input. Both publication and consumption may reset it.
class HandLocomotion {
public:
    using Clock=std::chrono::steady_clock;
    void publish(const XrRuntimeFrame&,bool walking_allowed,Clock::time_point now=Clock::now());
    // Resolve the default clock under the mutex so a concurrent publication
    // cannot become newer than a timestamp captured while waiting for it.
    LocomotionCommand inspect(Clock::time_point now=Clock::time_point::max());
    LocomotionCommand diagnostic_command(); // No freshness gate; diagnostics only.
    StickVisual stick_visual(){std::lock_guard lock(mutex_);return visual_;}
    void reset(const char* static_reason="external");
    void set_mode(LocomotionMode);
    void configure(const GestureSettings&);
    void suspend(bool); // Remains suspended across reset/configure/mode changes.
    bool suppresses_hand_actions();
    std::array<bool,2> reserved_hand_actions();
    ArmSwingStats arm_swing_stats();
    // ASCII Polish, static strings suitable for the small in-headset overlay.
    const char* status_text();
    void set_native_block(const char* static_reason);
    void note_native_state(std::uint64_t blocks,std::array<std::uint64_t,2> physical);
    void start_journal();
    LocomotionJournalBatch drain_journal(bool stop=false);
private:
    void clear();
    void record(LocomotionEvent,Clock::time_point source_now=Clock::now());
    std::mutex mutex_;
    PinchDetector pinch_;
    bool joystick_closed_{};
    GestureSettings settings_;
    bool suspended_{};
    const char* status_{};
    const char* native_block_{};
    LocomotionCommand command_;
    StickVisual visual_;
    std::optional<Clock::time_point> published_;
    std::optional<std::int64_t> last_xr_time_;
    std::uint64_t revision_{},frame_{};
    bool has_revision_{};
    std::optional<Vec3> neutral_;
    Quaternion axes_;
    LocomotionMode mode_{LocomotionMode::joystick};
    ArmSwingDetector arm_swing_;
    bool journal_enabled_{},journal_overflow_{},native_state_seen_{};
    Clock::time_point journal_epoch_{};
    std::uint64_t journal_sequence_{},native_blocks_{};
    std::array<std::uint64_t,2> native_physical_{};
    std::deque<LocomotionEvent> journal_;
};
}
