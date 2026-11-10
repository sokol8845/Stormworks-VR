#include "swvr/control_practice.hpp"
#include "swvr/target_feedback.hpp"
#include "swvr/vr_settings_menu.hpp"
#include "steering_fixture.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
using namespace swvr;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void shift(XrTrackedHand& h,float x,float z){for(auto& j:h.joints){j.pose.position.x+=x;j.pose.position.z+=z;}}
struct PracticeFixture {
    ControlPractice practice;GestureSettings settings;XrRuntimeFrame frame;std::uint64_t id{};
    PracticeFixture(){settings.steering.smoothing_ms=0;settings.input.dwell_ms=300;practice.start(settings);}
    void step(XrRuntimeFrame f,bool released=true,bool fault=false){f.id=++id;f.predicted_display_time_ns+=static_cast<std::int64_t>(id)*20'000'000;frame=f;practice.update(frame,released,fault);}
    void open(){for(int n=0;n<13;++n)step(steering_fixture::hands(0,false,false));check(practice.phase()==PracticePhase::running,"practice did not arm on open hands");}
    void click(int action){
        const auto state=practice.snapshot();const auto it=std::find_if(state.buttons.begin(),state.buttons.end(),[&](const auto& b){return b.id==action;});check(it!=state.buttons.end(),"practice button missing");const auto b=*it;
        step(steering_fixture::hands(0,false,false));
        for(int n=0;n<22;++n){auto f=steering_fixture::hands(0,false,false);auto& h=f.hands[1];
            // Move the whole open hand so its index points at the button: a
            // finger detached from its wrist is not a hand the practice accepts.
            const auto tip=compose(state.panel,Pose{{},{b.x,b.y,.20f}}).position,from=h.joints[10].pose.position;
            for(auto& j:h.joints){j.pose.position.x+=tip.x-from.x;j.pose.position.y+=tip.y-from.y;j.pose.position.z+=tip.z-from.z;}
            const auto before=practice.stage();const auto phase=practice.phase();const bool done=practice.completed();step(f);
            if(practice.stage()!=before||practice.phase()!=phase||practice.completed()!=done)return;
        }
    }
};
void settings_tests(){
    GestureSettings settings;check(adjust_vr_preferences(VrMenuAction::joystick_visual_toggle,settings)&&settings.input.joystick_visual,"joystick visual toggle");
    check(adjust_vr_preferences(VrMenuAction::wheel_visual_toggle,settings)&&settings.steering.show_grip,"wheel visual toggle");
    check(parse_gesture_settings(format_gesture_settings_ini(settings))==settings,"new preferences did not roundtrip");
    for(const auto text:{"[input]\njoystick_visual=maybe\n","[steering]\nshow_grip=5\n"}){bool threw{};try{parse_gesture_settings(text);}catch(const std::exception&){threw=true;}check(threw,"invalid new bool accepted");}
    const std::array actions{VrMenuAction::joy_dead_less,VrMenuAction::joy_dead_more,VrMenuAction::joy_range_less,VrMenuAction::joy_range_more,VrMenuAction::sprint_on_less,VrMenuAction::sprint_on_more,VrMenuAction::sprint_off_less,VrMenuAction::sprint_off_more};
    std::mt19937 random(71);for(unsigned i=0;i<10000;++i){const auto action=random()%actions.size();const auto before=settings.joystick;adjust_vr_preferences(actions[action],settings);validate_gesture_settings(settings);
        const std::array old{before.dead_zone_m,before.full_deflection_m,before.sprint_on_m,before.sprint_off_m};const auto& j=settings.joystick;const std::array now{j.dead_zone_m,j.full_deflection_m,j.sprint_on_m,j.sprint_off_m};
        check(action%2?now[action/2]>=old[action/2]:now[action/2]<=old[action/2],"plus/minus reversed at limit");}
}
void feedback_tests(){
    const auto now=HandInteractionClock::time_point{}+std::chrono::seconds(5);HandInteractionSnapshot ray;ray.valid_ray=true;ray.sequence=8;ray.space_revision=2;ray.cancellation_generation=3;ray.published_at=now;ray.ray.direction={0,0,-1};
    TargetFeedback confirmed{TargetFeedbackState::ready,ray,now,"ready"};check(current_target_feedback(confirmed,ray,now).state==TargetFeedbackState::ready,"fresh native target lost");
    for(int cause=0;cause<13;++cause){auto next=ray;auto time=now;auto confirmed_ray=confirmed;
        if(cause==0)next.valid_ray=false;if(cause==1)++next.space_revision;if(cause==2)++next.cancellation_generation;if(cause==3)next.hand=InteractionHand::left;
        if(cause==4)next.ray.origin[0]=.02;if(cause==5)next.ray.direction={.1,0,-.995};if(cause==6)next.ray.direction[0]=std::numeric_limits<double>::quiet_NaN();
        if(cause==7)time+=std::chrono::milliseconds(101);if(cause==8)time-=std::chrono::milliseconds(1);if(cause==9)--next.sequence;
        // A direction that is not a unit vector would pass the angle test with any angle.
        if(cause==10)next.ray.direction={.6,0,-1.9};if(cause==11)next.ray.direction={0,0,0};if(cause==12)confirmed_ray.sample.ray.direction={0,0,-2};
        check(current_target_feedback(confirmed_ray,next,time).state==TargetFeedbackState::unknown,"invalid/old selection stayed green");}
    ++ray.sequence;ray.ray.origin[0]=.002;check(current_target_feedback(confirmed,ray,now).state==TargetFeedbackState::ready,"normal next-frame jitter lost confirmation");
}
void visual_tests(){
    GestureSettings settings;settings.input.movement_fists=false;settings.steering.smoothing_ms=0;HandLocomotion movement;HandTurn turn;HandSteering steering;std::uint64_t id{};
    const auto step=[&](XrRuntimeFrame f){f.id=++id;f.predicted_display_time_ns+=static_cast<std::int64_t>(id)*20'000'000;const auto now=HandLocomotion::Clock::time_point{}+std::chrono::nanoseconds(f.predicted_display_time_ns);movement.publish(f,true,now);turn.update(f,settings,true);steering.publish(f,settings,42,true,now);return f;};
    movement.configure(settings);step(steering_fixture::hands(0,false,false));step(steering_fixture::hands(0));auto f=steering_fixture::hands(0);shift(f.hands[0],.04f,-.14f);shift(f.hands[1],.08f,0);f=step(f);
    const auto m=movement.stick_visual(),t=turn.stick_visual();check(m.active&&m.sprint&&m.frame==f.id&&std::abs(m.displacement.z+.14f)<.0001f,"movement guide mismatched actual anchor");check(t.active&&t.hand==1&&t.turn&&std::abs(t.displacement.x-.08f)<.0001f,"turn guide mismatched anchor");
    step(steering_fixture::hands(0,false,false));check(!movement.stick_visual().active&&!turn.stick_visual().active,"released guides stayed active");
    steering.reset();step(steering_fixture::hands(0));check(!steering.grip_visual().established,"reset grip reused closed hands");
    step(steering_fixture::hands(0,false,false));auto moved=steering_fixture::hands(0);for(auto& h:moved.hands)shift(h,.12f,0);step(moved);check(steering.grip_visual().established&&std::abs(steering.grip_visual().plane.position.x-.12f)<.001f,"reset did not establish new wheel centre");
    moved.focused=false;step(moved);check(!steering.grip_visual().visible&&!movement.stick_visual().active&&!turn.stick_visual().active,"focus loss retained active guides");
}
void practice_tests(){
    PracticeFixture f;for(int n=0;n<20;++n)f.step(steering_fixture::hands(0));check(f.practice.phase()==PracticePhase::waiting,"closed hands armed practice");
    for(int n=0;n<20;++n)f.step(steering_fixture::hands(0,false,false),false);check(f.practice.phase()==PracticePhase::waiting,"undrained native command armed practice");
    f.open();f.step(steering_fixture::hands(0));auto moving=steering_fixture::hands(0);shift(moving.hands[0],0,-.14f);f.step(moving);check(f.practice.sticks()[0].sprint,"practice did not simulate sprint");
    f.step(steering_fixture::hands(0,false,false));check(f.practice.completed(),"move then release did not complete practice");
    f.click(2);check(f.practice.stage()==PracticeStage::turn,"practice next button failed");f.open();f.step(steering_fixture::hands(0));auto turning=steering_fixture::hands(0);shift(turning.hands[1],.12f,0);f.step(turning);f.step(steering_fixture::hands(0,false,false));check(f.practice.completed(),"turn exercise failed");
    f.click(2);check(f.practice.stage()==PracticeStage::steering,"steering navigation failed");f.open();f.step(steering_fixture::hands(0));for(int n=1;n<=10;++n)f.step(steering_fixture::hands(n*.05f));check(f.practice.wheel().held[0]&&f.practice.wheel().established,"practice wheel guide missing");f.step(steering_fixture::hands(.5f,false,false));check(f.practice.completed(),"steering exercise failed");
    f.click(2);check(f.practice.stage()==PracticeStage::click,"click navigation failed");f.open();f.click(3);check(f.practice.completed(),"test button did not complete exercise");f.click(0);check(f.practice.phase()==PracticePhase::leaving,"exit button did not begin release barrier");
    for(int n=0;n<20;++n)f.step(steering_fixture::hands(0));check(f.practice.active()&&!live_gestures_allowed(f.practice.active()),"exit leaked held fists to game");
    for(int n=0;n<13;++n)f.step(steering_fixture::hands(0,false,false));check(!f.practice.active()&&live_gestures_allowed(f.practice.active()),"open-hands exit never resumed");
    for(int cause=0;cause<6;++cause){PracticeFixture loss;loss.open();loss.step(steering_fixture::hands(0));loss.step(moving);auto bad=moving;
        if(cause==0)bad.focused=false;if(cause==1)bad.hands[0].active=false;if(cause==2)bad.hands[0].joints[1].position_tracked=false;if(cause==3)bad.system_input_suppressed=true;if(cause==4)++bad.space_revision;
        if(cause==5)loss.practice.update(loss.frame,true,false);else loss.step(bad);
        check(!loss.practice.completed()&&loss.practice.phase()==PracticePhase::waiting,"tracking/focus/time failure counted as successful release");loss.open();check(!loss.practice.completed(),"tracking recovery reused exercise progress");}
    // A fingertip the runtime infers inside a fist is not tracking loss: the live joystick keeps its grip too.
    {PracticeFixture inferred;inferred.open();inferred.step(steering_fixture::hands(0));auto fist=moving;fist.hands[0].joints[10].position_tracked=false;inferred.step(fist);
        check(inferred.practice.phase()==PracticePhase::running&&inferred.practice.sticks()[0].sprint,"inferred fingertip in a fist interrupted the exercise");
        inferred.step(steering_fixture::hands(0,false,false));check(inferred.practice.completed(),"exercise with an inferred fingertip did not complete");}
    PracticeFixture failure;failure.open();failure.step(steering_fixture::hands(0),true,true);check(failure.practice.phase()==PracticePhase::waiting,"native fault did not stop simulation");
    for(int n=0;n<20;++n)failure.step(steering_fixture::hands(0,false,false),false,true);check(failure.practice.phase()==PracticePhase::waiting,"a faulted backend resumed the simulation");
    // The user must not be trapped: exit works with a persistent fault and an undrained hold.
    failure.practice.request_exit();for(int n=0;n<20;++n)failure.step(steering_fixture::hands(0),false,true);check(failure.practice.active(),"exit under fault ignored closed fists");
    for(int n=0;n<13;++n)failure.step(steering_fixture::hands(0,false,false),false,true);check(!failure.practice.active(),"a native fault trapped the user in practice");
    PracticeFixture draining;draining.open();draining.practice.request_exit();for(int n=0;n<20;++n)draining.step(steering_fixture::hands(0,false,false),false);check(draining.practice.active(),"exit did not wait for the native release");
}
int main(){try{settings_tests();feedback_tests();visual_tests();practice_tests();std::cout<<"PASS training lifecycle, isolated detectors, UI, tuning, target leases and guides\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
