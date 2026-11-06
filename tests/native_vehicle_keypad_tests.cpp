#include "swvr/native_vehicle_keypad.hpp"
#include <windows.h>
#include <MinHook.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace swvr;using U=std::uintptr_t;
namespace swvr {
void test_vehicle_keypad_setup(U,void(*)(U,U,U),void(*)(U,U,U,U,U,U),void(*)(U,U,U,U,U,int),void*(*)(std::size_t),void(*)(void*,std::size_t),void(*)(void*,void*));
void test_vehicle_keypad_tick(U,U,U);void test_vehicle_keypad_event(U,U,U,U,U,U,U);
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_ERROR_UNSUPPORTED_FUNCTION;}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_OK;}
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class T>void put(U p,std::size_t offset,T value){std::memcpy(reinterpret_cast<void*>(p+offset),&value,sizeof(value));}
constexpr U base=0x140000000;U owner,sim,data,world,node,holder,actor,ui,pause,message;unsigned event_kind{},closed{},allocations{},frees{},fail_allocation{},mutation{};bool throw_push{};
std::vector<NativeKeypadRequest> sent;
void original_event(U s,U graphics,U d,U context,U queue,U){check(s==sim&&graphics==123&&d==data&&context==data+0x280&&queue==owner+0xce6b8,"frontend event ABI");}
void original_tick(U o,U graphics,U d){check(o==owner&&graphics==123&&d==data,"frontend tick ABI");if(event_kind){const auto vt=event_kind==35?0xb5f0f8:event_kind==36?0xb524a0:0xafa6a8;put(message,0,base+vt);event_kind=0;test_vehicle_keypad_event(base+0x84d207,sim,graphics,data,data+0x280,owner+0xce6b8,message);}}
void cancel(U s,U graphics,U d,U context,U queue,int command){check(s==sim&&graphics==123&&d==data&&context==data+0x280&&queue==owner+0xce6b8&&command==4,"native cancel arguments");++closed;put(ui,0xe0,std::uint32_t{});put(ui,0x1c,std::uint8_t{});}
void* allocate(std::size_t n){check(n==sizeof(NativeKeypadRequest),"request allocation size");if(++allocations==fail_allocation)return nullptr;
    if(mutation==1)put(ui,0xe0,std::uint32_t{999});if(mutation==2)put(holder,0,U{});return new NativeKeypadRequest;}
void destroy(void* p,std::size_t n){check(n==sizeof(NativeKeypadRequest),"request delete size");++frees;delete static_cast<NativeKeypadRequest*>(p);}
void push(void* q,void* p){check(reinterpret_cast<U>(q)==data+0x5c0,"submit queue scope");sent.push_back(*static_cast<NativeKeypadRequest*>(p));destroy(p,sizeof(NativeKeypadRequest));if(throw_push)throw std::runtime_error("uncertain");}
struct Fixture {
    std::vector<unsigned char> o=std::vector<unsigned char>(0xce800),s=std::vector<unsigned char>(0x4000),d=std::vector<unsigned char>(0x700),w=std::vector<unsigned char>(0xbba10),n=std::vector<unsigned char>(0x100),h=std::vector<unsigned char>(8),a=std::vector<unsigned char>(8),u=std::vector<unsigned char>(0x120),p=std::vector<unsigned char>(0x100),m=std::vector<unsigned char>(0x58);
    Fixture(unsigned type=6){owner=reinterpret_cast<U>(o.data());sim=reinterpret_cast<U>(s.data());data=reinterpret_cast<U>(d.data());world=reinterpret_cast<U>(w.data());node=reinterpret_cast<U>(n.data());holder=reinterpret_cast<U>(h.data());actor=reinterpret_cast<U>(a.data());ui=reinterpret_cast<U>(u.data());pause=reinterpret_cast<U>(p.data());message=reinterpret_cast<U>(m.data());
        closed=allocations=frees=mutation=fail_allocation=0;throw_push=false;sent.clear();
        put(owner,0,base+0xb0f4a0);put(owner,0xce190,sim);put(owner,0x350,sim);put(owner,0xce150,pause);put(pause,0,base+0xb07c10);put(data,0,base+0xb17cc0);
        put(sim,0,base+0xb1a988);put(sim,0x38,world);put(world,0xbb9f8,node);put(node,0,4);put(node,0x90,holder);put(holder,0,actor);put(sim,0x3c38,ui);
        put(ui,0,base+0xb60628);put(ui,0x1c,std::uint8_t{1});put(ui,0xe0,std::uint32_t{7});put(ui,0xe4,std::uint32_t{8});put(ui,0xe8,std::array<std::int32_t,3>{9,-10,11});put(ui,0x100,static_cast<std::uint8_t>(type==6));
        put(message,0x38,std::array<float,2>{1.5f,-2.f});put(message,0x40,type);
        test_vehicle_keypad_setup(base,original_tick,original_event,cancel,allocate,destroy,push);vehicle_keypad_poll(true);event_kind=35;tick();
    }
    void tick(){test_vehicle_keypad_tick(owner,123,data);}
    NativeKeypadSession current(){const auto v=vehicle_keypad_poll(true);check(v.has_value(),"missing recognized Show");return *v;}
};
int main(){try{
    check(!install_native_vehicle_keypad(1,false),"false profile accepted");
    for(unsigned type:{5u,6u}){Fixture f(type);auto s=f.current();check(s.owner==owner&&s.actor==actor&&s.vehicle==7,"Show scope certificate");check(vehicle_keypad_reply(s.generation,std::array<float,2>{12,-8}),"reply rejected");f.tick();
        check(sent.size()==2&&sent[0].value==12&&sent[1].value==(type==5?-2:-8)&&sent[0].channel==0&&sent[1].channel==1&&sent[0].grid==s.grid&&sent[0].vehicle_id==7,"wrong native values or target");
        check(closed==1&&allocations==frees&&!vehicle_keypad_reply(s.generation,std::array<float,2>{3,4}),"duplicate submit or leaked allocation");f.tick();check(sent.size()==2,"reply replayed");}
    {Fixture f;auto s=f.current();vehicle_keypad_reply(s.generation,{});f.tick();check(sent.empty()&&closed==1,"cancel submitted");}
    {Fixture f;f.current();vehicle_keypad_poll(false);f.tick();check(sent.empty()&&closed==1,"focus loss did not cancel");}
    {Fixture f;auto s=f.current();vehicle_keypad_reply(s.generation,std::array<float,2>{3,4});vehicle_keypad_poll(false);f.tick();check(sent.empty()&&closed==1,"queued OK survived focus loss");}
    {Fixture f;auto s=f.current();vehicle_keypad_reply(s.generation,std::array<float,2>{3,4});put(pause,0x58,1);f.tick();check(sent.empty()&&closed==1,"pause committed or left hidden native keypad open");}
    for(unsigned kind:{35u,36u,37u}){Fixture f;auto s=f.current();vehicle_keypad_reply(s.generation,std::array<float,2>{3,4});event_kind=kind;f.tick();check(sent.empty(),"old reply survived Show/Hide/Update");}
    for(unsigned change:{1u,2u}){Fixture f;auto s=f.current();vehicle_keypad_reply(s.generation,std::array<float,2>{3,4});mutation=change;f.tick();check(sent.empty()&&allocations==frees,"allocation reentry changed target or leaked");}
    for(unsigned fail:{1u,2u}){Fixture f;auto s=f.current();vehicle_keypad_reply(s.generation,std::array<float,2>{3,4});fail_allocation=fail;f.tick();check(sent.empty()&&frees==fail-1,"failed preparation was submitted or leaked");}
    {Fixture f;auto s=f.current();vehicle_keypad_reply(s.generation,std::array<float,2>{3,4});throw_push=true;f.tick();check(sent.size()==1&&allocations==frees&&vehicle_keypad_stats().faults==1,"uncertain ownership retry or leak");f.tick();check(sent.size()==1,"uncertain submission retried");}
    std::cout<<"PASS native keypad frontend scope, submission, cancellation and stale sessions\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
