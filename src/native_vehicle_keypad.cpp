#include "swvr/native_vehicle_keypad.hpp"
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <cstring>
#include <mutex>
#include <limits>
#include <cmath>
namespace swvr {
namespace {
using U=std::uintptr_t;using Clock=NativeKeypadSession::Clock;
using Tick=void(*)(U,U,U);using Event=void(*)(U,U,U,U,U,U);using Action=void(*)(U,U,U,U,U,int);
Tick original_tick{};Event original_event{};Action action{};
U base{};void*(*allocate)(std::size_t){};void(*destroy)(void*,std::size_t){};void(*push)(void*,void*){};
std::recursive_mutex mutex;
struct Scope {U owner{},graphics{},data{};Scope* previous{};};thread_local Scope* scope{};
std::optional<NativeKeypadSession> session;
struct Reply {std::uint64_t generation{};std::optional<std::array<float,2>> values;};std::optional<Reply> reply;
std::uint64_t generation{};bool enabled{},focused{},busy{};Clock::time_point heartbeat{};VehicleKeypadStats stats;
bool readable(U p,std::size_t n){if(!p||n>std::numeric_limits<U>::max()-p)return false;const auto end=p+n;
    while(p<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto prot=m.Protect&255;if(prot!=PAGE_READONLY&&prot!=PAGE_READWRITE&&prot!=PAGE_WRITECOPY&&prot!=PAGE_EXECUTE_READ&&prot!=PAGE_EXECUTE_READWRITE&&prot!=PAGE_EXECUTE_WRITECOPY)return false;
        auto next=reinterpret_cast<U>(m.BaseAddress)+m.RegionSize;if(next<=p)return false;p=next;}return true;}
template<class T>T read(U p,std::size_t offset=0){T v{};if(p&&offset<=std::numeric_limits<U>::max()-p&&readable(p+offset,sizeof(T)))std::memcpy(&v,reinterpret_cast<void*>(p+offset),sizeof(T));return v;}
bool live(const NativeKeypadSession& s,bool require_focus=true){
    if(!scope||!session||session->generation!=s.generation||scope->owner!=s.owner||read<U>(scope->owner)!=base+0xb0f4a0||read<U>(scope->data)!=base+0xb17cc0)return false;
    const auto sim=read<U>(scope->owner,0xce190),world=read<U>(sim,0x38),node=read<U>(world,0xbb9f8);
    if(read<U>(scope->owner,0x350)!=sim||read<U>(sim)!=base+0xb1a988||world!=s.world||read<int>(node)!=4||read<U>(read<U>(node,0x90))!=s.actor)return false;
    const auto ui=read<U>(sim,0x3c38);
    if(ui!=s.ui||read<U>(ui)!=base+0xb60628||!read<unsigned char>(ui,0x1c)||read<std::uint32_t>(ui,0xe0)!=s.vehicle||read<std::uint32_t>(ui,0xe4)!=s.section||read<std::array<std::int32_t,3>>(ui,0xe8)!=s.grid)return false;
    if((read<unsigned char>(ui,0x100)!=0)!=(s.button_type==6)||!s.vehicle)return false;
    const auto pause=read<U>(scope->owner,0xce150);
    if(read<U>(pause)!=base+0xb07c10||(require_focus&&read<int>(pause,0x58))||read<unsigned char>(scope->data,0x233))return false;
    const auto now=Clock::now();return !require_focus||(focused&&now>=heartbeat&&now-heartbeat<=std::chrono::milliseconds(100));
}
bool validate(void*,const NativeKeypadSession& s){return live(s);}
void* alloc(void*,std::size_t n){return allocate(n);}
void discard(void*,NativeKeypadRequest* r){destroy(r,sizeof(*r));}
void enqueue(void*,void* queue,NativeKeypadRequest* r){push(queue,r);}
void service(){
    std::lock_guard lock(mutex);if(busy||!session)return;
    if(!live(*session,false)){session.reset();reply.reset();return;}
    session->observed_at=Clock::now();session->focused=live(*session);
    if(!reply)return;
    const auto command=*reply;reply.reset();
    if(command.generation!=session->generation){++stats.rejected;return;}
    const bool submit=command.values&&live(*session);if(command.values&&!submit)++stats.rejected;
    busy=true;struct Done{~Done(){busy=false;}} done;
    const auto certificate=*session;
    if(submit){
        NativeKeypadSubmission batch(true,{nullptr,base,base+keypad_request_vtable_rva,alloc,discard,enqueue,validate});
        try {batch.prepare_values(certificate,*command.values);batch.submit(*session,reinterpret_cast<void*>(scope->data+0x5c0));++stats.submitted;}
        catch(...){++stats.faults;try{batch.discard_remaining();}catch(...){++stats.faults;}}
    }else ++stats.cancelled;
    // The original Cancel path emits Clear24 and releases native text focus.
    // It does not emit type11 requests. Never close a replacement Show session.
    if(live(certificate,false)){
        try{action(read<U>(scope->owner,0xce190),scope->graphics,scope->data,scope->data+0x280,scope->owner+0xce6b8,4);}catch(...){++stats.faults;}
        session.reset();
    }
}
void tick(U owner,U graphics,U data){
    Scope current{owner,graphics,data,scope};scope=&current;struct Restore{Scope* old;~Restore(){scope=old;}} restore{current.previous};
    original_tick(owner,graphics,data);service();
}
void event_impl(U caller,U sim,U graphics,U data,U context,U queue,U message){
    const bool known=scope&&caller==base+0x84d207&&sim==read<U>(scope->owner,0xce190)&&graphics==scope->graphics&&data==scope->data&&context==data+0x280&&queue==scope->owner+0xce6b8;
    const auto vt=known?read<U>(message):0;
    std::array<float,2> values{};unsigned type{};
    if(vt==base+0xb5f0f8){values=read<std::array<float,2>>(message,0x38);type=read<unsigned>(message,0x40);}
    original_event(sim,graphics,data,context,queue,message);
    if(!known)return;
    std::lock_guard lock(mutex);
    if(vt==base+0xb524a0){session.reset();reply.reset();return;}
    // External Update can invalidate values while the user edits. Close the VR
    // draft rather than overwriting an updated channel with a stale value.
    if(vt==base+0xafa6a8){if(session)reply=Reply{session->generation,{}};return;}
    if(vt!=base+0xb5f0f8)return;
    session.reset();reply.reset();
    const auto ui=read<U>(sim,0x3c38),world=read<U>(sim,0x38),node=read<U>(world,0xbb9f8);
    NativeKeypadSession s;s.generation=++generation;s.owner=scope->owner;s.world=world;s.actor=read<U>(read<U>(node,0x90));s.ui=ui;
    // UI+vehicle/grid and Show generation are the target certificate; no stale
    // component pointer is followed from the other (logic) thread.
    s.target=ui;s.vehicle=read<std::uint32_t>(ui,0xe0);s.section=read<std::uint32_t>(ui,0xe4);s.grid=read<std::array<std::int32_t,3>>(ui,0xe8);
    s.button_type=type;s.current=values;s.observed_at=Clock::now();s.recognized=s.visible=true;
    if((type!=5&&type!=6)||!std::isfinite(values[0])||!std::isfinite(values[1]))return;
    session=s;if(!live(s,false)){session.reset();return;}++stats.opened;
}
void event(U sim,U graphics,U data,U context,U queue,U message){event_impl(reinterpret_cast<U>(_ReturnAddress()),sim,graphics,data,context,queue,message);}
}
bool install_native_vehicle_keypad(U image,bool sha) noexcept {
    std::lock_guard lock(mutex);if(enabled||!image||!sha)return false;
    // Full SHA plus the ABI functions and original frontend call relation.
    const std::array<std::pair<U,std::array<unsigned char,4>>,5> guards{{
        {0x84d010,{0x40,0x55,0x53,0x56}}, {0x7b6060,{0x48,0x8b,0xc4,0x55}},
        {0x7b3100,{0x48,0x8b,0xc4,0x48}}, {0x95c6f8,{0x40,0x53,0x48,0x83}}, {0x236ed0,{0x48,0x89,0x5c,0x24}}}};
    for(const auto& [rva,bytes]:guards)if(!readable(image+rva,bytes.size())||std::memcmp(reinterpret_cast<void*>(image+rva),bytes.data(),bytes.size()))return false;
    if(read<U>(image+keypad_request_vtable_rva)!=image+keypad_serialize_rva||!readable(image+keypad_serialize_rva,keypad_serialize_prefix.size())||std::memcmp(reinterpret_cast<void*>(image+keypad_serialize_rva),keypad_serialize_prefix.data(),keypad_serialize_prefix.size()))return false;
    if(read<unsigned char>(image+0x84d202)!=0xe8||0x84d207+read<std::int32_t>(image+0x84d203)!=0x7b6060)return false;
    base=image;action=reinterpret_cast<Action>(base+0x7b3100);allocate=reinterpret_cast<decltype(allocate)>(base+0x95c6f8);destroy=reinterpret_cast<decltype(destroy)>(base+0x95c2ec);push=reinterpret_cast<decltype(push)>(base+0x236ed0);
    if(MH_CreateHook(reinterpret_cast<void*>(base+0x84d010),reinterpret_cast<void*>(tick),reinterpret_cast<void**>(&original_tick))!=MH_OK)return false;
    if(MH_CreateHook(reinterpret_cast<void*>(base+0x7b6060),reinterpret_cast<void*>(event),reinterpret_cast<void**>(&original_event))!=MH_OK){MH_RemoveHook(reinterpret_cast<void*>(base+0x84d010));return false;}
    enabled=true;return true;
}
std::optional<NativeKeypadSession> vehicle_keypad_poll(bool focus) noexcept {std::lock_guard lock(mutex);focused=focus;heartbeat=Clock::now();if(!enabled||!session)return {};
    if(!focus)reply=Reply{session->generation,{}};
    if(heartbeat<session->observed_at||heartbeat-session->observed_at>std::chrono::milliseconds(100))return {};return session;}
bool vehicle_keypad_reply(std::uint64_t id,std::optional<std::array<float,2>> values) noexcept {std::lock_guard lock(mutex);if(!session||reply||session->generation!=id)return false;reply=Reply{id,values};return true;}
VehicleKeypadStats vehicle_keypad_stats() noexcept {std::lock_guard lock(mutex);return stats;}
#ifdef SWVR_VEHICLE_KEYPAD_TESTING
void test_vehicle_keypad_setup(U image,Tick t,Event e,Action a,void*(*al)(std::size_t),void(*de)(void*,std::size_t),void(*pu)(void*,void*)){
    base=image;original_tick=t;original_event=e;action=a;allocate=al;destroy=de;push=pu;enabled=true;focused=busy=false;session.reset();reply.reset();stats={};generation=0;heartbeat={};
}
void test_vehicle_keypad_tick(U owner,U graphics,U data){tick(owner,graphics,data);}
void test_vehicle_keypad_event(U caller,U sim,U graphics,U data,U context,U queue,U message){event_impl(caller,sim,graphics,data,context,queue,message);}
#endif
}
