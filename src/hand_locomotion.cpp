#include "swvr/hand_locomotion.hpp"
#include "swvr/hand_closure.hpp"
#include <algorithm>
#include <cmath>

namespace swvr {
std::uint64_t locomotion_buttons(LocomotionCommand c) noexcept {
    if(!c.active||!std::isfinite(c.forward)||!std::isfinite(c.sideways))return 0;
    std::uint64_t bits{};
    if(c.forward>0)bits|=move_forward;else if(c.forward<0)bits|=move_back;
    if(c.sideways>0)bits|=move_right;else if(c.sideways<0)bits|=move_left;
    if(c.sprint&&c.forward>0)bits|=move_sprint;
    return bits;
}
namespace {
Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float length(Vec3 a){return std::hypot(a.x,a.y,a.z);}
bool valid(const XrTrackedJoint& joint) {
    const auto p=joint.pose.position;
    return joint.position_valid&&joint.position_tracked&&std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
float axis(float metres,const JoystickSettings& s){return std::abs(metres)<=s.dead_zone_m?0.f:std::copysign(std::min(1.f,(std::abs(metres)-s.dead_zone_m)/(s.full_deflection_m-s.dead_zone_m)),metres);}
}
void HandLocomotion::clear(){pinch_.reset();joystick_closed_=false;command_={};neutral_.reset();published_.reset();arm_swing_.reset();}
void HandLocomotion::record(LocomotionEvent event,Clock::time_point now){
    if(!journal_enabled_||journal_overflow_)return;
    if(journal_.size()>=1024){journal_overflow_=true;return;}
    event.sequence=++journal_sequence_;event.observed_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-journal_epoch_).count();
    event.source_ns=std::max(std::int64_t{0},std::chrono::duration_cast<std::chrono::nanoseconds>(now-journal_epoch_).count());if(event.kind==LocomotionEventKind::sample)event.sample.elapsed_ns=event.source_ns;journal_.push_back(std::move(event));
}
void HandLocomotion::start_journal(){std::lock_guard lock(mutex_);journal_epoch_=Clock::now();journal_.clear();journal_sequence_=0;journal_overflow_=false;journal_enabled_=true;native_state_seen_=false;
    clear();last_xr_time_.reset();has_revision_=false;frame_=0;record({.kind=LocomotionEventKind::configuration,.settings=settings_,.arm_swing=mode_==LocomotionMode::arm_swing});
    record({.kind=LocomotionEventKind::suspend,.flag=suspended_});record({.kind=LocomotionEventKind::gate,.reason=native_block_?native_block_:""});}
LocomotionJournalBatch HandLocomotion::drain_journal(bool stop){std::lock_guard lock(mutex_);LocomotionJournalBatch result;result.overflow=journal_overflow_;
    result.events.assign(std::make_move_iterator(journal_.begin()),std::make_move_iterator(journal_.end()));journal_.clear();if(stop)journal_enabled_=false;return result;}
void HandLocomotion::note_native_state(std::uint64_t blocks,std::array<std::uint64_t,2> physical){std::lock_guard lock(mutex_);
    if(!native_state_seen_||blocks!=native_blocks_||physical!=native_physical_){native_state_seen_=true;native_blocks_=blocks;native_physical_=physical;record({.kind=LocomotionEventKind::native_state,.blocks=blocks,.physical=physical});}}
void HandLocomotion::reset(const char* why){std::lock_guard lock(mutex_);record({.kind=LocomotionEventKind::reset,.reason=why?why:"external"});clear();last_xr_time_.reset();status_=nullptr;has_revision_=false;frame_=0;revision_=0;}
void HandLocomotion::set_mode(LocomotionMode mode){std::lock_guard lock(mutex_);clear();status_=nullptr;has_revision_=false;frame_=0;mode_=mode;record({.kind=LocomotionEventKind::configuration,.settings=settings_,.arm_swing=mode_==LocomotionMode::arm_swing});}
void HandLocomotion::configure(const GestureSettings& settings){
    validate_gesture_settings(settings);std::lock_guard lock(mutex_);clear();has_revision_=false;frame_=0;
    settings_=settings;status_=nullptr;pinch_=PinchDetector(settings.joystick.pinch_press_m,settings.joystick.pinch_release_m);
    arm_swing_.configure(settings.arm_swing);
    record({.kind=LocomotionEventKind::configuration,.settings=settings_,.arm_swing=mode_==LocomotionMode::arm_swing});
}
void HandLocomotion::suspend(bool value){std::lock_guard lock(mutex_);suspended_=value;clear();has_revision_=false;frame_=0;record({.kind=LocomotionEventKind::suspend,.flag=value});}
void HandLocomotion::set_native_block(const char* reason){std::lock_guard lock(mutex_);if(reason||native_block_)record({.kind=LocomotionEventKind::gate,.reason=reason?reason:""});native_block_=reason;if(reason)clear();}
const char* HandLocomotion::status_text(){
    std::lock_guard lock(mutex_);if(native_block_)return native_block_;
    if(suspended_)return "ZMIANA USTAWIEN - ZATRZYMANIE";
    if(mode_!=LocomotionMode::arm_swing)return status_?status_:settings_.joystick.activation==JoystickActivation::whole_hand?(settings_.input.joystick_hand==GestureHand::left?"JOYSTICK LEWA - OTWARTA DLON":"JOYSTICK PRAWA - OTWARTA DLON"):(settings_.input.joystick_hand==GestureHand::left?"OTWORZ LEWA DLON":"OTWORZ PRAWA DLON");
    if(status_!=nullptr)return status_;
    switch(arm_swing_.status()) {
    case ArmSwingStatus::tracking:return settings_.arm_swing.activation==RunActivation::two_fists?"BRAK SLEDZENIA DLONI":"POKAZ OBIE DLONIE";
    case ArmSwingStatus::body_zone:return "RECE PRZY CIELE";
    case ArmSwingStatus::open_hands:return "OTWORZ OBIE DLONIE";
    case ArmSwingStatus::close_fists:return settings_.arm_swing.activation==RunActivation::two_fists?"TRYB PIESCI - STOP":"ZAMKNIJ OBIE PIESCI";
    case ArmSwingStatus::alternate:return "RUSZAJ NAPRZEMIENNIE";
    case ArmSwingStatus::running:return "BIEG";
    case ArmSwingStatus::rejected:return "RUCH ZA SZYBKI / PRZERWA";
    }
    return "POKAZ DLONIE";
}
bool HandLocomotion::suppresses_hand_actions(){std::lock_guard lock(mutex_);return mode_==LocomotionMode::arm_swing&&arm_swing_.uses_hands();}
std::array<bool,2> HandLocomotion::reserved_hand_actions(){
    std::lock_guard lock(mutex_);if(mode_==LocomotionMode::arm_swing){const bool used=arm_swing_.uses_hands();return {used,used};}
    std::array<bool,2> result{};result[static_cast<unsigned>(settings_.input.joystick_hand)]=command_.active;return result;
}
ArmSwingStats HandLocomotion::arm_swing_stats(){std::lock_guard lock(mutex_);return arm_swing_.stats();}
LocomotionCommand HandLocomotion::diagnostic_command(){std::lock_guard lock(mutex_);return command_;}
LocomotionCommand HandLocomotion::inspect(Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if(now==Clock::time_point::max())now=Clock::now();
    if(!published_||now<*published_||now-*published_>std::chrono::milliseconds(100)){clear();status_="BRAK SWIEZYCH DANYCH";}
    record({.kind=LocomotionEventKind::inspect,.bits=locomotion_buttons(command_)},now);
    return command_;
}
void HandLocomotion::publish(const XrRuntimeFrame& frame,bool allowed,Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if(journal_enabled_)record({.kind=LocomotionEventKind::sample,.sample={std::chrono::duration_cast<std::chrono::nanoseconds>(now-journal_epoch_).count(),allowed,suspended_,frame}},now);
    if(suspended_){clear();return;}
    if(frame.predicted_display_time_ns<0||(last_xr_time_&&frame.predicted_display_time_ns<=*last_xr_time_)){clear();status_="BRAK SWIEZYCH DANYCH";return;}
    last_xr_time_=frame.predicted_display_time_ns;
    if(published_&&now<*published_){clear();return;}
    if(has_revision_&&revision_==frame.space_revision&&frame.id<=frame_){clear();return;}
    if(!has_revision_||revision_!=frame.space_revision ||
       (published_&&now-*published_>std::chrono::milliseconds(100)))clear();
    has_revision_=true;revision_=frame.space_revision;frame_=frame.id;
    status_=!allowed?"RUCH ZABLOKOWANY":!frame.focused?"BRAK FOKUSU VR":
        !frame.position_tracked||!frame.orientation_tracked?"BRAK SLEDZENIA GLOWY":nullptr;
    if(mode_==LocomotionMode::arm_swing) {
        if(!status_&&(!frame.hands[0].active||!frame.hands[1].active))status_="BRAK SLEDZENIA DLONI";
        const bool running=arm_swing_.update(frame,allowed);
        command_=running?LocomotionCommand{1,0,true,true}:LocomotionCommand{};
        published_=now;return;
    }
    const auto selected=static_cast<unsigned>(settings_.input.joystick_hand);const auto& hand=frame.hands[selected];
    const bool legacy=settings_.joystick.activation==JoystickActivation::middle_pinch;
    constexpr unsigned middle_tip=15;
    if(!allowed||!frame.id||!frame.running||frame.terminal||!frame.focused||!frame.should_render||
       !frame.position_valid||!frame.orientation_valid||!frame.position_tracked||!frame.orientation_tracked||
       !hand.active||!valid(hand.joints[1])||(legacy&&(!valid(hand.joints[xr_thumb_tip])||!valid(hand.joints[middle_tip])||
       !valid(hand.joints[xr_index_tip])))){clear();if(!status_)status_="BRAK SLEDZENIA DLONI";return;}
    // A closed fist or an index pinch is not the deliberate middle pinch.
    if(legacy&&length(sub(hand.joints[xr_index_tip].pose.position,hand.joints[xr_thumb_tip].pose.position))<.045f){clear();status_="OTWORZ PALEC WSKAZUJACY";return;}
    try {
        const auto center=stereo_center({frame.eyes[0].pose,frame.eyes[1].pose});
        const auto relative=sub(hand.joints[1].pose.position,center.position);
        if(!std::isfinite(length(relative))||length(relative)>1.2f){clear();return;}
        PinchEvent gesture{};
        if(legacy)gesture=pinch_.update({true,true,true,true,frame.predicted_display_time_ns,
            hand.joints[xr_thumb_tip].pose.position,hand.joints[middle_tip].pose.position},frame.predicted_display_time_ns);
        else{const auto closed=hand_closed(hand,joystick_closed_,settings_.arm_swing);
            if(!closed){clear();status_="BRAK SLEDZENIA DLONI";return;}
            gesture=*closed?(joystick_closed_?PinchEvent::held:PinchEvent::began):(joystick_closed_?PinchEvent::released:PinchEvent::none);joystick_closed_=*closed;
        }
        published_=now;
        if(gesture==PinchEvent::began) {
            neutral_=relative;axes_=gravity_aligned_origin({frame.eyes[0].pose,frame.eyes[1].pose}).orientation;
        }
        if(gesture==PinchEvent::released||gesture==PinchEvent::cancelled){neutral_.reset();command_={};return;}
        if(!neutral_){command_={};status_=legacy?"KCIUK DO SRODKOWEGO":selected?"JOYSTICK PRAWA - OTWARTA DLON":"JOYSTICK LEWA - OTWARTA DLON";return;}
        // Subtract head translation as well as hand translation: ordinary
        // room-scale motion or turning the head alone must not move the stick.
        const auto delta=compose(inverse(Pose{axes_,{}}),Pose{{},sub(relative,*neutral_)}).position;
        if(!std::isfinite(length(delta))||length(delta)>.45f){clear();return;}
        const float forward=-delta.z;
        const auto& s=settings_.joystick;
        const bool sprint=forward>=(command_.sprint?s.sprint_off_m:s.sprint_on_m)&&(s.forward_only||forward>std::abs(delta.x)*1.5f);
        command_={s.forward_only?std::max(0.f,axis(forward,s)):axis(forward,s),s.forward_only?0.f:axis(delta.x,s),true,sprint};
        status_=sprint?"BIEG":command_.forward||command_.sideways?"CHOD":!legacy?"JOYSTICK - NEUTRALNY":selected?"PRZESUN PRAWA DLON":"PRZESUN LEWA DLON";
    } catch(...) {clear();}
}
}
