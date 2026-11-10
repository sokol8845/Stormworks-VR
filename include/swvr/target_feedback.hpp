#pragma once
#include "swvr/hand_interaction.hpp"
namespace swvr {
enum class TargetFeedbackState { unknown,ready,blocked,held };
struct TargetFeedback {
    TargetFeedbackState state{TargetFeedbackState::unknown};
    HandInteractionSnapshot sample;
    HandInteractionClock::time_point observed{};
    const char* reason{"no_selection"}; // Static text; no native target pointer.
};
// A recent selection may precede the render publication. Small ray jitter is
// tolerated (1 cm, 2 degrees); motion, lifecycle change, a direction that is not
// a unit vector, or 100 ms expiry revokes it.
TargetFeedback current_target_feedback(TargetFeedback,const HandInteractionSnapshot&,HandInteractionClock::time_point now=HandInteractionClock::now())noexcept;
const char* target_feedback_hint(const TargetFeedback&)noexcept;
}
