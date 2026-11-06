#include "swvr/arm_swing.hpp"
#include "swvr/hand_closure.hpp"
#include <algorithm>
#include <cmath>

namespace swvr {
namespace {
Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float length(Vec3 v){return std::hypot(v.x,v.y,v.z);}
bool joint_valid(const XrTrackedJoint& j) {
    return j.position_valid&&j.position_tracked&&std::isfinite(length(j.pose.position));
}
bool tracked(const XrTrackedHand& hand) {
    if(!hand.active||!joint_valid(hand.joints[1]))return false;
    for(unsigned proximal:{7u,12u,17u,22u})
        if(!joint_valid(hand.joints[proximal])||!joint_valid(hand.joints[proximal+3]))return false;
    return true;
}
bool fist(const XrTrackedHand& hand,bool was_closed,const ArmSwingSettings& s) {
    const auto wrist=hand.joints[1].pose.position;
    const float width=length(sub(hand.joints[7].pose.position,hand.joints[22].pose.position));
    if(width<.035f||width>.14f)return false;
    for(unsigned p:{7u,12u,17u,22u}) {
        const auto proximal=hand.joints[p].pose.position,tip=hand.joints[p+3].pose.position;
        const float knuckle=length(sub(proximal,wrist));
        if(knuckle<.035f||knuckle>.16f||
           length(sub(tip,wrist))>knuckle*(was_closed?s.fist_open_wrist_ratio:s.fist_close_wrist_ratio)||
           length(sub(tip,proximal))>width*(was_closed?s.fist_open_knuckle_ratio:s.fist_close_knuckle_ratio))return false;
    }
    return true;
}
bool in_zone(Vec3 p,unsigned hand) {
    const float side=hand?p.x:-p.x;
    return side>=.07f&&side<=.6f&&p.y<=-.15f&&p.y>=-1.f&&p.z>=-.65f&&p.z<=.35f;
}
}
void ArmSwingDetector::stop_motion() noexcept {
    if(running_)++stats_.stops;
    running_=false;phase_=last_confirmed_phase_=0;streak_=0;phase_confirmed_=false;travel_={};
    last_motion_.reset();last_stroke_.reset();
}
void ArmSwingDetector::reset() noexcept {
    stop_motion();armed_=false;uses_hands_=false;fists_={};previous_.reset();previous_time_.reset();
    status_=ArmSwingStatus::tracking;
}
void ArmSwingDetector::configure(const ArmSwingSettings& settings) {
    GestureSettings s;s.arm_swing=settings;validate_gesture_settings(s);reset();settings_=settings;
}
bool ArmSwingDetector::update(const XrRuntimeFrame& frame,bool allowed) {
    ++stats_.frames;
    if(!allowed||!frame.id||frame.predicted_display_time_ns<0||!frame.running||frame.terminal||!frame.should_render||!frame.focused||
       !frame.position_valid||!frame.orientation_valid||!frame.position_tracked||!frame.orientation_tracked) {
        ++stats_.invalid_tracking;reset();return false;
    }
    if(settings_.activation==RunActivation::two_fists){
        const auto left=hand_closed(frame.hands[0],fists_[0],settings_),right=hand_closed(frame.hands[1],fists_[1],settings_);
        const bool reserved=left.value_or(false)||right.value_or(false);
        if(!left||!right){++stats_.invalid_tracking;reset();uses_hands_=reserved;return false;}
        fists_={*left,*right};uses_hands_=reserved;
        if(*left&&*right){if(!running_)++stats_.starts;running_=true;status_=ArmSwingStatus::running;return true;}
        ++stats_.fists_open;stop_motion();status_=ArmSwingStatus::close_fists;return false;
    }
    try {
        const auto center=stereo_center({frame.eyes[0].pose,frame.eyes[1].pose});
        const auto heading=gravity_aligned_origin({frame.eyes[0].pose,frame.eyes[1].pose}).orientation;
        const auto axes=inverse(Pose{heading,{}});
        const auto neck=compose(center,Pose{{},head_neck_offset}).position;
        std::array<Vec3,2> relative;
        bool missing{},outside{},reserved{};
        for(unsigned h=0;h<2;++h) {
            if(!tracked(frame.hands[h])){missing=true;continue;}
            relative[h]=sub(frame.hands[h].joints[1].pose.position,neck);
            const auto body=compose(axes,Pose{{},relative[h]}).position;
            const bool near=std::isfinite(length(body))&&in_zone(body,h);outside=outside||!near;
            fists_[h]=fist(frame.hands[h],fists_[h],settings_);
            reserved=reserved||(near&&fists_[h]);
        }
        // One recognized fist still reserves the activator when the other hand
        // disappears or leaves the zone. It must not become a cockpit pinch.
        if(missing){++stats_.invalid_tracking;reset();uses_hands_=reserved;return false;}
        if(outside){++stats_.outside_zone;reset();uses_hands_=reserved;status_=ArmSwingStatus::body_zone;return false;}
        uses_hands_=fists_[0]||fists_[1];
        if(!(fists_[0]&&fists_[1])) {
            ++stats_.fists_open;stop_motion();previous_.reset();previous_time_.reset();
            // After an interruption both hands must be visibly open once.
            // Closing the fists need not happen in the same XR frame.
            armed_=armed_||(!fists_[0]&&!fists_[1]);status_=armed_?ArmSwingStatus::close_fists:ArmSwingStatus::open_hands;return false;
        }
        if(!armed_){status_=ArmSwingStatus::open_hands;return false;}
        status_=ArmSwingStatus::alternate;
        const auto time=frame.predicted_display_time_ns;
        if(!previous_||!previous_time_) {previous_=relative;previous_time_=time;previous_heading_=heading;return false;}
        const auto dt_ns=time-*previous_time_;
        if(dt_ns<=0||dt_ns>100'000'000){++stats_.rejected_motion;reset();uses_hands_=reserved;status_=ArmSwingStatus::rejected;return false;}
        const float dt=static_cast<float>(static_cast<double>(dt_ns)*1e-9);
        std::array<Vec3,2> delta;
        const auto turn=compose(Pose{heading,{}},inverse(Pose{previous_heading_,{}}));
        float expected_turn{},turn_residual{};
        bool opposing=true;
        for(unsigned h=0;h<2;++h) {
            // Difference in LOCAL first, then project. Head turning alone must
            // not manufacture a hand velocity; shared head/body motion cancels.
            delta[h]=compose(axes,Pose{{},sub(relative[h],(*previous_)[h])}).position;
            const auto turned=compose(turn,Pose{{},(*previous_)[h]}).position;
            expected_turn+=length(sub(turned,(*previous_)[h]));
            turn_residual+=length(sub(relative[h],turned));
            const float speed=length(delta[h])/dt;
            if(!std::isfinite(speed)||speed>settings_.maximum_speed_mps){++stats_.rejected_motion;reset();uses_hands_=reserved;status_=ArmSwingStatus::rejected;return false;}
            opposing=opposing&&std::abs(delta[h].z)/dt>=settings_.minimum_speed_mps&&
                std::abs(delta[h].z)>=.6f*std::hypot(delta[h].x,delta[h].y);
        }
        previous_=relative;previous_time_=time;previous_heading_=heading;
        // Both fists rotating rigidly with the headset are a body turn, not
        // alternating arm strokes. A head-only turn leaves wrist deltas zero.
        if(expected_turn>.001f&&turn_residual<=expected_turn*.25f) {
            ++stats_.rigid_turns;opposing=false;
        }
        const auto stop_ns=static_cast<std::int64_t>(settings_.stop_delay_ms)*1'000'000;
        const auto max_stroke_ns=static_cast<std::int64_t>(settings_.maximum_half_stroke_ms)*1'000'000;
        if((last_motion_&&time-*last_motion_>stop_ns)||
           (running_&&last_stroke_&&time-*last_stroke_>max_stroke_ns))stop_motion();
        opposing=opposing&&delta[0].z*delta[1].z<0;
        if(!opposing) {
            if(phase_&&!phase_confirmed_&&time-phase_since_>stop_ns)stop_motion();
            status_=running_?ArmSwingStatus::running:ArmSwingStatus::alternate;
            return running_;
        }
        ++stats_.opposed_samples;
        const int phase=delta[0].z<0?1:-1;
        if(phase_!=phase) {
            phase_=phase;phase_since_=time-dt_ns;phase_confirmed_=false;travel_={};
        }
        for(unsigned h=0;h<2;++h)travel_[h]+=std::abs(delta[h].z);
        if(travel_[0]>=.015f&&travel_[1]>=.015f)last_motion_=time;
        if(!phase_confirmed_&&travel_[0]>=settings_.stroke_m&&travel_[1]>=settings_.stroke_m&&time-phase_since_>=static_cast<std::int64_t>(settings_.minimum_half_stroke_ms)*1'000'000) {
            phase_confirmed_=true;++stats_.half_strokes;
            if(last_stroke_&&time-*last_stroke_>max_stroke_ns){streak_=0;last_confirmed_phase_=0;}
            streak_=last_confirmed_phase_==phase?1:std::min(settings_.start_half_strokes,streak_+1);
            last_confirmed_phase_=phase;last_stroke_=time;
            if(streak_==settings_.start_half_strokes&&!running_){running_=true;++stats_.starts;}
        }
        status_=running_?ArmSwingStatus::running:ArmSwingStatus::alternate;return running_;
    }catch(...){++stats_.invalid_tracking;reset();return false;}
}
}
