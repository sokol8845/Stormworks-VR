#pragma once
#include "swvr/hand_locomotion.hpp"
#include "swvr/hand_turn.hpp"
#include "swvr/hand_steering.hpp"
#include "swvr/pointing_panel.hpp"
namespace swvr {
enum class PracticeStage : unsigned {movement,turn,steering,click};
enum class PracticePhase {inactive,waiting,running,leaving};
// Owns simulation-only detectors. No native adapter, game pointer, OS input or
// command sink can be supplied to this class. The host gates live sources while active.
class ControlPractice {
public:
    void start(const GestureSettings&);
    void request_exit();
    // A native fault stops the simulation but never the exit: open hands still leave.
    void update(const XrRuntimeFrame&,bool native_released,bool native_fault);
    bool active()const noexcept{return phase_!=PracticePhase::inactive;}
    PracticePhase phase()const noexcept{return phase_;}
    PracticeStage stage()const noexcept{return stage_;}
    bool completed()const noexcept{return completed_;}
    const PanelSnapshot& snapshot()const noexcept{return panel_.snapshot();}
    std::array<StickVisual,2> sticks(){return {movement_.stick_visual(),turn_.stick_visual()};}
    WheelGripVisual wheel(){return steering_.grip_visual();}
private:
    void reset_detectors();void change_stage(PracticeStage);void open_panel(const XrRuntimeFrame&);
    PracticePhase phase_{PracticePhase::inactive};PracticeStage stage_{};GestureSettings settings_;
    HandLocomotion movement_;HandTurn turn_;HandSteering steering_;PointingPanel panel_;
    bool observed_{},completed_{};
    std::optional<std::int64_t> open_since_,time_;
    std::uint64_t frame_{},revision_{};
};
// Central routing predicate used by all live gesture consumers in the host.
constexpr bool live_gestures_allowed(bool practice_active,bool other_modal=false)noexcept{return !practice_active&&!other_modal;}
}
