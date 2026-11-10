#include "swvr/native_hand_buttons.hpp"
#include "swvr/native_button_retirement.hpp"
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// This unit fixture replaces retirement only. The separate integration fixture
// runs the real ray observer, retirement hooks and request bindings together.
namespace swvr {
NativeButtonRetirementStatus install_native_button_retirement(std::uintptr_t,bool,const std::filesystem::path&) noexcept {
    return NativeButtonRetirementStatus::installed;
}
NativeButtonRequestApi wrap_native_button_request_api(NativeButtonRequestApi api){return api;}
void note_native_button_retirement_reset() noexcept {}
void flush_native_button_retirement() noexcept {}
}
namespace {
using namespace swvr;
using U=std::uintptr_t;
using Loop=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t);
using ShortLoop=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,int*);
using Dispatch=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,void*);
using Action=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,int,int);
using Dtor=void*(*)(void*,std::uint32_t);
using Deactivate=void(*)(void*,std::uint64_t,void*,const int*);
using Replace=U(*)(void*,std::uint64_t,void*,std::uint16_t,const void*,std::uint64_t);
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class T>void put(U object,std::size_t offset,const T& val){std::memcpy(reinterpret_cast<void*>(object+offset),&val,sizeof(val));}
template<class T>T get(U object,std::size_t offset){T val{};std::memcpy(&val,reinterpret_cast<void*>(object+offset),sizeof(val));return val;}
struct Key {U vtable;int action,phase;};
U base{},owner{},sim{},data{},controller{},target{},context{};
U actor_holder{};
std::array<unsigned char,8> first_actor{},second_actor{};
std::array<void*,7> detours{};
unsigned deactivations{},replacements{};U held_at_transition{};bool replace_in_loop{},deactivate_in_loop{};
U replace_call{},unknown_replace_call{},deactivate_call{},deactivate_state_call{};std::array<double,3> replacement_pose{1,2,3};
bool throw_deactivation{},deactivate_in_action{},competing_keypad_hold{};
unsigned create_count{},remove_count{},fail_create{},action_count{},phase0_count{},loop_count{},dtor_count{};
std::uint32_t last_dtor_flags{};
bool consume_fifo=true,native_accept=true,reset_in_action{},publish_open_in_action{},physical_during_release{},throw_key_push{},throw_release_push{},reset_in_dtor{};
bool wrong_dispatch{},wrong_action{};
bool reset_before_submit{};
bool change_actor_before_action{},change_actor_in_action{},change_actor_in_getter{},change_actor_in_release{};
std::deque<Key*> key_queue;
std::vector<NativeButtonRequestLayout*> requests;
std::unordered_map<void*,std::size_t> allocations;
std::unordered_map<void*,std::size_t> buffers;
std::vector<unsigned> release_skip;
unsigned object_frees{},buffer_frees{};
HandInteractionBridge bridge;
std::uint64_t frame_id{};
const Matrix4d identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
const HandCalibration calibration{Pose{{},{0,1,0}},4};
U known_loop{},known_short{},known_dispatch{},unknown_dispatch{},known_action{},unknown_action{},unknown_loop{};
std::vector<RUNTIME_FUNCTION> functions;
XrRuntimeFrame frame(float gap=.05f,bool focused=true) {
    XrRuntimeFrame f;f.id=++frame_id;f.predicted_display_time_ns=1000000000000LL+static_cast<std::int64_t>(frame_id);
    f.running=f.should_render=true;f.focused=focused;f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
    f.space_revision=4;auto& h=f.hands[1];h.active=true;
    for(auto i:{std::size_t(7),xr_index_tip,xr_thumb_tip})h.joints[i].position_valid=h.joints[i].position_tracked=true;
    h.joints[7].pose.position={.2f,1,-.3f};h.joints[xr_index_tip].pose.position={.2f,1,-.4f};
    h.joints[xr_thumb_tip].pose.position={.2f+gap,1,-.4f};return f;
}
void publish(float gap=.05f,bool focused=true){bridge.publish(frame(gap,focused),calibration,identity);}
void selection(bool nested=false,bool used=true,bool completed=true) {
    NativeHandSelection s;s.controller_live=reinterpret_cast<void*>(controller);s.a2=context;s.a3=owner+0xce600;
    s.a4=data;s.a6_live=reinterpret_cast<const int*>(owner+0x358);s.sample=bridge.inspect();s.target_token=target;
    s.used_hand_ray=used;s.completed=completed;s.nested_selection=nested;
    const auto cb=native_hand_buttons_callbacks();require(cb.selection_complete!=nullptr,"missing callback");cb.selection_complete(cb.context,s);
}
void tick(bool short_path=false,bool known=true) {
    if(short_path) {
        int token=194;reinterpret_cast<ShortLoop>(known_short)(reinterpret_cast<void*>(owner),0x11223344,data,context,&token);
        require(token==195,"short loop fifth argument lost");
    }else reinterpret_cast<Loop>(known?known_loop:unknown_loop)(reinterpret_cast<void*>(owner),0x11223344,data,context);
}
void physical(int phase) {
    reinterpret_cast<Action>(known_action)(reinterpret_cast<void*>(controller),0x9988,context,owner+0xce5d0,owner+0xce600,20,phase);
}
void* native_allocate(std::size_t bytes) {
    auto* p=::operator new(bytes);require(allocations.emplace(p,bytes).second,"duplicate allocation");return p;
}
void native_reserve(NativeButtonString* s,std::uint32_t bytes) {
    require(!s->data,"reserve only empty fixture strings");auto* p=new char[bytes];buffers.emplace(p,bytes);
    s->data=reinterpret_cast<U>(p);s->capacity=bytes;
}
void native_free(void* p) {
    require(buffers.erase(p)==1,"double/foreign string free");++buffer_frees;delete[] static_cast<char*>(p);
}
void native_delete(void* p,std::size_t bytes) {
    const auto it=allocations.find(p);require(it!=allocations.end()&&it->second==bytes,"wrong sized delete");
    allocations.erase(it);++object_frees;::operator delete(p);
}
NativeButtonString* native_getter(void* button,NativeButtonString* out,const float* hit) {
    require(reinterpret_cast<U>(button)==target&&hit[0]==.25f,"release did not capture accepted hit");
    native_reserve(out,4);std::memcpy(reinterpret_cast<void*>(out->data),"abc",4);out->length=3;
    if(change_actor_in_getter){change_actor_in_getter=false;actor_holder=reinterpret_cast<U>(second_actor.data());}
    return out;
}
void native_action(void* c,std::uint64_t a2,std::uint64_t ctx,std::uint64_t a4,std::uint64_t a5,int id,int phase) {
    require(reinterpret_cast<U>(c)==controller&&ctx==context&&a4==owner+0xce5d0&&a5==owner+0xce600,"Action ABI changed");
    require(a2==0x9988,"Action opaque argument changed");++action_count;if(phase==0)++phase0_count;
    if(id!=20)return;
    if(phase==1) {
        put(controller,0x4bf,std::uint8_t{1});
        if(native_accept){const auto type=get<std::uint32_t>(get<U>(target,0x58),0xda4);
            // 78C7A0 stores the hand hold BEFORE branching to keypad Show.
            put(controller,0x558,target);put(controller,0x568,std::array<float,3>{.25f,.5f,.75f});
            if(type==5||type==6){put(controller,0x518,target);
                if(competing_keypad_hold){put(controller,0x560,target);put(controller,0x4c0,std::uint8_t{1});}}}
        if(change_actor_in_action){change_actor_in_action=false;actor_holder=reinterpret_cast<U>(second_actor.data());}
        if(deactivate_in_action){deactivate_in_action=false;reinterpret_cast<Deactivate>(deactivate_call)(reinterpret_cast<void*>(sim),0x2233,reinterpret_cast<void*>(data),reinterpret_cast<const int*>(owner+0x358));}
        if(reset_in_action){reset_in_action=false;bridge.reset();reset_native_hand_buttons();}
        if(publish_open_in_action){publish_open_in_action=false;publish();}
    }else {put(controller,0x558,U{});put(controller,0x4bf,std::uint8_t{});}
}
void native_dispatch(void* c,std::uint64_t d,std::uint64_t a3,std::uint64_t ctx,std::uint64_t a5,std::uint64_t a6,void* event) {
    require(reinterpret_cast<U>(c)==controller&&d==data&&a3==0x9988&&ctx==context&&a5==owner+0xce5d0&&a6==owner+0xce600,"dispatch ABI changed");
    const auto& key=*static_cast<Key*>(event);
    if(key.action==999){put(controller,0x49f,std::uint8_t{1});return;}
    if(change_actor_before_action){change_actor_before_action=false;actor_holder=reinterpret_cast<U>(second_actor.data());}
    reinterpret_cast<Action>(wrong_action?unknown_action:known_action)(c,a3,ctx,a5,a6,key.action,key.phase);
}
void* native_dtor(void* event,std::uint32_t flags) {
    ++dtor_count;last_dtor_flags=flags;
    if(reset_in_dtor) {
        reset_in_dtor=false;reset_native_hand_buttons();
        require(native_hand_buttons_stats().pending_events>0,"reset falsely reported idle during native dtor");
    }
    if(flags&1)native_delete(event,sizeof(Key));return event;
}
void drain() {
    if(!consume_fifo)return;
    while(!key_queue.empty()) {
        auto* event=key_queue.front();key_queue.pop_front();
        reinterpret_cast<Dispatch>(wrong_dispatch?unknown_dispatch:known_dispatch)(reinterpret_cast<void*>(controller),data,0x9988,context,owner+0xce5d0,owner+0xce600,event);
        const auto result=reinterpret_cast<Dtor>(detours[3])(event,1);
        require(result==event&&last_dtor_flags==1,"dtor did not preserve flags or RAX");
    }
}
void native_loop(void* o,std::uint64_t a2,std::uint64_t d,std::uint64_t ctx) {
    require(reinterpret_cast<U>(o)==owner&&a2==0x11223344&&d==data&&ctx==context,"logic loop ABI changed");++loop_count;
    if(replace_in_loop){replace_in_loop=false;require(reinterpret_cast<Replace>(replace_call)(reinterpret_cast<void*>(get<U>(controller,0)),0x3344,reinterpret_cast<void*>(context),7,replacement_pose.data(),0x9988)==0x12345678,"replacement RAX changed");}
    if(deactivate_in_loop){deactivate_in_loop=false;reinterpret_cast<Deactivate>(deactivate_call)(reinterpret_cast<void*>(sim),0x2233,reinterpret_cast<void*>(data),reinterpret_cast<const int*>(owner+0x358));}
    drain();
}
void native_deactivate(void* simulation,std::uint64_t opaque,void* app_data,const int* state) {
    require(opaque==0x2233 && reinterpret_cast<U>(app_data)==data && reinterpret_cast<U>(state)==owner+0x358,"deactivate ABI changed");
    if(reinterpret_cast<U>(simulation)!=sim){++deactivations;return;}
    ++deactivations;held_at_transition=get<U>(controller,0x558);put(controller,0x558,U{});actor_holder=0;
    if(!held_at_transition)require(native_hand_buttons_stats().pending_events>0,"native lifecycle callback falsely reported idle before returning");
    if(throw_deactivation)throw std::runtime_error("original deactivate exception");
}
U native_replace(void* world,std::uint64_t opaque,void* client,std::uint16_t player,const void* pose,std::uint64_t connection) {
    require(reinterpret_cast<U>(world)==get<U>(controller,0) && opaque==0x3344 && reinterpret_cast<U>(client)==context &&
        pose==replacement_pose.data() && connection==0x9988,"replacement ABI lost a register/stack argument");
    ++replacements;held_at_transition=get<U>(controller,0x558);
    if(player==7 && !held_at_transition)require(native_hand_buttons_stats().pending_events>0,"replacement callback falsely reported idle before returning");
    if(player==7)actor_holder=reinterpret_cast<U>(second_actor.data());return 0x12345678;
}
void native_short(void* o,std::uint64_t a2,std::uint64_t d,std::uint64_t ctx,int* a5) {
    require(a5&&*a5==194,"short loop fifth argument not forwarded");++*a5;native_loop(o,a2,d,ctx);
}
void native_push(void* queue,void* object) {
    if(reinterpret_cast<U>(queue)==owner+0xce6b8) {
        key_queue.push_back(static_cast<Key*>(object));
        if(throw_key_push)throw std::runtime_error("unknown key handoff");
    }else {
        require(reinterpret_cast<U>(queue)==context+0x340,"release destination is not live request queue");
        auto* r=static_cast<NativeButtonRequestLayout*>(object);
        require(r->type==10&&r->pressed==0&&r->vehicle_id==123&&r->section_index==0&&r->payload.length==3,"release layout changed");
        require(std::memcmp(reinterpret_cast<void*>(r->payload.data),"abc",4)==0,"release payload changed");
        requests.push_back(r);release_skip.push_back(r->skip_on_button_press);
        if(change_actor_in_release){change_actor_in_release=false;actor_holder=reinterpret_cast<U>(second_actor.data());}
        if(physical_during_release){physical_during_release=false;physical(1);}
        if(throw_release_push)throw std::runtime_error("unknown request handoff");
    }
}
void consume_requests() {
    for(auto* r:requests){if(r->payload.data)native_free(reinterpret_cast<void*>(r->payload.data));native_delete(r,sizeof(*r));}
    requests.clear();
}
void patch(U rva,std::initializer_list<unsigned char> bytes){std::memcpy(reinterpret_cast<void*>(base+rva),bytes.begin(),bytes.size());}
void jump(U rva,void* function) {
    patch(rva,{0xff,0x25,0,0,0,0});put(base+rva,6,reinterpret_cast<U>(function));
}
void init_image() {
    patch(0x7aca70,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x41,0x80,0xb8,0x33});
    patch(0x5f2950,{0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xe9,0x48,0x81,0xec});
    patch(0x852d01,{0xe8});put(base+0x852d01,1,std::int32_t(0x5f2950-0x852d06));put(base+0xb1a988,0x20,base+0x7aca70);
    patch(0x84c6ee,{0xff,0x50,0x20});patch(0x84dfb1,{0xff,0x50,0x20});
    patch(0x86a150,{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xb8,0xfd,0xff,0xff,0x48,0x81});
    patch(0x86cc30,{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec});
    patch(0x790850,{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xb8,0xfc,0xff,0xff,0x48,0x81});
    patch(0x695e70,{0x48,0x89,0x4c,0x24,0x08,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8d,0x05,0x4c,0xc2,0x50,0x00,0x48,0x89,0x01,0xf6});
    patch(0x78c7a0,{0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x4c,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41});
    patch(0x95c6f8,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xeb,0x0f,0x48,0x8b,0xcb,0xe8,0xb1,0xc8,0x05,0x00,0x85,0xc0,0x74,0x13,0x48});
    patch(0x17ca10,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xbb,0x20,0,0,0,0x48,0x8b,0xf9,0x3b,0xd3,0x76,0x06,0x03,0xdb});
    patch(0x9b6750,{0x48,0x83,0xec,0x28,0x48,0x85,0xc9,0x74,0x0d,0x48,0x83,0xe1,0xf8,0x48,0x8b,0x49,0xf8,0xe8,0xce,0x3d,0xff,0xff,0x48,0x83});
    patch(0x95c2ec,{0xe9,0x8f,0xe4,0x7f,0xff});
    patch(0x236ed0,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x30,0x4c,0x8b,0xf2,0x48,0x8b,0xf1});
    patch(0x231bd0,{0x4c,0x8b,0xc2,0x4c,0x8b,0xc9,0x8b,0x52,0x10,0x41,0x80,0x78,0x14,0,0x75,0x11,0x83,0xfa,0x04,0x72,0x2b,0x49,0x8b,0x40});
    patch(0x32b470,{0x48,0x89,0x54,0x24,0x10,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0xda,0xc7,0x44,0x24,0x20,0,0,0,0,0x48,0x8b,0x01});
    patch(0x37c4d0,{0xb8,0x17,0,0,0,0xc3});put(base+0xbb2110,0,base+0x695e70);put(base+0xbb2110,8,base+0x37c4d0);
    put(base+0xb0fec0,0,base+0x231bd0);put(base+0xb75658,0x2a8,base+0x32b470);
}
U thunk(DWORD return_rva,void* destination,unsigned arguments) {
    std::vector<unsigned char> code{0x48,0x83,0xec,0x48};
    for(unsigned i=0;i<arguments-4;++i) {
        code.insert(code.end(),{0x48,0x8b,0x84,0x24});const std::uint32_t offset=0x70+8*i;
        const auto old=code.size();code.resize(old+4);std::memcpy(code.data()+old,&offset,4);
        code.insert(code.end(),{0x48,0x89,0x44,0x24,static_cast<unsigned char>(0x20+8*i)});
    }
    code.insert(code.end(),{0x48,0xb8});const auto address=reinterpret_cast<U>(destination);
    const auto old=code.size();code.resize(old+8);std::memcpy(code.data()+old,&address,8);code.insert(code.end(),{0xff,0xd0});
    const auto start=return_rva-static_cast<DWORD>(code.size());code.insert(code.end(),{0x48,0x83,0xc4,0x48,0xc3});
    std::memcpy(reinterpret_cast<void*>(base+start),code.data(),code.size());
    functions.push_back({start,start+static_cast<DWORD>(code.size()),0xbe0000});return base+start;
}
void init_thunks() {
    patch(0xbe0000,{1,4,1,0,4,0x82,0,0});
    known_loop=thunk(0x84cd83,detours[0],4);known_short=thunk(0x84cb35,detours[1],5);
    known_dispatch=thunk(0x86a76c,detours[2],7);unknown_dispatch=thunk(0x86a580,detours[2],7);
    known_action=thunk(0x7933b2,detours[4],7);unknown_action=thunk(0x793130,detours[4],7);
    unknown_loop=thunk(0x84c800,detours[0],4);
    replace_call=thunk(0x852d06,detours[6],6);unknown_replace_call=thunk(0x11286,detours[6],6);
    deactivate_call=thunk(0x84c6f1,detours[5],4);
    deactivate_state_call=thunk(0x84dfb4,detours[5],4);
    std::sort(functions.begin(),functions.end(),[](auto a,auto b){return a.BeginAddress<b.BeginAddress;});
    require(RtlAddFunctionTable(functions.data(),static_cast<DWORD>(functions.size()),base)!=0,"thunk unwind registration failed");
    jump(0x95c6f8,reinterpret_cast<void*>(native_allocate));jump(0x17ca10,reinterpret_cast<void*>(native_reserve));
    jump(0x9b6750,reinterpret_cast<void*>(native_free));jump(0x95c2ec,reinterpret_cast<void*>(native_delete));
    jump(0x236ed0,reinterpret_cast<void*>(native_push));jump(0x32b470,reinterpret_cast<void*>(native_getter));
    FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base),0xc00000);
}
void begin() {publish();selection();tick();publish(.01f);selection();}
void accepted() {begin();tick();require(native_hand_buttons_stats().own_hold,"native acceptance not acknowledged");}
void normal_tests() {
    accepted();const auto before=action_count;publish();tick();
    require(!native_hand_buttons_stats().own_hold&&!get<U>(controller,0x558)&&get<std::uint8_t>(controller,0x4bf)==0,"normal open did not release own hold");
    require(release_skip.back()==0&&action_count==before&&phase0_count==0,"normal open generated phase0 or skipped callback");consume_requests();

    // A newer valid pose in the same gesture is permitted by the final gate.
    begin();publish(.01f);tick(true);require(native_hand_buttons_stats().own_hold,"newer same-gesture pose rejected");
    publish(.01f,false);tick();require(!native_hand_buttons_stats().own_hold&&release_skip.back()==1,"focus loss did not cancel");consume_requests();

    accepted();const auto physical_before=action_count;physical(1);
    require(action_count==physical_before+1&&!native_hand_buttons_stats().own_hold&&get<U>(controller,0x558)==target&&get<std::uint8_t>(controller,0x4bf)==1,"physical press was filtered or foreign hold cleared");
    require(release_skip.back()==1,"physical takeover did not use cancellation release");consume_requests();physical(0);

    // Native actor/health/distance rejection leaves no hold and no stuck own flag.
    native_accept=false;begin();tick();require(!native_hand_buttons_stats().own_hold&&get<std::uint8_t>(controller,0x4bf)==0,"rejected native press left stuck flag");native_accept=true;

    // Pre-existing mode event precedes the appended hand event in the same FIFO.
    begin();auto* mode=static_cast<Key*>(native_allocate(sizeof(Key)));*mode={0,999,1};key_queue.push_back(mode);
    const auto action_before=action_count;tick();require(action_count==action_before&&!native_hand_buttons_stats().own_hold,"FIFO pause race bypassed final gate");put(controller,0x49f,std::uint8_t{});

    begin();put(get<U>(owner,0xce150),0x58,1);tick();require(!native_hand_buttons_stats().own_hold,"pause pane race accepted press");put(get<U>(owner,0xce150),0x58,0);
    begin();Sleep(110);tick();require(!native_hand_buttons_stats().own_hold,"expired certificate accepted");

    begin();consume_fifo=false;tick();require(!key_queue.empty()&&native_hand_buttons_stats().pending_events>0,"event not queued");
    // Old open and a new closed pose must never turn the old event into a new press.
    publish();publish(.01f);consume_fifo=true;tick();require(!native_hand_buttons_stats().own_hold,"late release merged into new gesture");

    begin();wrong_dispatch=true;tick();wrong_dispatch=false;require(!native_hand_buttons_stats().own_hold,"unknown dispatcher caller accepted own event");
    begin();wrong_action=true;tick();wrong_action=false;require(!native_hand_buttons_stats().own_hold,"unknown action caller accepted own event");
    begin();tick(false,false);require(key_queue.empty()&&!native_hand_buttons_stats().own_hold,"unknown loop enqueued event");tick();publish();tick();consume_requests();

    begin();consume_fifo=false;tick();bridge.reset();reset_native_hand_buttons();
    require(native_hand_buttons_stats().pending_events>0,"reset forgot submitted native event");
    auto* event=key_queue.front();key_queue.pop_front();reset_in_dtor=true;
    require(reinterpret_cast<Dtor>(detours[3])(event,1)==event,"event teardown return changed");consume_fifo=true;
    require(native_hand_buttons_stats().pending_events==0,"destroyed event remained pending");

    begin();reset_in_action=true;tick();
    require(!native_hand_buttons_stats().own_hold&&release_skip.back()==1&&!get<U>(controller,0x558),"reset during accepted Action lost cleanup ownership");consume_requests();
    begin();publish_open_in_action=true;tick();require(!native_hand_buttons_stats().own_hold&&release_skip.back()==0,"open during Action did not clean accepted hold");consume_requests();

    begin();selection(true);tick();require(!native_hand_buttons_stats().own_hold,"nested selection certified press");
    publish();selection();tick();publish(.01f);selection(false,false);tick();require(!native_hand_buttons_stats().own_hold,"native fallback certified hand press");
    require(!native_hand_buttons_stats().blocked&&phase0_count==1,"normal gate suppression became fatal or synthesized phase0");
    require(allocations.empty()&&buffers.empty(),"fixture allocation leaked on resolved paths");
    // Toggle buttons (button_type 1) use the same native type-10 press/release
    // request as Push; text/number variants and unknown types stay unsupported.
    const auto definition=get<U>(target,0x58);
    for(std::uint32_t type:{1u,4u}) {
        put(definition,0xda4,type);accepted();publish();tick();
        require(!native_hand_buttons_stats().own_hold&&!get<U>(controller,0x558),"toggle/lever press/release did not complete");consume_requests();
    }
    for(std::uint32_t type:{2u,3u,5u,6u,99u}) {
        put(definition,0xda4,type);begin();tick();
        require(!native_hand_buttons_stats().own_hold&&!get<U>(controller,0x558),"unsupported button type accepted");publish();tick();consume_requests();
    }
    enable_native_keypad_opening(true);
    for(std::uint32_t type:{5u,6u}){
        put(definition,0xda4,type);const auto accepted_before=native_hand_buttons_stats().presses_accepted;begin();tick();
        require(native_hand_buttons_stats().presses_accepted==accepted_before+1&&!native_hand_buttons_stats().own_hold&&get<U>(controller,0x518)==target&&get<std::uint8_t>(controller,0x4bf)==0,"keypad opening created a button hold or retained native down flag");
        publish();tick();require(requests.empty(),"keypad opening emitted type10 release");put(controller,0x518,U{});
    }
    enable_native_keypad_opening(false);
    put(definition,0xda4,std::uint32_t{0});
    require(allocations.empty()&&buffers.empty(),"button type cases leaked");
    // Targeted button types are counted once per newly selected target (diagnostic
    // that reveals e.g. a throttle lever's native type without pressing it).
    {
        const auto before=native_hand_buttons_stats().targeted_types;
        put(definition,0xda4,std::uint32_t{3});publish();selection();selection();
        auto after=native_hand_buttons_stats().targeted_types;
        require(after[3]==before[3]+1,"targeted button type not counted once per target");
        put(definition,0xda4,std::uint32_t{0});
    }
}
void isolated_test(const std::string& name) {
    if(name=="target_feedback") {
        // The ray colour reports the game's own completed selection through
        // the real press gate, before any click gesture is made.
        const auto feedback=[]{return native_target_feedback(bridge.inspect());};
        const auto reason=[&]{return std::string(feedback().reason);};
        publish();require(feedback().state==TargetFeedbackState::unknown,"target confirmed before any native selection");
        selection();require(feedback().state==TargetFeedbackState::ready&&std::string(target_feedback_hint(feedback()))=="CEL GRY - PRZYCISK GOTOWY","supported button not ready before the click gesture");
        require(!native_hand_buttons_stats().own_hold&&action_count==0,"reading the target feedback acted on the game");
        // Gaze, nested and unfinished selections never certify the hand ray.
        publish();selection(false,false);require(feedback().state==TargetFeedbackState::unknown,"gaze selection coloured the hand ray");
        publish();selection();publish();selection(true);require(feedback().state==TargetFeedbackState::unknown,"nested selection kept the previous confirmation");
        publish();selection(false,true,false);require(feedback().state==TargetFeedbackState::unknown,"unfinished selection coloured the hand ray");
        // Pointing at air, or at a component that is not a button, blocks nothing.
        {const auto button_target=target;target=0;publish();selection();require(feedback().state==TargetFeedbackState::unknown,"empty ray reported as a blocked click");
         target=sim;publish();selection();require(feedback().state==TargetFeedbackState::unknown,"non-button component reported as a blocked click");target=button_target;}
        const auto definition=get<U>(target,0x58);
        put(definition,0xda4,std::uint32_t{2});publish();selection();
        require(feedback().state==TargetFeedbackState::blocked&&reason()=="unsupported_button_type"&&std::string(target_feedback_hint(feedback()))=="TEN TYP ELEMENTU NIE JEST OBSLUGIWANY","unsupported element shown as ready");
        put(definition,0xda4,std::uint32_t{0});put(controller,0x520,U{});publish();selection();
        require(feedback().state==TargetFeedbackState::blocked&&reason()=="target_mismatch","ray without a matching button shown as ready");
        put(controller,0x520,target);put(controller,0x4bf,std::uint8_t{1});publish();selection();
        require(feedback().state==TargetFeedbackState::blocked&&reason()=="native_hand_down","natively pressed hand shown as ready");
        put(controller,0x4bf,std::uint8_t{});publish();selection();require(feedback().state==TargetFeedbackState::ready,"target did not become ready after the block cleared");
        // The hand moved off the confirmed ray without a new selection.
        auto moved=frame();for(auto i:{std::size_t(7),xr_index_tip,xr_thumb_tip})moved.hands[1].joints[i].pose.position.x+=.05f;
        bridge.publish(moved,calibration,identity);require(feedback().state==TargetFeedbackState::unknown,"confirmation followed the hand to another place");
        publish();selection();require(feedback().state==TargetFeedbackState::ready,"fixture lost the target");
        reset_native_hand_buttons();require(feedback().state==TargetFeedbackState::unknown,"reset kept the confirmation");
        publish();selection();require(feedback().state==TargetFeedbackState::ready,"target not confirmed again after reset");
        Sleep(130);require(feedback().state==TargetFeedbackState::unknown,"confirmation outlived 100 ms");
        publish();require(feedback().state==TargetFeedbackState::unknown,"expired confirmation revived by a new hand sample");
        // Own hold: amber until the hand opens and the release is acknowledged.
        accepted();require(feedback().state==TargetFeedbackState::held&&std::string(target_feedback_hint(feedback()))=="PRZYCISK TRZYMANY - OTWORZ DLON","held button not reported");
        publish();tick();require(!native_hand_buttons_stats().own_hold&&feedback().state!=TargetFeedbackState::held,"released button still reported as held");consume_requests();
    }else if(name=="denied_press_diagnostic") {
        put(controller,0x4bf,std::uint8_t{1});begin();tick();
        const auto first=native_hand_buttons_stats();
        require(first.selection_denials==1&&std::string(first.last_selection_denial)=="native_hand_down",
            "blocked click did not report its native gate");
        for(int i=0;i<100;++i){publish(.01f);selection();tick();}
        require(native_hand_buttons_stats().selection_denials==1,"held pinch flooded the diagnostic log");
        put(controller,0x4bf,std::uint8_t{});accepted();publish();tick();consume_requests();
    }else if(name=="keypad_then_button") {
        enable_native_keypad_opening(true);
        const auto definition=get<U>(target,0x58);
        for(std::uint32_t type:{5u,6u}) {
            put(definition,0xda4,type);begin();tick();
            require(get<U>(controller,0x518)==target,"keypad did not open");
            bridge.reset();reset_native_hand_buttons();
            // Frontend OK/Cancel queues Clear24. Its native handler at 79337E
            // clears only C+518; it does not release C+558/560 or C+4BF/4C0.
            put(controller,0x518,U{});
            put(definition,0xda4,std::uint32_t{0});
            const auto before=native_hand_buttons_stats().presses_accepted;
            begin();tick();
            require(native_hand_buttons_stats().presses_accepted==before+1&&native_hand_buttons_stats().own_hold,
                "vehicle button stayed blocked after keypad OK/Clear24");
            publish();tick();consume_requests();
        }
    }else if(name=="keypad_actor_changed" || name=="keypad_competing_hold" || name=="keypad_reset") {
        enable_native_keypad_opening(true);put(get<U>(target,0x58),0xda4,std::uint32_t{5});
        change_actor_in_action=name=="keypad_actor_changed";
        competing_keypad_hold=name=="keypad_competing_hold";reset_in_action=name=="keypad_reset";
        begin();tick();const auto state=native_hand_buttons_stats();
        require(requests.empty()&&get<U>(controller,0x518)==target,"keypad cleanup sent type10 or removed the session");
        if(name=="keypad_reset") {
            require(!state.blocked&&!state.own_hold&&state.keypad_hand_releases==1&&
                !get<U>(controller,0x558)&&!get<std::uint8_t>(controller,0x4bf),"reset lost the attributable keypad opening state");
        }else {
            require(state.blocked&&state.keypad_hand_releases==0&&std::string(state.last_fault)=="keypad_hand_ownership_changed"&&
                get<U>(controller,0x558)==target&&get<std::uint8_t>(controller,0x4bf)==1,
                "keypad cleanup cleared a changed character or competing hand's state");
            if(competing_keypad_hold)require(get<U>(controller,0x560)==target&&get<std::uint8_t>(controller,0x4c0)==1,"competing Q was cleared");
            tick();require(requests.empty(),"uncertain keypad cleanup retried as type10");
        }
    }else if(name=="deactivate_hold" || name=="deactivate_state_hold" || name=="replace_local_hold") {
        accepted();
        if(name!="replace_local_hold")reinterpret_cast<Deactivate>(name=="deactivate_state_hold"?deactivate_state_call:deactivate_call)(reinterpret_cast<void*>(sim),0x2233,reinterpret_cast<void*>(data),reinterpret_cast<const int*>(owner+0x358));
        else {replace_in_loop=true;tick();}
        const auto stats=native_hand_buttons_stats();
        require(!stats.own_hold && !stats.blocked && !stats.faults && requests.size()==1 && release_skip.back()==1 && held_at_transition==0,
            "native transition did not release the owned hold before the old context changed");
        require(!get<std::uint8_t>(controller,0x4bf) && !phase0_count,"native transition used phase0 or left its down flag");consume_requests();
        actor_holder=reinterpret_cast<U>(second_actor.data());accepted();publish();tick();consume_requests();
    }else if(name=="deactivate_pending") {
        begin();consume_fifo=false;tick();reinterpret_cast<Deactivate>(deactivate_call)(reinterpret_cast<void*>(sim),0x2233,reinterpret_cast<void*>(data),reinterpret_cast<const int*>(owner+0x358));
        require(native_hand_buttons_stats().pending_events>0 && key_queue.size()==1 && requests.empty(),"deactivate forgot or executed a queued press");
        actor_holder=reinterpret_cast<U>(second_actor.data());consume_fifo=true;tick();
        require(!native_hand_buttons_stats().pending_events && !native_hand_buttons_stats().presses_accepted && !native_hand_buttons_stats().blocked && allocations.empty(),
            "deactivate cancelled event escaped its native destruction path");
    }else if(name=="replace_remote" || name=="replace_unknown_caller" || name=="replace_without_logic") {
        accepted();const auto call=name=="replace_unknown_caller"?unknown_replace_call:replace_call;
        require(reinterpret_cast<Replace>(call)(reinterpret_cast<void*>(get<U>(controller,0)),0x3344,reinterpret_cast<void*>(context),
            name=="replace_remote"?9:7,replacement_pose.data(),0x9988)==0x12345678,"passthrough replacement RAX changed");
        require(requests.empty() && replacements==1 && held_at_transition==target,"unproven/remote replacement emitted a guessed release");
        if(name=="replace_remote"){publish();tick();require(requests.size()==1 && !native_hand_buttons_stats().blocked,"remote replacement cancelled local interaction");consume_requests();}
        else {tick();require(native_hand_buttons_stats().blocked && native_hand_buttons_stats().own_hold,"unproven local transition reported false cleanup");}
    }else if(name=="deactivate_foreign") {
        accepted();std::array<unsigned char,0x40> other{};
        reinterpret_cast<Deactivate>(deactivate_call)(other.data(),0x2233,reinterpret_cast<void*>(data),reinterpret_cast<const int*>(owner+0x358));
        require(deactivations==1 && native_hand_buttons_stats().own_hold && requests.empty() && !native_hand_buttons_stats().blocked,"foreign deactivation stole local hold");
        publish();tick();consume_requests();
    }else if(name=="deactivate_unknown") {
        accepted();reinterpret_cast<Deactivate>(detours[5])(reinterpret_cast<void*>(sim),0x2233,reinterpret_cast<void*>(data),reinterpret_cast<const int*>(owner+0x358));
        require(deactivations==1 && requests.empty() && native_hand_buttons_stats().own_hold,"unknown deactivation caller emitted a guessed release");
        tick();require(native_hand_buttons_stats().blocked,"unknown deactivation reported false cleanup");
    }else if(name=="deactivate_exception") {
        accepted();throw_deactivation=true;bool caught{};
        try{reinterpret_cast<Deactivate>(deactivate_call)(reinterpret_cast<void*>(sim),0x2233,reinterpret_cast<void*>(data),reinterpret_cast<const int*>(owner+0x358));}
        catch(const std::runtime_error& e){caught=std::string(e.what())=="original deactivate exception";}
        require(caught && !native_hand_buttons_stats().own_hold && native_hand_buttons_stats().blocked && requests.size()==1 &&
            std::string(native_hand_buttons_stats().last_fault)=="native_deactivation_exception","deactivate changed an original exception or lost its handed-off release");consume_requests();
    }else if(name=="deactivate_in_action") {
        begin();deactivate_in_action=true;tick();
        require(!deactivate_in_action && native_hand_buttons_stats().blocked && requests.empty() &&
            std::string(native_hand_buttons_stats().last_fault)=="lifecycle_operation_in_flight" && get<std::uint8_t>(controller,0x4bf)==1,
            "deactivation inside an unfinished own action guessed release or cleared a new context's state");
    }else if(name=="external_same_target") {
        accepted();physical_during_release=true;publish();tick();
        require(native_hand_buttons_stats().blocked&&get<U>(controller,0x558)==target&&get<std::uint8_t>(controller,0x4bf)==1,"in-flight physical same-target hold was cleared");
        const auto count=release_skip.size();tick();require(release_skip.size()==count,"ambiguous release retried");consume_requests();
    }else if(name=="uncertain_request") {
        accepted();throw_release_push=true;publish();tick();
        require(native_hand_buttons_stats().blocked&&native_hand_buttons_stats().own_hold&&requests.size()==1,"request uncertainty was not retained");
        const auto count=object_frees;tick();require(requests.size()==1&&object_frees==count,"unknown request retried or freed");consume_requests();
    }else if(name=="uncertain_key") {
        begin();consume_fifo=false;throw_key_push=true;tick();
        require(native_hand_buttons_stats().blocked&&native_hand_buttons_stats().pending_events>0&&key_queue.size()==1,"key handoff uncertainty lost ownership");
        const auto frees=object_frees;tick();require(key_queue.size()==1&&object_frees==frees,"unknown key retried/freed");
        auto* e=key_queue.front();key_queue.pop_front();reinterpret_cast<Dtor>(detours[3])(e,1);
    }else if(name=="lost_context") {
        accepted();put(data,0,U{});publish();tick();require(native_hand_buttons_stats().blocked&&native_hand_buttons_stats().own_hold&&requests.empty(),"invalid live context dereferenced cached hold");
    }else if(name=="reset_before_submit") {
        begin();const auto frees_before=object_frees;
        reset_before_submit=true;tick();
        require(!reset_before_submit && key_queue.empty(),"pre-submit reset did not run before native push");
        require(allocations.empty() && object_frees==frees_before+1,"pre-submit reset leaked an event that never reached native push");
        const auto stats=native_hand_buttons_stats();
        require(!stats.pending_events && !stats.own_hold && !stats.blocked && !stats.faults && !stats.events_queued,
            "pre-submit cancellation retained ownership or blocked the backend");
        accepted();publish();tick();consume_requests();
        require(allocations.empty() && buffers.empty(),"backend did not recover after pre-submit cancellation");
    }else if(name=="world_changed_hold" || name=="character_changed_hold" || name=="character_missing_hold") {
        accepted();const auto generation=native_hand_buttons_stats().scene_generation;
        std::vector<unsigned char> next_world(0xbba00);
        if(name=="world_changed_hold") {
            put(reinterpret_cast<U>(next_world.data()),0xbb9f8,get<U>(get<U>(controller,0),0xbb9f8));
            put(controller,0,reinterpret_cast<U>(next_world.data()));
        }else actor_holder=name=="character_missing_hold"?0:reinterpret_cast<U>(second_actor.data());
        tick();const auto stats=native_hand_buttons_stats();
        require(stats.blocked && stats.own_hold && stats.scene_generation>generation && requests.empty(),
            "world/character change did not preserve an explicitly unresolved hold");
        require(std::string(stats.last_fault)=="scene_changed_with_hold","scene fault has no precise diagnostic reason");
        require(get<U>(controller,0x558)==target && get<std::uint8_t>(controller,0x4bf)==1,
            "scene transition cleared native state belonging to a different identity");
        publish();tick();tick();require(requests.empty() && native_hand_buttons_stats().faults==1,"old hold was retried or its first fault was lost");
    }else if(name=="world_changed_pending" || name=="character_changed_pending") {
        begin();consume_fifo=false;tick();require(key_queue.size()==1,"scene fixture did not queue own event");
        std::vector<unsigned char> next_world(0xbba00);
        if(name=="world_changed_pending") {
            put(reinterpret_cast<U>(next_world.data()),0xbb9f8,get<U>(get<U>(controller,0),0xbb9f8));
            put(controller,0,reinterpret_cast<U>(next_world.data()));
        }else actor_holder=reinterpret_cast<U>(second_actor.data());
        consume_fifo=true;tick();const auto stats=native_hand_buttons_stats();
        require(!stats.presses_accepted && !stats.pending_events && !stats.own_hold && !stats.blocked && action_count==0 && allocations.empty(),
            "old queued press executed after changing world/character");
        accepted();publish();tick();consume_requests();
        require(allocations.empty() && buffers.empty(),"new scene did not accept a fresh gesture after cancelled pending input");
    }else if(name=="context_lost_pending") {
        begin();consume_fifo=false;tick();put(data,0,U{});tick();
        put(data,0,base+0xb17cc0);consume_fifo=true;tick();
        const auto stats=native_hand_buttons_stats();
        require(!stats.presses_accepted && !stats.pending_events && !stats.blocked && allocations.empty(),
            "restoring the same context revived an event cancelled during context loss");
        accepted();publish();tick();consume_requests();
    }else if(name=="character_changed_before_action") {
        begin();change_actor_before_action=true;tick();const auto stats=native_hand_buttons_stats();
        require(!change_actor_before_action && !stats.presses_accepted && !stats.pending_events && !stats.own_hold && !stats.blocked && action_count==0,
            "character changed between dispatcher and Action but the old press still executed");
        accepted();publish();tick();consume_requests();
    }else if(name=="character_changed_in_action") {
        begin();change_actor_in_action=true;tick();const auto stats=native_hand_buttons_stats();
        require(!change_actor_in_action && stats.presses_accepted==1 && stats.own_hold && stats.blocked &&
            std::string(stats.last_fault)=="scene_changed_with_hold" && requests.empty() && get<U>(controller,0x558)==target,
            "character change inside an accepted action lost hold ownership or released into the new character");
    }else if(name=="character_changed_during_capture" || name=="character_changed_during_release") {
        accepted();change_actor_in_getter=name=="character_changed_during_capture";change_actor_in_release=!change_actor_in_getter;
        publish();tick();const auto stats=native_hand_buttons_stats();
        require(stats.blocked && stats.own_hold && requests.size()==(name=="character_changed_during_capture"?0u:1u),
            "release crossed a reentrant character transition without retaining uncertainty");
        require(get<U>(controller,0x558)==target && get<std::uint8_t>(controller,0x4bf)==1,
            "release cleared native state after character identity changed");
        require(std::string(stats.last_fault)==(name=="character_changed_during_capture"?"cleanup_identity_changed":"release_handoff_identity_changed"),
            "reentrant character change has no precise diagnostic reason");
        const auto count=requests.size();tick();require(requests.size()==count,"reentrant identity change retried release");consume_requests();
    }else if(name=="removed_target" || name=="replaced_held_target") {
        accepted();const U replacement=name=="removed_target"?0:1;
        put(controller,0x558,replacement);put(controller,0x520,U{1});
        require(VirtualFree(reinterpret_cast<void*>(target),4096,MEM_DECOMMIT)!=0,"cannot decommit retired button memory");
        tick();const auto stats=native_hand_buttons_stats();
        require(stats.blocked && stats.own_hold && requests.empty(),"lost/replaced held target was not reported while pinch remained down");
        require(std::string(stats.last_fault)=="held_target_lost","lost target has no precise diagnostic reason");
        require(get<U>(controller,0x558)==replacement && get<std::uint8_t>(controller,0x4bf)==1,
            "lost-target handling overwrote native state");
        publish();tick();tick();require(requests.empty(),"lost target caused a release retry or stale dereference");
    }else if(name=="pause_hold" || name=="ui_hold") {
        const std::vector<std::pair<U,std::size_t>> fields=name=="pause_hold"?
            std::vector<std::pair<U,std::size_t>>{{controller,0x494},{controller,0x49c},{controller,0x49f},{controller,0x4bc},{controller,0x4bd}}:
            std::vector<std::pair<U,std::size_t>>{{get<U>(owner,0xce150),0x58},{owner,0xcde80},{sim,0x25b0},{sim,0x1d00},{sim,0x25a8},{sim,0x3010},{data,0x233},{owner,0x350}};
        for(const auto [object,offset]:fields) {
            accepted();const auto actions_before=action_count;
            const auto bytes=(offset==0x58)?sizeof(int):(object==controller||offset==0x3010||offset==0x233)?1:sizeof(U);
            std::array<unsigned char,sizeof(U)> saved{};std::memcpy(saved.data(),reinterpret_cast<void*>(object+offset),bytes);
            std::memset(reinterpret_cast<void*>(object+offset),0,bytes);
            if(offset!=0x3010 && offset!=0x350)put(object,offset,std::uint8_t{1});
            tick();const auto stats=native_hand_buttons_stats();
            require(!stats.own_hold && !stats.blocked && !requests.empty() && release_skip.back()==1,
                "pause/UI failed to cancel a held button while pinch remained down");
            require(!get<U>(controller,0x558) && !get<std::uint8_t>(controller,0x4bf) && action_count==actions_before && phase0_count==0,
                "pause/UI cleanup changed foreign actions or used phase0");
            std::memcpy(reinterpret_cast<void*>(object+offset),saved.data(),bytes);consume_requests();
        }
    }else if(name=="hover_changed_hold") {
        accepted();put(controller,0x520,U{1});tick();
        require(native_hand_buttons_stats().own_hold && requests.empty(),"moving the ray incorrectly cancelled or retargeted a hold");
        publish();tick();require(!native_hand_buttons_stats().own_hold && requests.size()==1 && release_skip.back()==0,
            "release used new hover rather than the saved held target");consume_requests();
    }else if(name=="physical_during_ui") {
        accepted();put(controller,0x49f,std::uint8_t{1});const auto actions_before=action_count;
        physical(1);tick();
        require(action_count==actions_before+1 && !native_hand_buttons_stats().own_hold && !native_hand_buttons_stats().blocked &&
            get<U>(controller,0x558)==target && get<std::uint8_t>(controller,0x4bf)==1 && requests.size()==1 && release_skip.back()==1,
            "UI cleanup suppressed physical takeover or cleared its native hold");
        consume_requests();physical(0);
    }else if(name=="xr_shutdown_pending") {
        begin();consume_fifo=false;tick();require(key_queue.size()==1,"shutdown fixture did not queue own press");
        bridge.reset();reset_native_hand_buttons();reset_native_hand_buttons();
        require(native_hand_buttons_stats().pending_events>0 && allocations.size()==1 && key_queue.size()==1,
            "XR shutdown freed or forgot a submitted event before native destruction");
        auto* foreign=static_cast<Key*>(native_allocate(sizeof(Key)));*foreign={0,99,1};key_queue.push_back(foreign);
        consume_fifo=true;tick();const auto stats=native_hand_buttons_stats();
        require(!stats.pending_events && !stats.own_hold && !stats.blocked && !stats.presses_accepted && action_count==1 && allocations.empty(),
            "shutdown queued press escaped cancellation or suppressed foreign input");
    }else if(name=="xr_shutdown_hold") {
        accepted();reset_native_hand_buttons();reset_native_hand_buttons();
        require(native_hand_buttons_stats().own_hold && requests.empty(),"XR shutdown performed native cleanup outside live logic");
        tick();const auto stats=native_hand_buttons_stats();
        require(!stats.own_hold && !stats.pending_events && !stats.blocked && requests.size()==1 && release_skip.back()==1 && !phase0_count,
            "XR shutdown did not release its own hold on the next live logic scope");
        tick();require(requests.size()==1,"XR shutdown submitted a duplicate release");consume_requests();
    }else throw std::runtime_error("unknown child case");
}
void run_child(const std::wstring& name) {
    std::array<wchar_t,32768> executable{};require(GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()))>0,"test executable path missing");
    std::wstring command=L"\""+std::wstring(executable.data())+L"\" "+name;
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION process{};
    require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&process)!=0,"could not start isolated ownership case");
    CloseHandle(process.hThread);require(WaitForSingleObject(process.hProcess,30000)==WAIT_OBJECT_0,"child timed out");
    DWORD exit{};require(GetExitCodeProcess(process.hProcess,&exit)!=0,"child exit missing");CloseHandle(process.hProcess);require(exit==0,"isolated ownership case failed");
}
}
namespace swvr {
void native_hand_buttons_before_submit_for_test() {
    if(reset_before_submit){reset_before_submit=false;reset_native_hand_buttons();}
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID address,LPVOID detour,LPVOID* original) {
    ++create_count;if(fail_create&&create_count==fail_create)return MH_ERROR_MEMORY_ALLOC;
    constexpr std::array<U,7> rvas{0x86a150,0x86cc30,0x790850,0x695e70,0x78c7a0,0x7aca70,0x5f2950};
    const std::array<void*,7> originals{reinterpret_cast<void*>(native_loop),reinterpret_cast<void*>(native_short),reinterpret_cast<void*>(native_dispatch),reinterpret_cast<void*>(native_dtor),reinterpret_cast<void*>(native_action),reinterpret_cast<void*>(native_deactivate),reinterpret_cast<void*>(native_replace)};
    for(unsigned i=0;i<rvas.size();++i)if(reinterpret_cast<U>(address)==base+rvas[i]){detours[i]=detour;*original=originals[i];return MH_OK;}
    return MH_ERROR_NOT_EXECUTABLE;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID){++remove_count;return MH_OK;}
int main(int argc,char** argv) {
    try {
        base=reinterpret_cast<U>(VirtualAlloc(nullptr,0xc00000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));require(base!=0,"fake image allocation failed");init_image();
        std::vector<unsigned char> owner_mem(0xcf000),sim_mem(0x4000),data_mem(0x1000),pause(0x60),definition(0xe00),section(0x300),vehicle(0x200),world(0xbba00);
        auto* button=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);require(button!=nullptr,"button fixture allocation failed");
        std::array<unsigned char,0x98> actor_node{};actor_holder=reinterpret_cast<U>(first_actor.data());
        put(reinterpret_cast<U>(world.data()),0xbb9f8,reinterpret_cast<U>(actor_node.data()));
        put(reinterpret_cast<U>(actor_node.data()),0,int{4});put(reinterpret_cast<U>(actor_node.data()),0x90,reinterpret_cast<U>(&actor_holder));
        std::array<U,1> ring{};owner=reinterpret_cast<U>(owner_mem.data());sim=reinterpret_cast<U>(sim_mem.data());data=reinterpret_cast<U>(data_mem.data());controller=sim+0x38;context=data+0x280;target=reinterpret_cast<U>(button);
        put(owner,0,base+0xb0f4a0);put(owner,0xce190,sim);put(owner,0x350,sim);put(owner,0xce150,reinterpret_cast<U>(pause.data()));
        put(sim,0,base+0xb1a988);put(sim,0x3010,std::uint8_t{1});put(controller,0,reinterpret_cast<U>(world.data()));put(controller,0x520,target);
        put(data,0,base+0xb17cc0);put(context,0x338,std::uint16_t{7});put(reinterpret_cast<U>(pause.data()),0,base+0xb07c10);put(target,0,base+0xb75658);put(target,0x58,reinterpret_cast<U>(definition.data()));
        put(target,0x18,std::array<std::int32_t,3>{1,2,3});put(target,0x28,reinterpret_cast<U>(section.data()));put(reinterpret_cast<U>(section.data()),0x250,reinterpret_cast<U>(vehicle.data()));
        ring[0]=reinterpret_cast<U>(section.data());const auto v=reinterpret_cast<U>(vehicle.data());put(v,0,reinterpret_cast<U>(ring.data()));put(v,8,std::uint32_t{1});put(v,0x10,std::uint32_t{1});put(v,0x178,std::uint32_t{123});
        const auto trace_directory=std::filesystem::current_path()/L"artifacts";
        std::filesystem::create_directories(trace_directory);
        const auto trace=trace_directory/(L"hand-buttons-unit-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        SetEnvironmentVariableW(L"SWVR_HAND_BUTTONS",nullptr);require(install_native_hand_buttons(0,false,trace,bridge)==NativeHandButtonsStatus::disabled,"backend not default disabled");
        SetEnvironmentVariableW(L"SWVR_HAND_BUTTONS",L"1");require(install_native_hand_buttons(base,false,trace,bridge)==NativeHandButtonsStatus::unsupported_profile,"false SHA accepted");
        put(base+0xbb2110,0,U{});require(install_native_hand_buttons(base,true,trace,bridge)==NativeHandButtonsStatus::signature_mismatch,"wrong event vtable accepted");put(base+0xbb2110,0,base+0x695e70);
        if(argc>1 && std::string(argv[1]).rfind("guard_lifecycle_",0)==0) {
            const auto mode=std::string(argv[1]).substr(16);
            if(mode=="deactivate")*reinterpret_cast<unsigned char*>(base+0x7aca70)^=1;
            else if(mode=="replace")*reinterpret_cast<unsigned char*>(base+0x5f2950)^=1;
            else if(mode=="vtable")put(base+0xb1a988,0x20,U{});
            else if(mode=="call")*reinterpret_cast<unsigned char*>(base+0x852d01)^=1;
            else if(mode=="deactivate_call")*reinterpret_cast<unsigned char*>(base+0x84c6ee)^=1;
            else if(mode=="rollback6" || mode=="rollback7")fail_create=mode=="rollback6"?6:7;
            else throw std::runtime_error("unknown lifecycle guard");
            const auto result=install_native_hand_buttons(base,true,trace,bridge);
            require(result==(fail_create?NativeHandButtonsStatus::hook_failure:NativeHandButtonsStatus::signature_mismatch),"lifecycle guard accepted an unsupported hook");
            require(fail_create?(create_count==fail_create && remove_count==fail_create-1):create_count==0,"lifecycle guard did not reject/roll back before native operations");return 0;
        }
        if(argc==1) {
            fail_create=3;require(install_native_hand_buttons(base,true,trace,bridge)==NativeHandButtonsStatus::hook_failure&&remove_count==2,"failed hook install not rolled back");
            fail_create=0;create_count=0;
        }
        const auto installed_trace=std::filesystem::path(trace.wstring()+L"-installed");
        require(install_native_hand_buttons(base,true,installed_trace,bridge)==NativeHandButtonsStatus::installed,"backend installation failed");
        require(create_count==7&&action_count==0&&allocations.empty(),"lifecycle hook installation incomplete or called native helpers");init_thunks();
        require(install_native_hand_buttons(base,true,installed_trace,bridge)==NativeHandButtonsStatus::already_installed,"duplicate install replaced state");
        if(argc>1)isolated_test(argv[1]);else {
            normal_tests();run_child(L"target_feedback");run_child(L"keypad_then_button");run_child(L"denied_press_diagnostic");
            for(const auto name:{L"keypad_actor_changed",L"keypad_competing_hold",L"keypad_reset"})run_child(name);
            run_child(L"external_same_target");run_child(L"uncertain_request");run_child(L"uncertain_key");run_child(L"lost_context");run_child(L"reset_before_submit");
            const std::array lifecycle_cases{L"world_changed_hold",L"character_changed_hold",L"character_missing_hold",L"world_changed_pending",L"character_changed_pending",
                L"removed_target",L"replaced_held_target",L"pause_hold",L"ui_hold",L"hover_changed_hold",L"physical_during_ui",L"xr_shutdown_pending",L"xr_shutdown_hold",
                L"context_lost_pending",L"character_changed_before_action",L"character_changed_in_action",L"character_changed_during_capture",L"character_changed_during_release"};
            for(const auto name:lifecycle_cases)run_child(name);
            run_child(L"deactivate_hold");run_child(L"deactivate_state_hold");run_child(L"replace_local_hold");
            for(const auto name:{L"deactivate_pending",L"replace_remote",L"replace_unknown_caller",L"replace_without_logic",L"deactivate_foreign",L"deactivate_exception",L"deactivate_in_action",
                L"guard_lifecycle_deactivate",L"guard_lifecycle_replace",L"guard_lifecycle_vtable",L"guard_lifecycle_call",L"guard_lifecycle_deactivate_call",L"guard_lifecycle_rollback6",L"guard_lifecycle_rollback7",L"deactivate_unknown"})run_child(name);
            std::cout<<"Lifecycle cases passed: "<<lifecycle_cases.size()<<" (world/character, pause/UI, retired target, physical input, XR shutdown).\n";
        }
        flush_native_hand_buttons();RtlDeleteFunctionTable(functions.data());VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
        require(VirtualFree(button,0,MEM_RELEASE)!=0,"button fixture reservation cleanup failed");
        std::cout<<"native hand buttons "<<(argc>1?argv[1]:"unit")<<" passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"native hand buttons: "<<e.what()<<'\n';return 1;}
}
