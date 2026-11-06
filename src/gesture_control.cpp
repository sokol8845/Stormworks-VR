#include "swvr/gesture_control.hpp"
#include <stdexcept>
namespace swvr {
void GestureChangeQueue::request(GestureChange change,Clock::time_point now){
    validate_gesture_settings(change.settings);
    if(change.mode!=LocomotionMode::joystick&&change.mode!=LocomotionMode::arm_swing)throw std::invalid_argument("Invalid locomotion mode");
    if(pending_)throw std::logic_error("Gesture change already pending");
    pending_=change;started_=now;status_=GestureChangeStatus::waiting;
}
GestureChangeStatus GestureChangeQueue::poll(bool holding,bool fault,Clock::time_point now){
    if(!pending_)return status_;
    if(fault)status_=GestureChangeStatus::fault;
    else if(now<started_||now-started_>=std::chrono::seconds(5))status_=GestureChangeStatus::timeout;
    else status_=holding?GestureChangeStatus::waiting:GestureChangeStatus::ready;
    if(status_==GestureChangeStatus::fault||status_==GestureChangeStatus::timeout)pending_.reset();
    return status_;
}
std::optional<GestureChange> GestureChangeQueue::take(){
    if(status_!=GestureChangeStatus::ready)return {};
    auto result=pending_;pending_.reset();status_=GestureChangeStatus::idle;return result;
}
}
