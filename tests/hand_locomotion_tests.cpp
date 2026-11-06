#include "swvr/hand_locomotion.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace swvr;
using namespace std::chrono;
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct Fixture {
    HandLocomotion stick;XrRuntimeFrame f;unsigned ticks{};
    HandLocomotion::Clock::time_point now{};
    Fixture() {
        GestureSettings legacy;legacy.joystick.activation=JoystickActivation::middle_pinch;stick.configure(legacy);
        f.running=f.focused=f.should_render=f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
        f.space_revision=4;f.eyes[0].pose.position={-.032f,1.6f,0};f.eyes[1].pose.position={.032f,1.6f,0};
        auto& h=f.hands[0];h.active=true;
        for(auto i:{1u,5u,10u,15u})h.joints[i].position_valid=h.joints[i].position_tracked=true;
        sample(.06f);sample(.01f);
    }
    LocomotionCommand sample(float gap=.01f,float z=0,float x=0,bool allowed=true) {
        ++f.id;f.predicted_display_time_ns=1000000000LL+static_cast<std::int64_t>(++ticks)*16000000;
        now+=milliseconds(16);auto& h=f.hands[0];
        h.joints[1].pose.position={-.3f+x,1.1f,-.35f+z};
        h.joints[5].pose.position={-.3f+x,1.1f,-.4f+z};
        h.joints[15].pose.position={-.3f+x+gap,1.1f,-.4f+z};
        h.joints[10].pose.position={-.3f+x,1.1f,-.5f+z};
        stick.publish(f,allowed,now);return stick.inspect(now);
    }
};
}
int main(){try{
    Fixture right;GestureSettings right_settings;right_settings.joystick.activation=JoystickActivation::middle_pinch;right_settings.input.joystick_hand=GestureHand::right;right.stick.configure(right_settings);
    const auto right_sample=[&](float gap,float forward){right.sample(gap,forward);right.f.hands[1]=right.f.hands[0];right.f.hands[0].active=false;++right.f.id;right.f.predicted_display_time_ns+=1;right.stick.publish(right.f,true,right.now);const auto command=right.stick.inspect(right.now);right.f.hands[0]=right.f.hands[1];return command;};
    right_sample(.06f,0);right_sample(.01f,0);require(right_sample(.01f,-.14f).sprint,"right joystick did not sprint");
    require(right.stick.reserved_hand_actions()==std::array<bool,2>{false,true},"right joystick reserved wrong hand");
    Fixture configured;GestureSettings settings;settings.joystick.activation=JoystickActivation::middle_pinch;settings.joystick.dead_zone_m=.08f;settings.joystick.sprint_on_m=.2f;settings.joystick.sprint_off_m=.16f;
    configured.stick.configure(settings);configured.sample(.06f);configured.sample(.01f);
    require(locomotion_buttons(configured.sample(.01f,-.06f))==0,"configured dead zone ignored");
    require(!configured.sample(.01f,-.14f).sprint,"configured sprint threshold ignored");
    require(configured.sample(.01f,-.22f).sprint,"configured sprint did not start");
    Fixture a;require(a.stick.inspect(a.now).active,"pinch at neutral inactive");
    require(locomotion_buttons(a.sample(.01f,-.02f))==0,"dead zone moved");
    auto c=a.sample(.01f,-.06f);require(c.forward>0&&!c.sprint&&locomotion_buttons(c)==move_forward,"forward walk");
    c=a.sample(.01f,-.14f);require(c.sprint&&locomotion_buttons(c)==(move_forward|move_sprint),"sprint");
    require(a.sample(.01f,-.10f).sprint,"sprint hysteresis missing");
    require(!a.sample(.01f,-.08f).sprint,"sprint failed to disengage");
    require(locomotion_buttons(a.sample(.01f,.08f))==move_back,"backwards mapping");
    require(locomotion_buttons(a.sample(.01f,0,.07f))==move_right,"right mapping");
    require(locomotion_buttons(a.sample(.01f,0,-.07f))==move_left,"left mapping");
    require(!a.sample(.01f,-.14f,.14f).sprint,"sideways sprint");
    require(!a.sample(.06f,-.14f).active,"opening did not stop");

    for(int blocker=0;blocker<9;++blocker) {
        Fixture b;require(b.sample(.01f,-.14f).sprint,"setup sprint");
        if(blocker==0)b.f.focused=false;
        if(blocker==1)b.f.hands[0].active=false;
        if(blocker==2)b.f.hands[0].joints[15].position_tracked=false;
        if(blocker==3)b.f.position_valid=false;
        if(blocker==4)++b.f.space_revision;
        if(blocker==5)b.f.terminal=true;
        if(blocker==6)b.now+=milliseconds(101);
        if(blocker==7)b.stick.reset();
        auto cmd=b.sample(.01f,-.14f,0,blocker!=8);
        require(!cmd.active&&!cmd.sprint,"blocker retained locomotion");
        b.f.focused=b.f.hands[0].active=b.f.hands[0].joints[15].position_tracked=b.f.position_valid=true;b.f.terminal=false;
        require(!b.sample(.01f,-.14f).active,"reacquired closed gesture moved");
        b.sample(.06f);require(b.sample(.01f).active,"open hand failed to rearm");
    }
    Fixture stale;stale.sample(.01f,-.14f);
    require(!stale.stick.inspect(stale.now+milliseconds(101)).active,"stale command kept running");
    Fixture restart;restart.sample(.01f,-.14f);restart.stick.reset();restart.f.id=0;restart.ticks=0;
    restart.sample(.06f);require(restart.sample(.01f).active,"new XR session frame numbers rejected after reset");
    Fixture reversal;reversal.sample(.01f,-.14f);
    require(!reversal.stick.inspect(reversal.now-milliseconds(1)).active,"clock reversal kept running");
    Fixture duplicate;duplicate.sample(.01f,-.14f);duplicate.stick.publish(duplicate.f,true,duplicate.now);
    require(!duplicate.stick.inspect(duplicate.now).active,"duplicate XR frame moved");
    Fixture teleport;require(!teleport.sample(.01f,-.5f).active,"implausible stick displacement moved");
    Fixture nan;require(!nan.sample(.01f,std::numeric_limits<float>::quiet_NaN()).active,"NaN hand moved");
    Fixture turn;const float angle=.8f;
    for(auto& eye:turn.f.eyes)eye.pose.orientation={0,std::sin(angle/2),0,std::cos(angle/2)};
    require(locomotion_buttons(turn.sample())==0,"head turn moved neutral stick");
    // Raising/lowering the hand (including T-pose) has no planar input.
    Fixture vertical;for(auto& eye:vertical.f.eyes)eye.pose.position.y+=.1f;
    require(locomotion_buttons(vertical.sample())==0,"vertical head/hand motion moved");
    Fixture threads;std::thread consumer([&]{for(int i=0;i<1000;++i)threads.stick.inspect(threads.now);});
    for(int i=0;i<1000;++i)threads.stick.reset();consumer.join();
    // The default read clock must be sampled under the mutex: a concurrent
    // publication is not a clock reversal and must not destroy a held sprint.
    {
        HandLocomotion live;GestureSettings legacy;legacy.joystick.activation=JoystickActivation::middle_pinch;live.configure(legacy);auto f=a.f;f.hands[0].active=true;
        f.focused=f.running=f.should_render=f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
        const auto publish=[&](float gap,float z){
            ++f.id;f.predicted_display_time_ns+=1'000'000;auto& h=f.hands[0];
            h.joints[1].pose.position={-.3f,1.1f,-.35f+z};h.joints[5].pose.position={-.3f,1.1f,-.4f+z};
            h.joints[15].pose.position={-.3f+gap,1.1f,-.4f+z};h.joints[10].pose.position={-.3f,1.1f,-.5f+z};
            live.publish(f,true);
        };
        publish(.06f,0);publish(.01f,0);publish(.01f,-.14f);
        require(live.inspect().sprint,"concurrent sprint setup failed");
        std::atomic<bool> start{},done{},lost{};std::atomic<unsigned> reads{};
        std::thread reader([&]{start=true;do {if(!live.inspect().sprint)lost=true;++reads;}while(!done.load());});
        while(!start.load())std::this_thread::yield();
        for(int i=0;i<5000;++i)publish(.01f,-.14f);
        done=true;reader.join();
        require(reads>0&&!lost&&live.inspect().sprint,"concurrent default read cancelled a fresh sprint");
    }
    NativeMovementRequest request{123,100,0,{move_forward|move_sprint,0xabcdef}};
    unsigned char bytes[sizeof(request)];std::memcpy(bytes,&request,sizeof(request));
    require(bytes[8]==100&&bytes[0x10]==0x84&&request.buttons[1]==0xabcdef,"native request wire layout");
    require(locomotion_buttons({1,0,false,true})==0,"inactive sprint command mapped");
    std::cout<<"PASS: hand stick, directions/sprint, release/rearm, tracking/focus/stale/LOCAL, native bits\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
