#include "swvr/hand_locomotion.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace swvr;
using namespace std::chrono;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Fixture {
    HandLocomotion movement;
    XrRuntimeFrame frame;
    HandLocomotion::Clock::time_point now{};
    float left{},right{},vertical{},yaw{},yaw_step{};
    bool rotate_body{};
    bool closed_left=true,closed_right=true,allowed=true;
    Fixture() {
        GestureSettings legacy;legacy.joystick.activation=JoystickActivation::middle_pinch;legacy.arm_swing.activation=RunActivation::alternating;movement.configure(legacy);
        movement.set_mode(LocomotionMode::arm_swing);
        frame.running=frame.focused=frame.should_render=frame.position_valid=frame.orientation_valid=frame.position_tracked=frame.orientation_tracked=true;
        frame.eyes[0].pose.position={-.032f,1.65f,0};frame.eyes[1].pose.position={.032f,1.65f,0};
        frame.predicted_display_time_ns=1'000'000'000;frame.space_revision=3;
        for(auto& h:frame.hands) {
            h.active=true;
            for(auto& j:h.joints)j.position_valid=j.position_tracked=true;
        }
        closed_left=closed_right=false;sample();closed_left=closed_right=true;sample();
    }
    void pose() {
        const Quaternion q{0,std::sin(yaw/2),0,std::cos(yaw/2)};
        const Vec3 neck{0,1.575f,.08f};
        const auto offset=compose(Pose{q,{}},Pose{{},head_neck_offset}).position;
        const Vec3 center{neck.x-offset.x,neck.y-offset.y,neck.z-offset.z};
        frame.eyes[0].pose={q,{center.x-.032f,center.y,center.z}};
        frame.eyes[1].pose={q,{center.x+.032f,center.y,center.z}};
        for(unsigned h=0;h<2;++h) {
            auto& hand=frame.hands[h];
            Vec3 wrist{h?.25f:-.25f,1.1f+vertical,-.18f+(h?right:left)};
            if(rotate_body) {
                const auto rotated=compose(Pose{q,{}},Pose{{},{wrist.x-neck.x,wrist.y-neck.y,wrist.z-neck.z}}).position;
                wrist={neck.x+rotated.x,neck.y+rotated.y,neck.z+rotated.z};
            }
            hand.joints[1].pose.position=wrist;
            const bool closed=h?closed_right:closed_left;
            unsigned finger{};
            for(unsigned p:{7u,12u,17u,22u}) {
                const float x=-.03f+.022f*static_cast<float>(finger++);
                hand.joints[p].pose.position={wrist.x+x,wrist.y,wrist.z-.075f};
                hand.joints[p+3].pose.position={wrist.x+x,wrist.y+(closed?-.025f:0),wrist.z+(closed?-.05f:-.165f)};
            }
            // A fist also resembles a right index pinch. The mode must reserve
            // the hands even before the running rhythm has been established.
            hand.joints[5].pose.position=hand.joints[10].pose.position;
        }
    }
    LocomotionCommand sample(milliseconds elapsed=milliseconds(20)) {
        ++frame.id;frame.predicted_display_time_ns+=duration_cast<nanoseconds>(elapsed).count();now+=elapsed;
        yaw+=yaw_step;
        pose();movement.publish(frame,allowed,now);return movement.inspect(now);
    }
    LocomotionCommand to(float l,float r,unsigned steps=10) {
        const float l0=left,r0=right;LocomotionCommand command;
        for(unsigned i=1;i<=steps;++i) {
            const float t=static_cast<float>(i)/static_cast<float>(steps);
            left=l0+(l-l0)*t;right=r0+(r-r0)*t;command=sample();
        }
        return command;
    }
    LocomotionCommand stroke(float target,unsigned steps=10){return to(target,-target,steps);}
    void run() {
        require(!stroke(-.1f).active,"single reach started running");
        require(!stroke(.1f,20).active,"two half strokes started running");
        require(stroke(-.1f,20).sprint,"three alternating strokes did not start sprint");
    }
};
}
int main(){try{
    Fixture configured;GestureSettings settings;settings.arm_swing.activation=RunActivation::alternating;settings.arm_swing.start_half_strokes=4;
    configured.movement.configure(settings);configured.closed_left=configured.closed_right=false;configured.sample();
    configured.closed_left=configured.closed_right=true;configured.sample();
    require(!configured.stroke(-.1f).active&&!configured.stroke(.1f,20).active&&!configured.stroke(-.1f,20).active,"configured four-stroke start ignored");
    require(configured.stroke(.1f,20).sprint,"configured four-stroke run failed");
    Fixture run;require(run.movement.suppresses_hand_actions(),"fists did not reserve hand actions");run.run();
    require(std::string(run.movement.status_text())=="BIEG","running headset hint wrong");
    require(locomotion_buttons(run.movement.inspect(run.now))==(move_forward|move_sprint),"wrong native running buttons");
    auto stats=run.movement.arm_swing_stats();require(stats.starts==1&&stats.half_strokes==3,"rhythm totals");
    for(int i=0;i<12;++i)require(run.sample().sprint,"short turnaround gap stopped sprint");
    require(!run.sample().active,"stationary fists kept running beyond 250 ms");
    require(std::string(run.movement.status_text())=="RUSZAJ NAPRZEMIENNIE","stopped rhythm hint wrong");
    // Restarting after a stop requires a fresh rhythm, but not reopening fists.
    require(!run.stroke(.1f,20).active,"stopped rhythm reused old strokes");
    require(!run.stroke(-.1f,20).active,"stopped rhythm reused old strokes");
    require(run.stroke(.1f,20).sprint,"fresh rhythm did not restart");
    run.closed_right=false;require(!run.sample().active,"opening one fist did not stop immediately");
    run.closed_left=false;run.sample();require(!run.movement.suppresses_hand_actions(),"open hands remained reserved");

    {Fixture still;for(int i=0;i<150;++i)require(!still.sample().active,"static fists started running");}
    {Fixture gradual;gradual.closed_left=gradual.closed_right=false;gradual.sample();
     gradual.closed_left=true;for(int i=0;i<10;++i)require(!gradual.sample().active,"one fist started running");
     gradual.closed_right=true;gradual.sample();gradual.run();}
    {Fixture rejected;rejected.left=-.2f;rejected.right=.2f;require(!rejected.sample().active,"jump started running");
     require(rejected.movement.suppresses_hand_actions(),"rejected motion turned fists into cockpit pinches");}
    {Fixture together;for(int i=0;i<6;++i)require(!together.to(i%2?.1f:-.1f,i%2?.1f:-.1f,20).active,"in-phase hands started running");}
    {Fixture single;for(int i=0;i<6;++i)require(!single.to(i%2?.1f:-.1f,0,20).active,"one swinging arm started running");}
    {Fixture open;open.closed_left=open.closed_right=false;for(int i=0;i<6;++i)require(!open.stroke(i%2?.1f:-.1f,20).active,"open-hand swings started running");}
    {Fixture vertical;for(int i=0;i<200;++i){vertical.vertical=.08f*std::sin(static_cast<float>(i)*.2f);require(!vertical.sample().active,"vertical waving started running");}}
    {Fixture jitter;for(int i=0;i<200;++i){jitter.left=i%2?.002f:-.002f;jitter.right=-jitter.left;require(!jitter.sample().active,"tracking jitter started running");}}
    {Fixture jitter;jitter.run();for(int i=0;i<15;++i){jitter.left=-.1f+(i%2?.001f:-.001f);jitter.right=-jitter.left;jitter.sample();}
     require(!jitter.movement.inspect(jitter.now).active,"small jitter prolonged an existing run");}
    {Fixture repeated;repeated.stroke(-.1f);repeated.stroke(-.07f);repeated.stroke(-.17f);repeated.stroke(-.14f);
     require(!repeated.stroke(-.24f).active,"same confirmed direction counted as alternating strokes");}
    for(bool body:{false,true}) {
        Fixture turn;turn.rotate_body=body;
        for(int i=0;i<300;++i) {
            turn.yaw=.6f*std::sin(static_cast<float>(i)*.1f);
            require(!turn.sample().active,body?"rigid body turning started an arm-swing run":"head turning started an arm-swing run");
        }
    }
    {Fixture turning;turning.rotate_body=true;turning.yaw_step=.005f;turning.run();
     require(turning.movement.inspect(turning.now).sprint,"turning rejected deliberate arm strokes");}
    for(int blocker=0;blocker<8;++blocker) {
        Fixture blocked;blocked.run();
        if(blocker==0)blocked.frame.focused=false;
        if(blocker==1)blocked.frame.hands[1].active=false;
        if(blocker==2)blocked.frame.hands[0].joints[25].position_tracked=false;
        if(blocker==3)blocked.allowed=false;
        if(blocker==4)++blocked.frame.space_revision;
        if(blocker==5)blocked.vertical=.65f;
        if(blocker==6)blocked.movement.reset();
        require(!blocked.sample(blocker==7?milliseconds(101):milliseconds(20)).active,"interruption retained running");
        blocked.frame.focused=blocked.frame.hands[1].active=blocked.frame.hands[0].joints[25].position_tracked=blocked.allowed=true;blocked.vertical=0;
        for(int i=0;i<4;++i)require(!blocked.stroke(i%2?.1f:-.1f,20).active,"interruption did not require open hands");
        blocked.closed_left=blocked.closed_right=false;blocked.sample();blocked.closed_left=blocked.closed_right=true;blocked.left=blocked.right=0;blocked.sample();blocked.run();
    }
    {Fixture out;out.frame.hands[0].joints[22].position_tracked=false;out.sample();require(out.movement.arm_swing_stats().invalid_tracking>0,"missing finger tracking not diagnosed");}
    {Fixture lost;lost.run();lost.frame.hands[0].active=false;require(!lost.sample().active&&lost.movement.suppresses_hand_actions(),"one tracked fist became a cockpit pinch after losing the other hand");}
    {Fixture jump;jump.left=-.2f;jump.right=.2f;require(!jump.sample().active&&jump.movement.arm_swing_stats().rejected_motion>0,"teleporting hands accepted");}
    {Fixture bad;bad.left=std::numeric_limits<float>::quiet_NaN();require(!bad.sample().active,"NaN fist accepted");}
    {Fixture stale;stale.run();require(!stale.movement.inspect(stale.now+milliseconds(101)).active,"stale run did not release");}
    {Fixture mode;mode.run();mode.movement.set_mode(LocomotionMode::joystick);require(!mode.movement.inspect(mode.now).active,"mode change retained run");
     require(!mode.sample().active,"fist gestures leaked into joystick mode");}
    std::cout<<"PASS: fist arm-swing cadence, alternation/amplitude, idle/open stop, jitter, blockers/rearm and mode isolation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
