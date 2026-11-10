#include "swvr/control_practice.hpp"
#include "swvr/hand_closure.hpp"
#include <cmath>
namespace swvr {
void ControlPractice::reset_detectors(){movement_.reset("practice");turn_.reset();steering_.reset();observed_=completed_=false;}
void ControlPractice::start(const GestureSettings& settings){
    validate_gesture_settings(settings);settings_=settings;settings_.input.movement_fists=false;settings_.joystick.activation=JoystickActivation::whole_hand;
    movement_.configure(settings_);movement_.set_mode(LocomotionMode::joystick);
    phase_=PracticePhase::waiting;stage_=PracticeStage::movement;open_since_.reset();time_.reset();frame_=revision_=0;panel_.cancel();reset_detectors();
}
void ControlPractice::request_exit(){if(active()){phase_=PracticePhase::leaving;open_since_.reset();reset_detectors();panel_.cancel();}}
void ControlPractice::change_stage(PracticeStage stage){stage_=stage;phase_=PracticePhase::waiting;open_since_.reset();reset_detectors();panel_.cancel();}
void ControlPractice::open_panel(const XrRuntimeFrame& f){
    const bool running=phase_==PracticePhase::running;
    const std::array<PanelButton,4> buttons{{{0,"ZAKONCZ CWICZENIA",0,-.26f,.4f,.06f,true},
        {1,"POPRZEDNI",-.105f,-.18f,.19f,.065f,running},{2,"DALEJ",.105f,-.18f,.19f,.065f,running},
        {3,"PRZYCISK TESTOWY",0,-.02f,.4f,.10f,running&&stage_==PracticeStage::click}}};
    PanelOptions options;options.offset={0,.18f,-settings_.input.menu_distance_m};options.scale=settings_.input.menu_scale;options.dwell_ms=settings_.input.dwell_ms;
    panel_.open(f,buttons,"CWICZENIA STEROWANIA",options);panel_.set_preferred_hand(static_cast<unsigned>(settings_.input.pointing_priority));
}
void ControlPractice::update(const XrRuntimeFrame& f,bool released,bool fault){
    if(!active())return;
    const bool valid=f.id&&f.running&&f.focused&&f.should_render&&!f.terminal&&!f.system_input_suppressed&&f.predicted_display_time_ns>=0&&f.position_valid&&f.orientation_valid&&f.position_tracked&&f.orientation_tracked&&hand_closed(f.hands[0],false,settings_.arm_swing).has_value()&&hand_closed(f.hands[1],false,settings_.arm_swing).has_value();
    if(!valid){reset_detectors();open_since_.reset();panel_.cancel();if(phase_!=PracticePhase::leaving)phase_=PracticePhase::waiting;return;}
    if(time_&&(f.id<=frame_||f.predicted_display_time_ns<=*time_)){
        reset_detectors();open_since_.reset();panel_.cancel();time_.reset();
        if(phase_!=PracticePhase::leaving)phase_=PracticePhase::waiting;return;
    }
    if(time_&&(revision_!=f.space_revision||f.predicted_display_time_ns-*time_>100'000'000)){reset_detectors();open_since_.reset();panel_.cancel();if(phase_!=PracticePhase::leaving)phase_=PracticePhase::waiting;}
    time_=f.predicted_display_time_ns;frame_=f.id;revision_=f.space_revision;
    if((!released||fault)&&phase_==PracticePhase::running){phase_=PracticePhase::waiting;open_since_.reset();reset_detectors();panel_.cancel();}
    if(phase_!=PracticePhase::running){
        const auto left=hand_closed(f.hands[0],false,settings_.arm_swing),right=hand_closed(f.hands[1],false,settings_.arm_swing);
        // Leaving must stay possible after a native fault: the backends refuse
        // input on their own, and a held command may then never drain.
        const bool settled=phase_==PracticePhase::leaving?released||fault:released&&!fault;
        if(settled&&left&&right&&!*left&&!*right){if(!open_since_)open_since_=f.predicted_display_time_ns;}
        else open_since_.reset();
        if(open_since_&&f.predicted_display_time_ns-*open_since_>=200'000'000){
            if(phase_==PracticePhase::leaving){phase_=PracticePhase::inactive;panel_.cancel();return;}
            phase_=PracticePhase::running;panel_.cancel();
        }
    }
    if(!panel_.snapshot().visible)open_panel(f);
    if(phase_!=PracticePhase::running){panel_.set_value(phase_==PracticePhase::leaving?"KONIEC - OTWORZ OBIE DLONIE":"OTWORZ OBIE DLONIE");panel_.set_notice(fault?"BLAD WEJSCIA GRY - STEROWANIE WSTRZYMANE":!released?"CZEKAM NA ZWOLNIENIE WEJSCIA GRY":"TRZYMAJ OTWARTE DLONIE PRZEZ CHWILE");
        if(const auto action=panel_.update(f);action&&*action==0)request_exit();return;}
    const std::array<const char*,4> names{"1/4 CHODZENIE","2/4 OBROT","3/4 KIEROWNICA","4/4 KLIKANIE"};
    panel_.set_value(std::string(names[static_cast<unsigned>(stage_)])+(completed_?" - ZALICZONE":""));
    panel_.set_notice(completed_?"ZALICZONE - WYBIERZ DALEJ":"CWICZENIA - GESTY NIE STERUJA GRA");
    auto panel_frame=f;
    const auto open=[&](unsigned hand){const auto closed=hand_closed(f.hands[hand],false,settings_.arm_swing);return closed&&!*closed;};
    const auto now=HandLocomotion::Clock::time_point{}+std::chrono::nanoseconds(f.predicted_display_time_ns);
    if(stage_==PracticeStage::movement){
        movement_.publish(f,true,now);const auto command=movement_.inspect(now);observed_|=command.forward>.5f;
        if(observed_&&!command.active&&open(static_cast<unsigned>(settings_.input.joystick_hand)))completed_=true;
        if(!f.hands[static_cast<unsigned>(settings_.input.joystick_hand)].active)observed_=false;
        panel_.set_description(settings_.input.joystick_hand==GestureHand::left?"ZAMKNIJ LEWA PIESC, PRZESUN DO PRZODU, POTEM OTWORZ.":"ZAMKNIJ PRAWA PIESC, PRZESUN DO PRZODU, POTEM OTWORZ.");
        if(command.active)panel_frame.hands[static_cast<unsigned>(settings_.input.joystick_hand)].active=false;
    }else if(stage_==PracticeStage::turn){
        const auto delta=turn_.update(f,settings_,true);observed_|=std::abs(delta)>.0001;
        if(observed_&&!turn_.using_hand()&&open(1u-static_cast<unsigned>(settings_.input.joystick_hand)))completed_=true;
        if(!f.hands[1u-static_cast<unsigned>(settings_.input.joystick_hand)].active)observed_=false;
        panel_.set_description(settings_.input.joystick_hand==GestureHand::left?"ZAMKNIJ PRAWA PIESC, PRZESUN W BOK, POTEM OTWORZ.":"ZAMKNIJ LEWA PIESC, PRZESUN W BOK, POTEM OTWORZ.");
        if(turn_.using_hand())panel_frame.hands[1u-static_cast<unsigned>(settings_.input.joystick_hand)].active=false;
    }else if(stage_==PracticeStage::steering){
        steering_.publish(f,settings_,1,true,now);const auto command=steering_.inspect(now);observed_|=std::abs(command.angle_radians)>.25f;
        if(observed_&&!command.active&&open(0)&&open(1))completed_=true;
        if(!f.hands[0].active||!f.hands[1].active)observed_=false;
        panel_.set_description("ZLAP OBIEMA DLONMI, OBROC UMOWNA KIEROWNICE, POTEM PUSC.");
        for(unsigned h=0;h<2;++h)if(command.hands[h])panel_frame.hands[h].active=false;
    }else panel_.set_description("DOTKNIJ PRZYCISKU TESTOWEGO LUB WSKAZ GO DO KONCA PASKA.");
    if(const auto action=panel_.update(panel_frame)){
        if(*action==0)request_exit();
        else if(*action==1)change_stage(static_cast<PracticeStage>((static_cast<unsigned>(stage_)+3)%4));
        else if(*action==2)change_stage(static_cast<PracticeStage>((static_cast<unsigned>(stage_)+1)%4));
        else if(*action==3&&stage_==PracticeStage::click)completed_=true;
    }
}
}
