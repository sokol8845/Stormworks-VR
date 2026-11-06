#include "swvr/numeric_keypad.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace swvr;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct Fixture {
    XrRuntimeFrame frame;NumericKeypad pad;
    Fixture(float yaw=0){frame.id=1;frame.predicted_display_time_ns=1'000'000'000;frame.space_revision=3;
        frame.running=frame.focused=frame.should_render=frame.position_valid=frame.orientation_valid=frame.position_tracked=frame.orientation_tracked=true;
        const Quaternion q{0,std::sin(yaw/2),0,std::cos(yaw/2)};frame.eyes[0].pose={q,{-.032f,1.6f,0}};frame.eyes[1].pose={q,{.032f,1.6f,0}};
        check(pad.open(frame),"tracked head could not open keypad");}
    void aim(unsigned button,bool closed,unsigned hand=1){
        const auto& key=pad.snapshot().buttons.at(button);const auto panel=pad.snapshot().panel;
        auto& h=frame.hands[hand];h.active=true;
        const auto position=[&](float x,float z){return compose(panel,Pose{{},{key.x+x,key.y,z}}).position;};
        for(unsigned j:{5u,7u,10u,15u})h.joints[j].position_valid=h.joints[j].position_tracked=true;
        h.joints[7].pose.position=position(0,.25f);h.joints[10].pose.position=position(0,.15f);h.joints[15].pose.position=position(0,.15f);h.joints[15].pose.position.y-=.07f;
        h.joints[5].pose.position=position(closed?.01f:.06f,.15f);h.joints[5].pose.position.y-=.07f;
        tick();
    }
    void poke(unsigned button,float z,unsigned hand=1){const auto& key=numeric_keypad_buttons()[button];auto& h=frame.hands[hand];h.active=true;
        h.joints[5].position_tracked=h.joints[15].position_tracked=false;
        for(unsigned j:{7u,10u})h.joints[j].position_valid=h.joints[j].position_tracked=true;
        h.joints[10].pose.position=compose(pad.snapshot().panel,Pose{{},{key.x,key.y,z}}).position;h.joints[10].radius=.008f;
        h.joints[7].pose.position=compose(pad.snapshot().panel,Pose{{},{key.x,key.y,z+.08f}}).position;tick();}
    void tick(){++frame.id;frame.predicted_display_time_ns+=20'000'000;pad.update(frame);}
    void click(unsigned key,unsigned hand=1){for(int i=0;i<42;++i)aim(key,false,hand);frame.hands[hand].active=false;tick();}
};
int main(){try{
    {Fixture f;check(f.pad.open_vehicle(f.frame,GestureSettings{},6,{1.5f,-2.f}),"large keypad did not open");f.click(6);f.click(7);f.click(16);f.click(8);f.click(9);f.click(14);
     const auto result=f.pad.take_result();check(result&&result->accepted&&result->native_values==std::optional<std::array<float,2>>({12,-3}),"large keypad channel edit lost values");}
    {Fixture f;check(f.pad.open_vehicle(f.frame,GestureSettings{},5,{1.5f,-2.f}),"small keypad did not open");f.click(13);f.click(14);const auto r=f.pad.take_result();check(r&&r->native_values==std::optional<std::array<float,2>>({0,-2}),"empty numeric entry or hidden channel changed");}
    {Fixture f;f.pad.open_vehicle(f.frame,GestureSettings{},6,{1e30f,-1e-30f});f.click(14);const auto r=f.pad.take_result();check(r&&r->native_values==std::optional<std::array<float,2>>({1e30f,-1e-30f}),"unchanged values lost float precision/range");}
    for(float yaw:{0.f,.7f})for(unsigned hand=0;hand<2;++hand){Fixture f(yaw);
        f.poke(6,.06f,hand);f.poke(6,.006f,hand);check(f.pad.snapshot().value=="1","direct index touch required a pinch or wrong hand");
        for(int i=0;i<5;++i)f.poke(6,.006f,hand);f.poke(7,.006f,hand);
        check(f.pad.snapshot().value=="1","holding or dragging a finger repeated a touch");
        f.poke(7,.06f,hand);f.poke(7,.006f,hand);check(f.pad.snapshot().value=="12","lifting finger did not rearm touch");}
    {Fixture f;f.poke(6,-.01f);check(f.pad.snapshot().value.empty(),"opening a panel around a finger clicked");f.poke(6,.06f);f.poke(6,.006f);f.frame.hands[1].active=false;f.tick();f.poke(7,.006f);check(f.pad.snapshot().value=="1","tracking reacquisition pressed through the panel");}
    {Fixture f;f.poke(6,.06f);f.frame.predicted_display_time_ns+=200'000'000;f.poke(6,.006f);check(f.pad.snapshot().value.empty(),"a tracking gap completed a touch");}
    {Fixture f;f.poke(6,.40f);f.poke(6,.006f);check(f.pad.snapshot().value.empty(),"tracking teleport manufactured a touch");}
    {Fixture f;f.poke(6,.06f);f.frame.focused=false;f.poke(6,.006f);check(!f.pad.snapshot().visible&&!f.pad.take_result()->accepted,"focus loss committed a pending touch");}
    {Fixture f;f.poke(6,.06f);f.poke(6,.006f);f.poke(6,.06f);f.poke(6,.006f);check(f.pad.snapshot().value=="11","deliberate repeated taps were lost");}
    {Fixture f;f.aim(6,false);auto& h=f.frame.hands[1];h.joints[5].pose.position=h.joints[10].pose.position;f.tick();h.joints[5].pose.position.x+=.06f;f.tick();check(f.pad.snapshot().value.empty(),"Quest index pinch clicked the application panel");}
    for(float yaw:{0.f,.7f}){
        Fixture f(yaw);f.aim(0,true);check(f.pad.snapshot().value.empty(),"closed hand on open clicked a key");
        f.click(6);f.click(7);f.click(11);f.click(8);f.click(9);check(f.pad.snapshot().value=="-12.3","digits/sign/decimal mapping wrong");
        f.click(11);check(f.pad.snapshot().value=="-12.3","duplicate decimal accepted");
        f.click(12);check(f.pad.snapshot().value=="-12.","backspace wrong");f.click(10);f.click(14);
        const auto result=f.pad.take_result();check(result&&result->accepted&&result->value=="-12.0"&&!f.pad.snapshot().visible&&!f.pad.take_result(),"OK lost/replayed local result");
    }
    {Fixture f;f.click(14);check(f.pad.snapshot().visible&&!f.pad.take_result(),"empty OK accepted");f.click(11);f.click(14);check(f.pad.snapshot().visible,"decimal alone accepted");f.click(13);check(f.pad.snapshot().value.empty(),"clear wrong");f.click(15);check(!f.pad.take_result()->accepted,"cancel accepted a value");}
    {Fixture f;f.aim(0,false);f.aim(0,true);f.aim(1,false);check(f.pad.snapshot().value.empty(),"drag release clicked another key");}
    {Fixture f;f.click(6,0);check(f.pad.snapshot().value=="1","left pointer did not click");for(int i=0;i<20;++i)f.click(10,0);check(f.pad.snapshot().value.size()==16,"numeric buffer unbounded");}
    {Fixture f;f.aim(0,false);f.aim(0,true);f.frame.focused=false;f.tick();check(!f.pad.snapshot().visible&&!f.pad.take_result()->accepted,"focus loss submitted a key");}
    {Fixture f;f.aim(0,false);f.aim(0,true);++f.frame.space_revision;f.tick();check(!f.pad.snapshot().visible,"recenter retained a stale panel");}
    {Fixture f;f.aim(0,false);f.aim(0,true);f.frame.hands[1].active=false;f.tick();f.aim(0,true);f.aim(0,false);check(f.pad.snapshot().value.empty(),"tracking reacquisition completed an old click");f.click(0);check(f.pad.snapshot().value=="7","tracking interruption did not rearm after open");}
    {Fixture f;f.aim(0,false);f.aim(0,true);f.frame.predicted_display_time_ns+=200'000'000;f.aim(0,false);check(f.pad.snapshot().value.empty(),"long gap completed old press");}
    {Fixture f;f.frame.focused=false;f.pad.cancel();check(!f.pad.open(f.frame),"unfocused head opened keypad");}
    std::cout<<"PASS keypad: direct touch, middle pinch, both hands, transformed panel, edit/accept/cancel and interrupted input\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
