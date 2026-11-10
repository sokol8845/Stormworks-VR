#include "swvr/native_locomotion.hpp"
#include "swvr/control_practice.hpp"
#include "steering_fixture.hpp"
#include "native_hand_buttons_fixture.hpp"
#include <windows.h>
#include <MinHook.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace swvr {
void test_locomotion_steering_setup(HandSteering&);
void test_locomotion_setup(std::uintptr_t,HandLocomotion&,void(*)(void*,void*,void*),void*(*)(std::size_t),void(*)(void*,void*),void(*)(void*,std::size_t));
void test_locomotion_send(std::uintptr_t,void*,void*,void*);
std::array<void*,4> test_locomotion_lifecycle_setup(void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t),void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,int*),void(*)(void*,std::uint64_t,void*,const int*),std::uintptr_t(*)(void*,std::uint64_t,void*,std::uint16_t,const void*,std::uint64_t));
}
unsigned hook_calls{};
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){++hook_calls;return MH_ERROR_NOT_INITIALIZED;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_OK;}
namespace {
using namespace swvr;
using U=std::uintptr_t;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class T>void put(U p,std::size_t at,T v){std::memcpy(reinterpret_cast<void*>(p+at),&v,sizeof(v));}
std::vector<unsigned char> owner_data(0xce200),simulation_data(0x4000),world_data(0xbba10),actor_data(0x2200),actor2_data(0x2200),node_data(0x100),holder_data(8),context_data(0x700),pause_data(0x100),queue_data(0x30);
U owner=reinterpret_cast<U>(owner_data.data()),sim=reinterpret_cast<U>(simulation_data.data()),world=reinterpret_cast<U>(world_data.data()),actor=reinterpret_cast<U>(actor_data.data()),node=reinterpret_cast<U>(node_data.data()),holder=reinterpret_cast<U>(holder_data.data()),data=reinterpret_cast<U>(context_data.data()),pause=reinterpret_cast<U>(pause_data.data()),queue=reinterpret_cast<U>(queue_data.data());
U image=0x140000000;
unsigned originals{},allocations{},frees{};bool throw_push{},invalidate_in_original{},invalidate_in_allocate{},keys_in_allocate{},invalidate_in_push{};
std::vector<NativeMovementRequest> packets;
std::vector<float> axes;
// Consumer contract verified at 860177 (axis store) and 4E4972..4E49A8:
// a seat ignores analog axis values while actor.input.analog_enabled is zero.
// Request 101 changes a value only; request 1 carries the input mode as well.
bool receiver_analog_enabled{};float receiver_axis{};
std::array<unsigned char,0x45> receiver_input{};
bool refresh_native_input{};
float seat_axis(){return receiver_analog_enabled?receiver_axis:0.f;}
std::vector<U> queues;
void original(void* o,void* c,void* q){require(reinterpret_cast<U>(o)==owner&&reinterpret_cast<U>(c)==data+0x280&&reinterpret_cast<U>(q)==queue,"original ABI changed");++originals;if(invalidate_in_original)put(owner,0x350,U{});
    if(refresh_native_input){std::memcpy(receiver_input.data(),reinterpret_cast<void*>(sim+0x1a10),receiver_input.size());receiver_analog_enabled=receiver_input[0x40]!=0;std::memcpy(&receiver_axis,receiver_input.data()+0x10,4);}}
void* allocate(std::size_t n){require(n==0x20||n==0x18||n==0x60,"wrong native allocation size");++allocations;if(invalidate_in_allocate)put(holder,0,reinterpret_cast<U>(actor2_data.data()));if(keys_in_allocate)put(sim,0x1a10,std::array<std::uint64_t,2>{move_back,0x8000});return ::operator new(n);}
void destroy(void* r,std::size_t bytes){require(bytes==0x20||bytes==0x18||bytes==0x60,"wrong unsubmitted delete size");::operator delete(r);++frees;}
void push(void* q,void* r){require(reinterpret_cast<U>(q)==queue||reinterpret_cast<U>(q)==data+0x280+0x340,"queue was cached or substituted");queues.push_back(reinterpret_cast<U>(q));std::uint32_t type{};std::memcpy(&type,static_cast<char*>(r)+8,4);
    if(type==101){U vt{};std::memcpy(&vt,r,8);float value{};std::memcpy(&value,static_cast<char*>(r)+0x14,4);require(vt==image+0xb636b8&&static_cast<unsigned char*>(r)[0x10]==0,"wrong axis header/index");axes.push_back(value);receiver_axis=value;destroy(r,0x18);}
    else if(type==1){U vt{};std::memcpy(&vt,r,8);require(vt==image+0xb57d80,"wrong full-input vtable");
        std::memcpy(receiver_input.data(),static_cast<char*>(r)+0x10,receiver_input.size());
        std::memcpy(&receiver_axis,receiver_input.data()+0x10,4);receiver_analog_enabled=receiver_input[0x40]!=0;
        axes.push_back(receiver_axis);destroy(r,0x60);}
    else {packets.push_back(*static_cast<NativeMovementRequest*>(r));destroy(r,0x20);}if(invalidate_in_push)put(holder,0,reinterpret_cast<U>(actor2_data.data()));if(throw_push)throw std::runtime_error("handoff outcome unknown");}
struct Fixture {
    HandLocomotion bridge;XrRuntimeFrame frame;HandLocomotion::Clock::time_point now=HandLocomotion::Clock::now();
    Fixture() {
        GestureSettings legacy;legacy.joystick.activation=JoystickActivation::middle_pinch;legacy.arm_swing.activation=RunActivation::alternating;bridge.configure(legacy);
        for(auto* v:{&owner_data,&simulation_data,&world_data,&actor_data,&actor2_data,&node_data,&holder_data,&context_data,&pause_data})std::fill(v->begin(),v->end(),0);
        originals=allocations=frees=0;throw_push=invalidate_in_original=invalidate_in_allocate=keys_in_allocate=invalidate_in_push=false;packets.clear();queues.clear();
        axes.clear();receiver_analog_enabled=false;receiver_axis=0;receiver_input={};refresh_native_input=false;
        put(owner,0,image+0xb0f4a0);put(owner,0xce190,sim);put(owner,0x350,sim);put(owner,0xce150,pause);
        put(sim,0,image+0xb1a988);put(sim,0x38,world);put(world,0xbb9f8,node);put(node,0,4);put(node,0x90,holder);put(holder,0,actor);
        put(data,0,image+0xb17cc0);put(pause,0,image+0xb07c10);
        put(sim,0x3c50,pause);put(sim,0x3c68,pause);
        frame.running=frame.focused=frame.should_render=frame.position_valid=frame.orientation_valid=frame.position_tracked=frame.orientation_tracked=true;
        frame.eyes[0].pose.position={-.032f,1.6f,0};frame.eyes[1].pose.position={.032f,1.6f,0};frame.hands[0].active=true;
        for(auto i:{1u,5u,10u,15u})frame.hands[0].joints[i].position_valid=frame.hands[0].joints[i].position_tracked=true;
        test_locomotion_setup(image,bridge,original,allocate,push,destroy);tick();
    }
    void sample(float gap,float forward=0) {
        ++frame.id;frame.predicted_display_time_ns=1000000000LL+static_cast<std::int64_t>(frame.id)*16000000;
        auto& h=frame.hands[0];h.joints[1].pose.position={-.3f,1.1f,-.35f-forward};h.joints[5].pose.position={-.3f,1.1f,-.4f-forward};
        h.joints[15].pose.position={-.3f+gap,1.1f,-.4f-forward};h.joints[10].pose.position={-.3f,1.1f,-.5f-forward};
        bridge.publish(frame,true,HandLocomotion::Clock::now());
    }
    void run(){sample(.06f);sample(.01f);sample(.01f,.14f);tick();require(!packets.empty()&&packets.back().buttons[0]==0x84,"native sprint not emitted");}
    void swing_sample(float left,float right,bool closed=true) {
        ++frame.id;frame.predicted_display_time_ns+=20'000'000;
        for(unsigned h=0;h<2;++h) {
            auto& hand=frame.hands[h];hand.active=true;
            for(auto& joint:hand.joints)joint.position_valid=joint.position_tracked=true;
            const Vec3 wrist{h?.25f:-.25f,1.1f,-.18f+(h?right:left)};hand.joints[1].pose.position=wrist;
            unsigned finger{};
            for(unsigned p:{7u,12u,17u,22u}) {
                const float x=wrist.x-.03f+.022f*static_cast<float>(finger++);
                hand.joints[p].pose.position={x,wrist.y,wrist.z-.075f};
                hand.joints[p+3].pose.position={x,wrist.y+(closed?-.025f:0),wrist.z+(closed?-.05f:-.165f)};
            }
        }
        bridge.publish(frame,true,HandLocomotion::Clock::now());tick();
    }
    void tick(U caller=0x86a36b){test_locomotion_send(caller,reinterpret_cast<void*>(owner),reinterpret_cast<void*>(data+0x280),reinterpret_cast<void*>(queue));}
};
using TestLoop=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t);
using TestShortLoop=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,int*);
using TestDeactivate=void(*)(void*,std::uint64_t,void*,const int*);
using TestReplace=U(*)(void*,std::uint64_t,void*,std::uint16_t,const void*,std::uint64_t);
U replace_thunk{};unsigned native_transitions{};bool throw_transition{};
std::array<float,3> transition_pose{1,2,3};
U native_replace(void* w,std::uint64_t graphics,void* context,std::uint16_t player,const void* pose,std::uint64_t connection) {
    require(reinterpret_cast<U>(w)==world&&graphics==0x4242&&reinterpret_cast<U>(context)==data+0x280&&player==7&&pose==transition_pose.data()&&connection==0x11223344,"replacement ABI lost an argument");
    require(!native_locomotion_stats().holding&&packets.back().buttons[0]==0&&queues.back()==data+0x5c0,"replacement reached native code with own movement held");
    ++native_transitions;if(throw_transition)throw std::runtime_error("native replacement failed");
    put(holder,0,reinterpret_cast<U>(actor2_data.data()));return 0x12345678;
}
void native_lifecycle_loop(void* o,std::uint64_t graphics,std::uint64_t d,std::uint64_t context) {
    require(reinterpret_cast<U>(o)==owner&&graphics==0x4242&&d==data&&context==data+0x280,"loop ABI changed");
    require(reinterpret_cast<TestReplace>(replace_thunk)(reinterpret_cast<void*>(world),graphics,reinterpret_cast<void*>(context),7,transition_pose.data(),0x11223344)==0x12345678,"replacement return changed");
}
void native_lifecycle_short(void* o,std::uint64_t graphics,std::uint64_t d,std::uint64_t context,int* extra) {
    require(extra&&*extra==42,"short-loop stack argument lost");++*extra;native_lifecycle_loop(o,graphics,d,context);
}
void native_deactivate(void* s,std::uint64_t opaque,void* d,const int* state) {
    require(reinterpret_cast<U>(s)==sim&&opaque==0x4242&&reinterpret_cast<U>(d)==data&&reinterpret_cast<U>(state)==owner+0x358,"deactivation ABI changed");
    require(!native_locomotion_stats().holding&&packets.back().buttons[0]==0,"native deactivation preceded movement release");++native_transitions;
}
}
int main(){try{
    for(unsigned physical_mode=0;physical_mode<2;++physical_mode){
        Fixture f;HandSteering steering;test_locomotion_steering_setup(steering);put(actor,0x2100,U{42});
        put(sim,0x1a10,std::array<std::uint64_t,2>{0x8000,0xabcdef});
        put(sim,0x1a20,std::array<float,8>{physical_mode?0.f:.6f,.4f,-.5f,.2f,0,0,0,0});
        put(sim,0x1a40,std::array<float,4>{.2f,.3f,.4f,.5f});put(sim,0x1a50,static_cast<unsigned char>(physical_mode));
        std::array<unsigned char,0x45> physical{};std::memcpy(physical.data(),reinterpret_cast<void*>(sim+0x1a10),physical.size());
        refresh_native_input=true;f.tick();std::uint64_t id{};
        auto sample=[&](float angle,bool closed){auto frame=steering_fixture::hands(angle,closed,closed);frame.id=++id;frame.predicted_display_time_ns+=static_cast<std::int64_t>(id)*20'000'000;
            steering.publish(frame,GestureSettings{},native_steering_seat(),true);f.tick();};
        sample(0,false);sample(0,true);for(int i=1;i<=12;++i)sample(i*.05f,true);
        require(seat_axis()>.25f&&native_locomotion_stats().holding,"native refresh or dormant gamepad axis disabled wheel steering");
        for(unsigned i=0;i<physical.size();++i){
            if((i>=0x10&&i<0x14)||i==0x40)continue;
            const auto expected=!physical_mode&&i>=0x14&&i<0x30?0:physical[i];
            require(receiver_input[i]==expected,"steering changed unrelated input or woke a dormant axis");
        }
        require(std::memcmp(physical.data(),reinterpret_cast<void*>(sim+0x1a10),physical.size())==0,"steering modified physical input memory");
        sample(.6f,false);
        require(receiver_input==physical&&!native_locomotion_stats().holding&&allocations==frees,"release failed to restore physical input and analog mode");
    }
    for(int stop=0;stop<7;++stop){Fixture f;HandSteering steering;test_locomotion_steering_setup(steering);put(actor,0x2100,U{42});f.tick();
        std::uint64_t id{};auto sample=[&](float angle,bool closed){auto frame=steering_fixture::hands(angle,closed,closed);frame.id=++id;frame.predicted_display_time_ns+=static_cast<std::int64_t>(id)*20'000'000;
            steering.publish(frame,GestureSettings{},native_steering_seat(),true);f.tick();};
        sample(0,false);sample(0,true);for(int i=1;i<=12;++i)sample(i*.05f,true);
        require(!axes.empty()&&axes.back()>.25f&&native_locomotion_stats().holding&&packets.empty(),"seat steering did not emit analog input or also emitted walking");
        require(seat_axis()>.25f,"vehicle steering stayed at zero: axis packets did not enable analog seat input");
        if(stop==0)sample(.6f,false);
        if(stop==1){put(pause,0x58,1);f.tick();}
        if(stop==2){put(actor,0x2100,U{});f.tick();}
        if(stop==3){put(actor,0x2100,U{43});f.tick();}
        if(stop==4){put(sim,0x1a20,.7f);put(sim,0x1a50,static_cast<unsigned char>(1));f.tick();}
        if(stop==5){steering.reset();f.tick();}
        if(stop==6){auto cb=native_locomotion_lifecycle_callbacks();require(cb.begin(cb.context,owner,data+0x280),"steering lifecycle release rejected");cb.end(cb.context,true);}
        require(!native_locomotion_stats().holding&&axes.back()==(stop==4?.7f:0.f)&&allocations==frees,"steering release did not preserve physical axis or leaked");
        require(receiver_analog_enabled==(stop==4),"steering release did not restore the physical analog mode");
        require(native_locomotion_stats().steering_nonzero_packets>0&&native_locomotion_stats().steering_max_axis>.25f,"steering diagnostics failed to record nonzero input");
    }
    HandLocomotion no_source;require(!install_native_locomotion(1,false,no_source)&&hook_calls==0,"false SHA inspected image");
    {Fixture f;f.run();require(originals==2&&native_locomotion_stats().holding,"original or held state lost");
     const auto before=packets.size();f.tick(0x1234);require(packets.size()==before&&originals==3,"unknown caller emitted or skipped original");
     f.sample(.06f);f.tick(0x86cdeb);require(packets.back().buttons[0]==0&&!native_locomotion_stats().holding,"open gesture did not release");
     const auto& r=packets.front();require(r.type==100&&r.metadata==0&&r.vtable==image+0xb5ee18,"native request header");
     require(allocations==frees,"request ownership did not transfer");}
    for(int mode=0;mode<9;++mode) {
        if(mode==0){
            Fixture changing;changing.run();const auto before=packets.size();
            require(pause_native_locomotion(true)&&native_locomotion_stats().holding&&packets.size()==before,"pause invoked native cleanup outside a live scope");
            changing.sample(.06f);changing.sample(.01f);changing.sample(.01f,.14f);
            require(!changing.bridge.inspect().active&&!pause_native_locomotion(false),"paused source restarted or resumed before release");
            changing.tick();require(!native_locomotion_stats().holding&&packets.back().buttons[0]==0,"paused adapter did not release in live callback");
            require(pause_native_locomotion(false),"drained source could not resume");
            changing.sample(.01f,.14f);require(!changing.bridge.inspect().active,"mode resume reused a closed gesture");changing.run();
        }
        Fixture f;f.run();
        if(mode==0)put(pause,0x58,1);
        if(mode==1)put(actor,0x2100,U{0x1234});
        if(mode==2)put(sim,0x25b0,U{0x1234});
        if(mode==3)put(data,0x233,static_cast<unsigned char>(1));
        if(mode==4)f.bridge.reset();
        if(mode==5){f.frame.focused=false;f.sample(.01f,.14f);}
        if(mode==6)put(pause,0x24,static_cast<unsigned char>(1));
        if(mode==7)put(sim,0x3c70,static_cast<unsigned char>(1));
        if(mode==8)put(sim,0x3c71,static_cast<unsigned char>(1));
        f.tick();require(packets.back().buttons[0]==0&&!native_locomotion_stats().holding,"mode/focus/reset failed to release");
    }
    {Fixture f;f.run();const std::array<std::uint64_t,2> physical{move_back|0x8000,0xabcdef};put(sim,0x1a10,physical);f.tick();
     require(packets.back().buttons==physical,"release cleared physical keyboard/high buttons");
     require(!f.bridge.inspect().active,"physical movement failed to cancel gesture");}
    {Fixture f;put(sim,0x1a10,std::array<std::uint64_t,2>{0x8000,0xabcdef});f.sample(.06f);f.sample(.01f);f.sample(.01f,.14f);f.tick();
     require(packets.back().buttons[0]==(0x8000|0x84)&&packets.back().buttons[1]==0xabcdef,"unrelated buttons lost");
     std::array<std::uint64_t,2> source{};std::memcpy(&source,reinterpret_cast<void*>(sim+0x1a10),sizeof(source));
     require(source[0]==0x8000,"mod changed physical input memory");}
    {Fixture f;f.run();put(holder,0,reinterpret_cast<U>(actor2_data.data()));const auto before=packets.size();f.tick();
     require(packets.size()==before&&native_locomotion_stats().faults==1&&native_locomotion_stats().holding,"actor change guessed a late release");}
    {Fixture f;f.run();const auto before=packets.size();invalidate_in_original=true;f.tick();require(packets.size()==before,"identity not rechecked after original callback");}
    {Fixture f;f.run();const auto before=packets.size();invalidate_in_allocate=true;f.tick();
     require(packets.size()==before&&allocations==frees&&native_locomotion_stats().faults==1&&native_locomotion_stats().holding,"allocation reentry emitted into another actor or lost hold evidence");}
    {Fixture f;f.run();keys_in_allocate=true;f.tick();
     require(packets.back().buttons==std::array<std::uint64_t,2>{move_back,0x8000}&&!native_locomotion_stats().holding&&allocations==frees,"physical input during allocation did not replace own sprint with a preserving release");}
    {Fixture f;f.run();invalidate_in_push=true;f.tick();
     require(native_locomotion_stats().faults==1&&native_locomotion_stats().holding&&allocations==frees,"handoff identity change lost unresolved movement");}
    {
        swvr::hand_buttons_fixture::Image mapped;image=mapped.base();Fixture f;
        const auto detours=test_locomotion_lifecycle_setup(native_lifecycle_loop,native_lifecycle_short,native_deactivate,native_replace);
        const auto full=mapped.add_caller(0x84cd83,4,detours[0]),short_call=mapped.add_caller(0x84cb35,5,detours[1]);
        const auto deactivate_call=mapped.add_caller(0x84c6f1,4,detours[2]);
        replace_thunk=mapped.add_caller(0x852d06,6,detours[3]);mapped.register_unwind();
        put(data+0x280,0x338,std::uint16_t{7});native_transitions=0;throw_transition=false;
        f.run();reinterpret_cast<TestDeactivate>(deactivate_call)(reinterpret_cast<void*>(sim),0x4242,reinterpret_cast<void*>(data),reinterpret_cast<const int*>(owner+0x358));
        f.tick();f.run();reinterpret_cast<TestLoop>(full)(reinterpret_cast<void*>(owner),0x4242,data,data+0x280);
        put(holder,0,actor);f.tick();f.run();int extra=42;
        reinterpret_cast<TestShortLoop>(short_call)(reinterpret_cast<void*>(owner),0x4242,data,data+0x280,&extra);
        require(extra==43&&native_transitions==3&&native_locomotion_stats().lifecycle_releases==3&&!native_locomotion_stats().faults&&allocations==frees,"standalone hooks did not complete all known transitions");
        put(holder,0,actor);f.tick();f.run();throw_transition=true;bool threw{};
        try{reinterpret_cast<TestLoop>(full)(reinterpret_cast<void*>(owner),0x4242,data,data+0x280);}catch(const std::runtime_error&){threw=true;}
        require(threw&&native_locomotion_stats().faults==1&&!native_locomotion_stats().holding,"native exception was swallowed or barrier retained movement");
        image=0x140000000;
    }
    {Fixture f;f.run();const auto cb=native_locomotion_lifecycle_callbacks();
     require(cb.begin(cb.context,owner,data+0x280),"recognized pre-transition was rejected");
     require(!native_locomotion_stats().holding&&native_locomotion_stats().lifecycle_releases==1&&packets.back().buttons[0]==0&&queues.back()==data+0x280+0x340,"movement not released in the old live queue before transition");
     const auto before=packets.size();f.sample(.06f);f.sample(.01f);f.sample(.01f,.14f);f.tick();require(packets.size()==before,"movement escaped transition barrier");
     put(holder,0,reinterpret_cast<U>(actor2_data.data()));cb.end(cb.context,true);f.tick();f.run();
     require(native_locomotion_stats().faults==0,"recognized character transition left a permanent block");}
    {Fixture f;f.run();put(sim,0x1a10,std::array<std::uint64_t,2>{move_back|0x8000,0xabcdef});const auto cb=native_locomotion_lifecycle_callbacks();
     require(cb.begin(cb.context,owner,data+0x280),"pre-deactivation failed");cb.end(cb.context,true);
     require(packets.back().buttons==std::array<std::uint64_t,2>{move_back|0x8000,0xabcdef},"lifecycle release cleared physical keys");}
    {Fixture f;f.run();const auto cb=native_locomotion_lifecycle_callbacks();require(!cb.begin(cb.context,owner+8,data+0x280),"unknown context authorized release");
     require(native_locomotion_stats().holding&&native_locomotion_stats().faults==1,"unknown lifecycle discarded hold evidence");}
    {Fixture f;f.run();throw_push=true;f.sample(.06f);f.tick();const auto before=packets.size();f.tick();
     require(packets.size()==before&&native_locomotion_stats().faults==1&&allocations==frees,"uncertain handoff retried or double freed");}
    {Fixture f;f.bridge.set_mode(LocomotionMode::arm_swing);f.swing_sample(0,0,false);f.swing_sample(0,0);
     auto stroke=[&](float from,float to,unsigned steps){for(unsigned i=1;i<=steps;++i){const float x=from+(to-from)*static_cast<float>(i)/static_cast<float>(steps);f.swing_sample(x,-x);}};
     stroke(0,-.1f,10);stroke(-.1f,.1f,20);require(packets.empty(),"incomplete swing rhythm emitted native movement");
     stroke(.1f,-.1f,20);require(!packets.empty()&&packets.back().buttons[0]==0x84&&native_locomotion_stats().holding,"arm-swing did not reach the actual native adapter");
     require(f.bridge.suppresses_hand_actions(),"running fists did not reserve interactions");
     for(int i=0;i<13;++i)f.swing_sample(-.1f,.1f);
     require(packets.back().buttons[0]==0&&!native_locomotion_stats().holding,"stopping swings did not emit native release");
     require(allocations==frees&&native_locomotion_stats().faults==0,"arm-swing request ownership imbalance");}
    {Fixture f;f.bridge.configure({});f.bridge.set_mode(LocomotionMode::arm_swing);
     f.swing_sample(0,0);require(packets.size()==1&&packets.back().buttons[0]==0x84&&native_locomotion_stats().holding,"two stationary fists did not emit native sprint");
     for(int i=0;i<20;++i)f.swing_sample(0,0);require(packets.back().buttons[0]==0x84,"stationary fists stopped native sprint");
     f.swing_sample(0,0,false);require(packets.back().buttons[0]==0&&!native_locomotion_stats().holding,"opening fists did not release native sprint");
     f.swing_sample(0,0);put(pause,0x58,1);f.tick();require(packets.back().buttons[0]==0&&!native_locomotion_stats().holding,"pause retained fist-run input");
     put(pause,0x58,0);f.tick();f.swing_sample(0,0);require(packets.back().buttons[0]==0x84,"closed fists did not resume after game gates cleared");
     require(allocations==frees&&native_locomotion_stats().faults==0,"fist-run native ownership imbalance");}
    {Fixture f;f.bridge.configure({});f.swing_sample(0,0);f.swing_sample(-.14f,0);
     require(!packets.empty()&&packets.back().buttons[0]==0x84,"whole-hand joystick did not emit native sprint");
     f.swing_sample(-.14f,0,false);require(packets.back().buttons[0]==0&&!native_locomotion_stats().holding,"opening joystick grip did not release native input");}
    {Fixture f;test_locomotion_setup(image,f.bridge,original,allocate,push,destroy);
     require(!native_locomotion_allows_turn(),"turning allowed before verifying a native actor");f.tick();require(native_locomotion_allows_turn(),"valid local walking context rejected turning");
     put(pause,0x58,1);require(!native_locomotion_allows_turn(),"pause did not immediately block turning");put(pause,0x58,0);
     put(actor,0x2100,U{123});require(!native_locomotion_allows_turn(),"seat did not block turning");put(actor,0x2100,U{});
     put(sim,0x3c70,static_cast<unsigned char>(1));require(!native_locomotion_allows_turn(),"game UI did not block turning");put(sim,0x3c70,static_cast<unsigned char>(0));
     require(pause_native_locomotion(true)&&!native_locomotion_allows_turn(),"settings suspension did not block turning");require(pause_native_locomotion(false)&&native_locomotion_allows_turn(),"resumed native context rejected turning");
     put(holder,0,reinterpret_cast<U>(actor2_data.data()));require(!native_locomotion_allows_turn(),"new actor reused old turn context");}
    {   // Control practice with the real adapter: the host pauses it like this
        // and gives the same hand frames to simulation-only detectors.
        Fixture f;f.bridge.configure({});f.swing_sample(0,0);f.swing_sample(-.14f,0);
        require(packets.back().buttons[0]==0x84&&native_locomotion_stats().holding,"fixture did not start a live sprint");
        ControlPractice practice;require(pause_native_locomotion(true),"practice could not pause the adapter");practice.start({});
        const auto step=[&](float left,bool closed){f.swing_sample(left,0,closed);const auto s=native_locomotion_stats();practice.update(f.frame,!s.holding,s.faults!=0);};
        step(-.14f,true);require(!native_locomotion_stats().holding&&packets.back().buttons[0]==0,"entering practice did not release the held sprint in a live callback");
        const auto sent=packets.size();
        for(int i=0;i<13;++i)step(0,false);require(practice.phase()==PracticePhase::running,"practice did not arm after the native release");
        step(0,true);for(int i=0;i<30;++i)step(-.14f,true);
        require(practice.sticks()[0].sprint,"practice detector did not see the sprint gesture");
        require(packets.size()==sent&&!native_locomotion_stats().holding&&!f.bridge.inspect().active,"a practice gesture reached the game");
        require(!native_locomotion_allows_turn(),"turning stayed available during practice");
        practice.request_exit();for(int i=0;i<5;++i)step(-.14f,true);require(practice.active()&&packets.size()==sent,"exit with a closed fist finished or sent input");
        for(int i=0;i<13;++i)step(0,false);require(!practice.active()&&packets.size()==sent,"practice did not finish cleanly on open hands");
        require(pause_native_locomotion(false),"adapter did not resume after practice");
        f.swing_sample(0,0);f.swing_sample(-.14f,0);require(packets.size()>sent&&packets.back().buttons[0]==0x84,"live sprint did not return after practice");
        f.swing_sample(-.14f,0,false);require(packets.back().buttons[0]==0&&!native_locomotion_stats().holding&&allocations==frees&&native_locomotion_stats().faults==0,"post-practice release or ownership failed");
    }
    {   // Entering practice while the wheel is held, then turning a fist pair
        // during practice: the seat axis is released and stays untouched.
        Fixture f;HandSteering steering;test_locomotion_steering_setup(steering);put(actor,0x2100,U{42});f.tick();std::uint64_t id{};
        ControlPractice practice;GestureSettings settings;settings.steering.smoothing_ms=0;bool training{};
        const auto sample=[&](float angle,bool closed){auto frame=steering_fixture::hands(angle,closed,closed);frame.id=++id;frame.predicted_display_time_ns+=static_cast<std::int64_t>(id)*20'000'000;
            steering.publish(frame,settings,native_steering_seat(),live_gestures_allowed(training));f.tick();
            if(training){const auto s=native_locomotion_stats();practice.update(frame,!s.holding,s.faults!=0);}};
        sample(0,false);sample(0,true);for(int i=1;i<=12;++i)sample(i*.05f,true);
        require(seat_axis()>.25f&&native_locomotion_stats().holding,"fixture did not hold the wheel");
        require(pause_native_locomotion(true),"practice could not pause steering");practice.start(settings);training=true;
        sample(.6f,true);require(seat_axis()==0&&!native_locomotion_stats().holding,"held wheel was not released when practice started");
        const auto nonzero=native_locomotion_stats().steering_nonzero_packets;
        for(int i=0;i<13;++i)sample(0,false);require(practice.phase()==PracticePhase::running,"practice did not arm after the wheel release");
        sample(0,true);for(int i=1;i<=12;++i)sample(i*.05f,true);
        require(seat_axis()==0&&!native_locomotion_stats().holding&&native_locomotion_stats().steering_nonzero_packets==nonzero&&packets.empty()&&!steering.inspect().active,"a practice wheel turn reached the seat");
        practice.request_exit();for(int i=0;i<13;++i)sample(0,false);require(!practice.active(),"practice did not finish on open hands");
        training=false;require(pause_native_locomotion(false),"steering did not resume after practice");
        // The first live frame re-identifies the seat; the grip needs open hands seen after that.
        sample(0,false);sample(0,false);sample(0,true);for(int i=1;i<=12;++i)sample(i*.05f,true);require(seat_axis()>.25f,"live steering did not return after practice");
        sample(.6f,false);require(!native_locomotion_stats().holding&&allocations==frees&&native_locomotion_stats().faults==0,"post-practice steering release failed");
    }
    std::cout<<"PASS: native movement ABI, whole-hand joystick, stationary two-fist sprint, game gates, practice isolation, release and request ownership\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
