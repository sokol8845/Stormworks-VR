#include "swvr/native_locomotion.hpp"
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>
#include <cmath>
#include <algorithm>

namespace swvr {
namespace {
using U=std::uintptr_t;
constexpr U input_send_rva=0x868410,allocate_rva=0x95c6f8,push_rva=0x236ed0,serialize_rva=0x234870;
constexpr U request_vtable=0xb5ee18;
constexpr U full_input_vtable=0xb57d80,full_input_serialize_rva=0x2319a0;
constexpr std::array<unsigned char,24> full_input_serialize_prefix{
    0x48,0x83,0xec,0x28,0x4c,0x8b,0xd9,0x48,0x83,0xc1,0x10,0xe8,
    0x20,0x97,0xfe,0xff,0x83,0x7a,0x10,0x04,0x72,0x31,0x80,0x7a};
constexpr U loop_rva=0x86a150,short_loop_rva=0x86cc30,deactivate_rva=0x7aca70,replace_rva=0x5f2950,delete_rva=0x95c2ec;
constexpr std::array<unsigned char,24> loop_prefix{
    0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xb8,0xfd,0xff,0xff,0x48,0x81};
constexpr std::array<unsigned char,24> short_loop_prefix{
    0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec};
constexpr std::array<unsigned char,24> deactivate_prefix{
    0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x41,0x80,0xb8,0x33};
constexpr std::array<unsigned char,24> replace_prefix{
    0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xe9,0x48,0x81,0xec};
constexpr std::array<unsigned char,5> delete_prefix{0xe9,0x8f,0xe4,0x7f,0xff};
constexpr std::array<unsigned char,24> input_send_prefix{
    0x48,0x8b,0xc4,0x48,0x89,0x58,0x20,0x55,0x56,0x57,0x41,0x54,
    0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0x08,0xfe,0xff};
constexpr std::array<unsigned char,24> allocate_prefix{
    0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xeb,0x0f,0x48,
    0x8b,0xcb,0xe8,0xb1,0xc8,0x05,0x00,0x85,0xc0,0x74,0x13,0x48};
constexpr std::array<unsigned char,24> push_prefix{
    0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,
    0x41,0x56,0x48,0x83,0xec,0x30,0x4c,0x8b,0xf2,0x48,0x8b,0xf1};
constexpr std::array<unsigned char,24> serialize_prefix{
    0x80,0x7a,0x14,0x00,0x4c,0x8b,0xc1,0x8b,0x42,0x10,0x75,0x12,
    0x83,0xf8,0x08,0x72,0x5d,0x48,0x8b,0x42,0x08,0x48,0x8b,0x08};
using Send=void(*)(void*,void*,void*);
Send real_send{};
void* (*allocate_native)(std::size_t){};
void (*push_native)(void*,void*){};
void (*destroy_native)(void*,std::size_t){};
using Loop=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t);
using ShortLoop=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,int*);
using Deactivate=void(*)(void*,std::uint64_t,void*,const int*);
using Replace=U(*)(void*,std::uint64_t,void*,std::uint16_t,const void*,std::uint64_t);
Loop real_loop{};ShortLoop real_short_loop{};Deactivate real_deactivate{};Replace real_replace{};
U base{};
HandLocomotion* bridge{};
HandSteering* steering{};
bool steering_holding{};U steering_seat{};
HandSteering::Clock::time_point steering_observed{};
struct SteeringInputRequest {U vtable{};std::uint32_t type{1},metadata{};std::array<unsigned char,0x50> input{};};
static_assert(sizeof(SteeringInputRequest)==0x60&&offsetof(SteeringInputRequest,input)==0x10);
std::recursive_mutex mutex;
NativeLocomotionStats stats;
struct Identity {U owner{},simulation{},actor{},world{},context{};bool operator==(const Identity&)const=default;};
Identity previous;
bool failed{},input_suspended{};
bool operation{};unsigned transition_depth{};
struct LiveLoop {U owner{},context{},world{};bool known{};LiveLoop* previous{};};
thread_local LiveLoop* live_loop{};
bool readable(U p,std::size_t n) {
    if(!p||n>std::numeric_limits<U>::max()-p)return false;
    const U end=p+n;
    while(p<end) {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m))||m.State!=MEM_COMMIT||
           (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto protection=m.Protect&0xffu;
        if(protection!=PAGE_READONLY&&protection!=PAGE_READWRITE&&protection!=PAGE_WRITECOPY&&
           protection!=PAGE_EXECUTE_READ&&protection!=PAGE_EXECUTE_READWRITE&&protection!=PAGE_EXECUTE_WRITECOPY)return false;
        const auto next=reinterpret_cast<U>(m.BaseAddress)+m.RegionSize;if(next<=p)return false;p=next;
    }
    return true;
}
template<class T>T read(U p,std::size_t offset=0) {
    T out{};
    if(p&&offset<=std::numeric_limits<U>::max()-p&&readable(p+offset,sizeof(T)))std::memcpy(&out,reinterpret_cast<void*>(p+offset),sizeof(T));
    return out;
}
struct Scope {Identity identity;std::array<std::uint64_t,2> physical{};std::array<unsigned char,0x45> input{};bool live{},allowed{};std::uint64_t blocks{16};U seat{};float axis{};};
Scope resolve(U owner,U context) {
    Scope s;
    if(read<U>(owner)!=base+0xb0f4a0||context<0x280||read<U>(context-0x280)!=base+0xb17cc0)return s;
    const auto simulation=read<U>(owner,0xce190);
    if(read<U>(simulation)!=base+0xb1a988||read<U>(owner,0x350)!=simulation)return s;
    const auto world=read<U>(simulation,0x38),node=read<U>(world,0xbb9f8);
    if(!node||read<int>(node)!=4)return s;
    const auto actor=read<U>(read<U>(node,0x90));
    if(!actor||!readable(simulation+0x1a10,sizeof(s.input)))return s;
    s.input=read<decltype(s.input)>(simulation,0x1a10);
    std::memcpy(s.physical.data(),s.input.data(),sizeof(s.physical));
    s.identity={owner,simulation,actor,world,context};s.live=true;
    const auto pause=read<U>(owner,0xce150),controller=simulation+0x38;
    s.blocks=0;
    if(!readable(actor+0x2100,sizeof(U))||!readable(pause+0x58,sizeof(int))||!readable(context-0x280+0x233,1)||read<U>(pause)!=base+0xb07c10)s.blocks|=16;
    if(read<int>(pause,0x58))s.blocks|=1;
    s.seat=read<U>(actor,0x2100);std::memcpy(&s.axis,s.input.data()+0x10,sizeof(s.axis));
    if(s.seat)s.blocks|=2;
    if(read<unsigned char>(context-0x280,0x233))s.blocks|=32;
    for(auto field:{0x494u,0x49cu,0x49fu,0x4bcu,0x4bdu})if(!readable(controller+field,1)||read<unsigned char>(controller,field))s.blocks|=64;
    for(auto field:{0x25b0u,0x1d00u,0x25a8u})if(read<U>(simulation,field))s.blocks|=4;
    // Same panel/editor gates as the game's keyboard button sampler 0x947760.
    for(auto field:{0x3c50u,0x3c68u}) {
        const auto panel=read<U>(simulation,field);
        if(!panel||!readable(panel+0x24,1)||read<unsigned char>(panel,0x24))s.blocks|=4;
    }
    if(read<unsigned char>(simulation,0x3c70)||read<unsigned char>(simulation,0x3c71)||read<U>(owner,0xcde80))s.blocks|=4;
    s.allowed=s.blocks==0;
    return s;
}
void fail(const char* why) {
    if(!failed){failed=true;++stats.faults;stats.last_fault=why;}
    bridge->set_native_block("BLAD RUCHU - SPRAWDZ LOG");bridge->reset(why);
    if(steering){steering->reset();steering_seat=0;}
}
bool steering_allowed(const Scope& s){return s.live&&s.seat&&(s.blocks&~std::uint64_t{2})==0&&!(s.physical[0]&3)&&
    (!s.input[0x40]||(std::isfinite(s.axis)&&std::abs(s.axis)<.01f))&&!input_suspended&&!transition_depth;}
bool send_steering(const Scope& s,void* queue,bool release=false){
    if(!steering)return true;
    steering_observed=HandSteering::Clock::now();const auto next_seat=!release&&steering_allowed(s)?s.seat:0;
    if(next_seat!=steering_seat)steering->reset();steering_seat=next_seat;
    if(!steering_seat)steering->reset();
    auto command=steering->inspect();
    if(!command.active&&!steering_holding)return true;
    if(operation||!queue||!readable(reinterpret_cast<U>(queue),0x18)){fail("steering_live_queue");return false;}
    operation=true;struct Done{~Done(){operation=false;}} done;
    SteeringInputRequest* r{};
    try{r=static_cast<SteeringInputRequest*>(allocate_native(sizeof(SteeringInputRequest)));}catch(...){fail("steering_allocate_exception");return false;}
    if(!r){fail("steering_allocate_failed");return false;}
    const auto current=resolve(s.identity.owner,s.identity.context);
    if(failed||!current.live||current.identity!=s.identity){destroy_native(r,sizeof(*r));fail("steering_identity_changed");return false;}
    command=steering->inspect();
    const bool active=!release&&steering_allowed(current)&&current.seat==s.seat&&command.active;
    const float value=active?std::clamp(command.axis,-1.f,1.f):(std::isfinite(current.axis)?current.axis:0.f);
    *r={};r->vtable=base+full_input_vtable;
    std::copy(current.input.begin(),current.input.end(),r->input.begin());
    if(active){
        // Seat update 4E4972 ignores analog axes unless input+40 is set.
        // Send a full native snapshot after the original sender, including its
        // mode refreshes. Never modify physical input or wake dormant axes.
        if(!current.input[0x40])std::fill(r->input.begin()+0x10,r->input.begin()+0x30,0);
        std::memcpy(r->input.data()+0x10,&value,sizeof(value));r->input[0x40]=1;
    }
    try{push_native(queue,r);}catch(...){steering_holding=true;fail("steering_handoff_uncertain");return false;}
    steering_holding=active;
    ++stats.steering_packets;if(!active)++stats.steering_releases;
    if(active){if(std::abs(value)>.01f)++stats.steering_nonzero_packets;stats.steering_max_axis=std::max(stats.steering_max_axis,std::abs(value));}
    if(!active)steering->reset();
    const auto after=resolve(s.identity.owner,s.identity.context);
    if(!after.live||after.identity!=s.identity){fail("steering_identity_after_handoff");return false;}return true;
}
bool enqueue(const Scope& s,void* queue,std::uint64_t own) {
    if(operation){fail("operation_in_flight");return false;}
    operation=true;struct Done {~Done(){operation=false;}} done;
    if(!queue||!readable(reinterpret_cast<U>(queue),0x18)){fail("queue_unreadable");return false;}
    NativeMovementRequest* request{};
    try{request=static_cast<NativeMovementRequest*>(allocate_native(sizeof(NativeMovementRequest)));}
    catch(...){fail("allocation_exception");return false;}
    if(!request){fail("allocation_failed");return false;}
    const auto discard=[&]{try{destroy_native(request,sizeof(NativeMovementRequest));}catch(...){fail("discard_uncertain");}};
    const auto current=resolve(s.identity.owner,s.identity.context);
    if(failed||!current.live||current.identity!=s.identity) {
        discard();if(!failed)fail("identity_changed_during_allocation");return false;
    }
    // Allocation can reenter native code. Preserve the current physical mask
    // and recheck game gates and intent before the ownership handoff.
    if(own&&(!current.allowed||(current.physical[0]&move_mask)||locomotion_buttons(bridge->inspect())!=own)) {
        discard();bridge->reset();if(failed)return false;
        operation=false;return stats.holding?enqueue(current,queue,0):false;
    }
    *request={base+request_vtable,100,0,{current.physical[0]|own,current.physical[1]}};
    try {push_native(queue,request);}
    catch(...) {fail("handoff_uncertain");return false;}
    ++stats.packets;
    if(own&move_sprint)++stats.sprint;else if(own)++stats.walk;else ++stats.releases;
    stats.holding=own!=0;
    const auto after=resolve(s.identity.owner,s.identity.context);
    if(!after.live||after.identity!=s.identity){fail("identity_changed_during_handoff");return false;}
    return !failed;
}
bool begin_transition_callback(void*,U owner,U context) noexcept {
    bool entered{};
    try {
        std::lock_guard lock(mutex);bridge->reset("lifecycle_begin");
        if(failed)return false;
        const auto s=resolve(owner,context);
        if(!s.live||((stats.holding||steering_holding)&&s.identity!=previous)){if(stats.holding||steering_holding)fail("lifecycle_identity_mismatch");return false;}
        if(operation){fail("lifecycle_operation_in_flight");return false;}
        ++transition_depth;entered=true;
        if(!send_steering(s,reinterpret_cast<void*>(context+0x340),true)){--transition_depth;entered=false;return false;}
        if(stats.holding) {
            if(!enqueue(s,reinterpret_cast<void*>(context+0x340),0)){--transition_depth;entered=false;return false;}
            ++stats.lifecycle_releases;
        }
        return true;
    }catch(...){std::lock_guard lock(mutex);if(entered&&transition_depth)--transition_depth;fail("lifecycle_exception");return false;}
}
void end_transition_callback(void*,bool succeeded) noexcept {
    std::lock_guard lock(mutex);if(transition_depth)--transition_depth;
    previous={};bridge->reset("lifecycle_end");if(steering)steering->reset();steering_seat=0;if(!succeeded)fail("native_transition_exception");
}
void lifecycle_loop(void* owner,std::uint64_t opaque,std::uint64_t data,std::uint64_t context) {
    const auto s=resolve(reinterpret_cast<U>(owner),context);
    LiveLoop scope{reinterpret_cast<U>(owner),context,s.live?read<U>(s.identity.simulation,0x38):0,
        reinterpret_cast<U>(_ReturnAddress())==base+0x84cd83&&s.live&&data+0x280==context,live_loop};
    live_loop=&scope;struct Restore {LiveLoop* p;~Restore(){live_loop=p;}} restore{scope.previous};
    real_loop(owner,opaque,data,context);
}
void lifecycle_short_loop(void* owner,std::uint64_t opaque,std::uint64_t data,std::uint64_t context,int* extra) {
    const auto s=resolve(reinterpret_cast<U>(owner),context);
    LiveLoop scope{reinterpret_cast<U>(owner),context,s.live?read<U>(s.identity.simulation,0x38):0,
        reinterpret_cast<U>(_ReturnAddress())==base+0x84cb35&&s.live&&data+0x280==context,live_loop};
    live_loop=&scope;struct Restore {LiveLoop* p;~Restore(){live_loop=p;}} restore{scope.previous};
    real_short_loop(owner,opaque,data,context,extra);
}
void lifecycle_deactivate(void* simulation,std::uint64_t opaque,void* data,const int* state) {
    const auto caller=reinterpret_cast<U>(_ReturnAddress());bool observed{};
    if((caller==base+0x84c6f1||caller==base+0x84dfb4)&&reinterpret_cast<U>(state)>=0x358) {
        const auto owner=reinterpret_cast<U>(state)-0x358,context=reinterpret_cast<U>(data)+0x280;
        const auto s=resolve(owner,context);
        if(s.live&&s.identity.simulation==reinterpret_cast<U>(simulation))observed=begin_transition_callback(nullptr,owner,context);
    }
    try {real_deactivate(simulation,opaque,data,state);}
    catch(...){if(observed)end_transition_callback(nullptr,false);throw;}
    if(observed)end_transition_callback(nullptr,true);
}
U lifecycle_replace(void* world,std::uint64_t graphics,void* context,std::uint16_t player,const void* pose,std::uint64_t connection) {
    bool observed{};
    if(reinterpret_cast<U>(_ReturnAddress())==base+0x852d06&&live_loop&&live_loop->known&&
       live_loop->world==reinterpret_cast<U>(world)&&live_loop->context==reinterpret_cast<U>(context)&&
       read<std::uint16_t>(reinterpret_cast<U>(context),0x338)==player)
        observed=begin_transition_callback(nullptr,live_loop->owner,live_loop->context);
    U result{};
    try {result=real_replace(world,graphics,context,player,pose,connection);}
    catch(...){if(observed)end_transition_callback(nullptr,false);throw;}
    if(observed)end_transition_callback(nullptr,true);return result;
}
void send_in_scope(U caller,void* owner,void* context,void* queue) {
    // Preserve every original call, including unknown callers, without touching
    // their arguments. Only the two measured logic sites may emit requests.
    if(caller!=0x86a36b&&caller!=0x86cdeb){real_send(owner,context,queue);return;}
    const auto before=resolve(reinterpret_cast<U>(owner),reinterpret_cast<U>(context));
    real_send(owner,context,queue);
    std::lock_guard lock(mutex);
    const auto s=resolve(reinterpret_cast<U>(owner),reinterpret_cast<U>(context));
    bridge->note_native_state(s.blocks|((s.physical[0]&move_mask)?8u:0u),s.physical);
    if(!s.live||!before.live||s.identity!=before.identity){bridge->reset();if(steering)steering->reset();steering_seat=0;if(stats.holding||steering_holding)fail("live_context_changed_during_send");return;}
    if(transition_depth){bridge->reset();return;}
    if(previous!=s.identity) {
        bridge->reset();if(steering)steering->reset();previous=s.identity;
        // No late release into another actor/world. The old identity is never
        // dereferenced; the next gesture must be armed with an open hand.
        if(stats.holding||steering_holding){fail("actor_changed_with_hold");return;}
    }
    if(failed)return;
    if(!send_steering(s,queue))return;
    bridge->set_native_block(!s.allowed?"PAUZA / UI / POJAZD":(s.physical[0]&move_mask)?"STEROWANIE KLAWIATURA":nullptr);
    if(!s.allowed||(s.physical[0]&move_mask))bridge->reset();
    const auto command=bridge->inspect();
    const auto own=s.allowed?locomotion_buttons(command):0;
    if(!own&&!stats.holding)return;
    enqueue(s,queue,own);
}
void send(void* owner,void* context,void* queue) {
    send_in_scope(reinterpret_cast<U>(_ReturnAddress())-base,owner,context,queue);
}
}
bool install_native_locomotion(U image,bool sha,HandLocomotion& source,bool shared_lifecycle) noexcept {
    try {
        std::lock_guard lock(mutex);if(bridge)return false;
        if(!sha||!image)return false;
        for(const auto& guard:std::array<std::pair<U,std::array<unsigned char,24>>,4>{{
            {input_send_rva,input_send_prefix},{allocate_rva,allocate_prefix},{push_rva,push_prefix},{serialize_rva,serialize_prefix}}})
            if(!readable(image+guard.first,guard.second.size())||std::memcmp(reinterpret_cast<void*>(image+guard.first),guard.second.data(),guard.second.size()))return false;
        if(read<U>(image+request_vtable)!=image+serialize_rva)return false;
        for(const auto& guard:std::array<std::pair<U,std::array<unsigned char,24>>,4>{{
            {loop_rva,loop_prefix},{short_loop_rva,short_loop_prefix},{deactivate_rva,deactivate_prefix},{replace_rva,replace_prefix}}})
            if(!readable(image+guard.first,guard.second.size())||std::memcmp(reinterpret_cast<void*>(image+guard.first),guard.second.data(),guard.second.size()))return false;
        if(!readable(image+delete_rva,delete_prefix.size())||std::memcmp(reinterpret_cast<void*>(image+delete_rva),delete_prefix.data(),delete_prefix.size()))return false;
        if(read<U>(image+0xb1a988,0x20)!=image+deactivate_rva||read<unsigned char>(image+0x852d01)!=0xe8||
           0x852d06+read<std::int32_t>(image+0x852d02)!=static_cast<std::intptr_t>(replace_rva))return false;
        for(const auto site:{0x86a366u,0x86cde6u}) {
            if(read<unsigned char>(image+site)!=0xe8||
               static_cast<std::intptr_t>(site+5)+read<std::int32_t>(image+site+1)!=static_cast<std::intptr_t>(input_send_rva))return false;
        }
        for(const auto site:{0x84c6eeu,0x84dfb1u})if(read<unsigned char>(image+site)!=0xff||read<unsigned short>(image+site+1)!=0x2050)return false;
        struct Hook {U rva;void* detour;void** original;};
        std::vector<Hook> hooks{{input_send_rva,reinterpret_cast<void*>(send),reinterpret_cast<void**>(&real_send)}};
        if(!shared_lifecycle) {
            hooks.push_back({loop_rva,reinterpret_cast<void*>(lifecycle_loop),reinterpret_cast<void**>(&real_loop)});
            hooks.push_back({short_loop_rva,reinterpret_cast<void*>(lifecycle_short_loop),reinterpret_cast<void**>(&real_short_loop)});
            hooks.push_back({deactivate_rva,reinterpret_cast<void*>(lifecycle_deactivate),reinterpret_cast<void**>(&real_deactivate)});
            hooks.push_back({replace_rva,reinterpret_cast<void*>(lifecycle_replace),reinterpret_cast<void**>(&real_replace)});
        }
        base=image;bridge=&source;
        std::size_t created{};
        for(const auto& hook:hooks) {
            if(MH_CreateHook(reinterpret_cast<void*>(image+hook.rva),hook.detour,hook.original)!=MH_OK) {
                while(created)MH_RemoveHook(reinterpret_cast<void*>(image+hooks[--created].rva));
                failed=true;return false;
            }
            ++created;
        }
        allocate_native=reinterpret_cast<void*(*)(std::size_t)>(image+allocate_rva);
        push_native=reinterpret_cast<void(*)(void*,void*)>(image+push_rva);
        destroy_native=reinterpret_cast<void(*)(void*,std::size_t)>(image+delete_rva);
        base=image;bridge=&source;return true;
    }catch(...){return false;}
}
NativeLocomotionStats native_locomotion_stats() noexcept {std::lock_guard lock(mutex);auto result=stats;result.holding=result.holding||steering_holding;return result;}
bool native_locomotion_allows_turn() noexcept {
    std::lock_guard lock(mutex);
    if(!bridge||failed||input_suspended||operation||transition_depth||!previous.owner)return false;
    const auto current=resolve(previous.owner,previous.context);
    return current.live&&current.allowed&&current.identity==previous&&!(current.physical[0]&move_mask);
}
bool pause_native_locomotion(bool paused) noexcept {
    std::lock_guard lock(mutex);if(!bridge||failed||operation||(!paused&&(stats.holding||steering_holding)))return false;
    if(paused&&steering)steering->reset();
    bridge->suspend(paused);input_suspended=paused;return true;
}
NativeInputLifecycleCallbacks native_locomotion_lifecycle_callbacks() noexcept {return {nullptr,begin_transition_callback,end_transition_callback};}
bool enable_native_steering(HandSteering& source) noexcept {
    std::lock_guard lock(mutex);if(!bridge||!base||steering)return false;
    if(read<U>(base+full_input_vtable)!=base+full_input_serialize_rva||
       !readable(base+full_input_serialize_rva,full_input_serialize_prefix.size())||
       std::memcmp(reinterpret_cast<void*>(base+full_input_serialize_rva),full_input_serialize_prefix.data(),full_input_serialize_prefix.size()))return false;
    steering=&source;return true;
}
U native_steering_seat() noexcept {std::lock_guard lock(mutex);const auto now=HandSteering::Clock::now();return !failed&&!input_suspended&&now>=steering_observed&&now-steering_observed<=std::chrono::milliseconds(100)?steering_seat:0;}
#ifdef SWVR_LOCOMOTION_TESTING
void test_locomotion_steering_setup(HandSteering& source){steering=&source;}
void test_locomotion_setup(U image,HandLocomotion& source,Send original,void*(*allocate)(std::size_t),void(*push)(void*,void*),void(*destroy)(void*,std::size_t)) {
    base=image;bridge=&source;real_send=original;allocate_native=allocate;push_native=push;
    destroy_native=destroy;previous={};stats={};failed=false;input_suspended=false;operation=false;transition_depth=0;steering=nullptr;steering_holding=false;steering_seat=0;
}
void test_locomotion_send(U caller,void* owner,void* context,void* queue){send_in_scope(caller,owner,context,queue);}
std::array<void*,4> test_locomotion_lifecycle_setup(Loop loop,ShortLoop short_loop,Deactivate deactivate,Replace replace) {
    real_loop=loop;real_short_loop=short_loop;real_deactivate=deactivate;real_replace=replace;
    return {reinterpret_cast<void*>(lifecycle_loop),reinterpret_cast<void*>(lifecycle_short_loop),reinterpret_cast<void*>(lifecycle_deactivate),reinterpret_cast<void*>(lifecycle_replace)};
}
#endif
}
