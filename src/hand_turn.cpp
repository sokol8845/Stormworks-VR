#include "swvr/hand_turn.hpp"
#include "swvr/control_recording.hpp"
#include "swvr/hand_closure.hpp"
#include <algorithm>
#include <cmath>

namespace swvr {
void HandTurn::clear_grip() noexcept {closed_=snap_latched_=false;rate_=0;neutral_.reset();}
void HandTurn::reset() noexcept {clear_grip();time_.reset();frame_=revision_=0;if(recorder_)recorder_->turn_reset();}
double HandTurn::update(const XrRuntimeFrame& f,const GestureSettings& settings,bool allowed) {
    const auto delta=update_impl(f,settings,allowed);if(recorder_)recorder_->turn(f,settings,allowed,delta,closed_);return delta;
}
double HandTurn::update_impl(const XrRuntimeFrame& f,const GestureSettings& settings,bool allowed) {
    if(!allowed||!f.running||!f.focused||!f.should_render||f.terminal||
       !f.position_valid||!f.orientation_valid||!f.position_tracked||!f.orientation_tracked||
       movement_hand_!=settings.input.joystick_hand||f.space_revision!=revision_)clear_grip();
    const auto movement_hand=static_cast<unsigned>(settings.input.joystick_hand);
    if(movement_hand>1){clear_grip();return 0;}
    const auto& hand=f.hands[1u-movement_hand];const auto& wrist=hand.joints[1];
    const bool eligible=allowed&&settings.joystick.activation==JoystickActivation::whole_hand&&f.running&&f.focused&&f.should_render&&!f.terminal&&
        f.position_valid&&f.orientation_valid&&f.position_tracked&&f.orientation_tracked&&hand.active&&wrist.position_valid&&wrist.position_tracked;
    const auto closed=eligible?hand_closed(hand,closed_,settings.arm_swing):std::optional<bool>{};
    // Loss of tracking or an opened hand revokes ownership even if the runtime
    // repeats a timestamp. Deduplication only suppresses integration.
    if(!closed||!*closed)clear_grip();
    // Never integrate a duplicate or backwards XR timestamp, even after a
    // focus/tracking loss. A long gap reanchors instead of catching up.
    if(!f.id||f.predicted_display_time_ns<0){clear_grip();return 0;}
    if(time_&&(f.id<=frame_||f.predicted_display_time_ns<=*time_)){
        if(f.id!=frame_||f.predicted_display_time_ns!=*time_)clear_grip();
        return 0;
    }
    const auto elapsed=time_?f.predicted_display_time_ns-*time_:0;
    if(!time_||f.space_revision!=revision_||elapsed>100'000'000||movement_hand_!=settings.input.joystick_hand)clear_grip();
    movement_hand_=settings.input.joystick_hand;
    time_=f.predicted_display_time_ns;frame_=f.id;revision_=f.space_revision;
    if(!closed||!*closed)return 0;
    try {
        const auto head=gravity_aligned_origin({f.eyes[0].pose,f.eyes[1].pose});
        const auto p=wrist.pose.position;const Vec3 relative{p.x-head.position.x,p.y-head.position.y,p.z-head.position.z};
        const float reach=std::hypot(relative.x,relative.y,relative.z);
        if(!std::isfinite(reach)||reach>1.2f){clear_grip();return 0;}
        if(!closed_){closed_=true;neutral_=relative;axes_=head.orientation;rate_=0;return 0;}
        const Vec3 difference{relative.x-neutral_->x,relative.y-neutral_->y,relative.z-neutral_->z};
        const auto d=compose(inverse(Pose{axes_,{}}),Pose{{},difference}).position;
        const float distance=std::hypot(d.x,d.y,d.z);
        if(!std::isfinite(distance)||distance>.45f){clear_grip();return 0;}
        const auto& stick=settings.turn;
        const float amount=std::clamp((std::abs(d.x)-stick.dead_zone_m)/(stick.full_deflection_m-stick.dead_zone_m),0.f,1.f);
        if(stick.snap){rate_=0;if(std::abs(d.x)<=stick.dead_zone_m)snap_latched_=false;
            if(amount>=.75f&&!snap_latched_){snap_latched_=true;return std::copysign(static_cast<double>(stick.snap_degrees)*.017453292519943295,d.x);}return 0;}
        const float maximum=stick.speed_degrees*.01745329252f;
        rate_=std::copysign(amount*maximum,d.x);
        return rate_*static_cast<double>(elapsed)/1'000'000'000.;
    }catch(...){clear_grip();return 0;}
}
}
