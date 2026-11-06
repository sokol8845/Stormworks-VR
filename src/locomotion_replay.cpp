#include "swvr/locomotion_replay.hpp"
#include <stdexcept>
namespace swvr {
ReplayObservation LocomotionReplay::apply(const HandRecord& r){
    ReplayObservation out;
    if(r.kind==HandRecordKind::end)return out;
    if(r.journal){if(r.event.sequence!=sequence_+1||r.event.observed_ns<last_observed_)throw std::runtime_error("Journal ordering mismatch");sequence_=r.event.sequence;last_observed_=r.event.observed_ns;}
    if(r.kind==HandRecordKind::configuration){mode_=override_mode_.value_or(r.mode);source_.configure(override_settings_.value_or(r.settings));source_.set_mode(mode_);++configurations_;return out;}
    if(!configurations_)throw std::runtime_error("Recording has no initial configuration");
    const auto time=r.journal?r.event.source_ns:r.sample.elapsed_ns;
    const auto now=HandLocomotion::Clock::time_point{}+std::chrono::nanoseconds(time);
    if(r.kind==HandRecordKind::sample){
        if(!r.journal&&r.sample.elapsed_ns<last_sample_)throw std::runtime_error("Non-monotonic recording samples");last_sample_=r.sample.elapsed_ns;
        if(r.journal&&suspended_!=r.sample.suspended)throw std::runtime_error("Missing journal suspension event");
        if(!r.journal&&suspended_!=r.sample.suspended){suspended_=r.sample.suspended;source_.suspend(suspended_);}
        source_.publish(r.sample.frame,r.sample.walking_allowed,now);out.command=source_.inspect(now);out.sample=true;out.event="sample";
    }else{
        switch(r.event.kind){
        case LocomotionEventKind::reset:source_.reset(r.event.reason.c_str());out.event="reset:"+r.event.reason;break;
        case LocomotionEventKind::gate:block_reason_=r.event.reason;source_.set_native_block(block_reason_.empty()?nullptr:block_reason_.c_str());out.event="gate:"+block_reason_;break;
        case LocomotionEventKind::suspend:suspended_=r.event.flag;source_.suspend(suspended_);out.event=suspended_?"suspended":"resumed";break;
        case LocomotionEventKind::native_state:blocks_=r.event.blocks;physical_=r.event.physical;out.event="native_state";break;
        case LocomotionEventKind::inspect:out.command=source_.inspect(now);out.expected=r.event.bits;out.mismatch=locomotion_buttons(out.command)!=out.expected;out.event="inspect";break;
        default:throw std::runtime_error("Invalid replay event");
        }
    }
    out.command=source_.diagnostic_command();out.emitted=true;out.status=source_.status_text();out.reserved=source_.suppresses_hand_actions();out.swing=source_.arm_swing_stats();out.blocks=blocks_;out.physical=physical_;return out;
}
}
