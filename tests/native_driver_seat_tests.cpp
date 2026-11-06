#include "swvr/native_driver_seat.hpp"
#include "swvr/native_locomotion.hpp"
#include <windows.h>
#include <MinHook.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace swvr;using U=std::uintptr_t;
namespace {
void check(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
template<class T>void put(U at,std::size_t offset,T value){std::memcpy(reinterpret_cast<void*>(at+offset),&value,sizeof(value));}
using Update=void(*)(U,U,U,U,U,U,U,U,U,U,U,U,U,U,U,U);
Update detour{};U local{};unsigned calls{};std::array<U,16> received{};bool exit_seat{};
void original(U a,U b,U c,U d,U e,U f,U g,U h,U i,U j,U k,U l,U m,U n,U o,U p){received={a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p};++calls;if(exit_seat)local=0;}
}
std::uintptr_t swvr::native_steering_seat() noexcept{return local;}
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID replacement,LPVOID* trampoline){detour=reinterpret_cast<Update>(replacement);*trampoline=reinterpret_cast<void*>(&original);return MH_OK;}
int main(){try{
    std::vector<unsigned char> image(0xb0a000),seat(0x60),definition(0x800),section(0x98);
    const U base=reinterpret_cast<U>(image.data()),s=reinterpret_cast<U>(seat.data()),d=reinterpret_cast<U>(definition.data()),t=reinterpret_cast<U>(section.data());
    const std::array<unsigned char,24> prefix{0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x00,0x01};
    check(!install_native_driver_seat(base,false)&&!install_native_driver_seat(base,true),"unknown EXE accepted");
    std::memcpy(image.data()+0x3487d0,prefix.data(),prefix.size());check(!install_native_driver_seat(base,true),"wrong vtable accepted");
    put(base,0xb09b18+0x358,base+0x3487d0);check(install_native_driver_seat(base,true)&&detour,"observer hook unavailable");
    put(s,0,base+0xb09b18);put(s,0x58,d);put(d,0x7f8,int{5});
    const std::array<int,3> grid{8,12,-4};const std::array<int,9> rotation{0,0,1,0,1,0,-1,0,0};
    put(s,0x18,grid);put(s,0x30,rotation);
    Matrix4d world{1,0,0,0,0,1,0,0,0,0,-1,0,10000,3,-20000,1};put(t,0x18,world);
    std::array<float,3> offset{1,2,3};const U off=reinterpret_cast<U>(offset.data());
    auto invoke=[&]{detour(s,2,3,4,t,off,7,8,9,10,11,12,13,14,15,16);};
    invoke();check(calls==1&&!native_driver_seat(s).valid,"foreign seat captured");local=s;invoke();
    check(calls==2&&received==std::array<U,16>{s,2,3,4,t,off,7,8,9,10,11,12,13,14,15,16},"native ABI arguments or call count changed");
    auto snapshot=native_driver_seat(s);check(snapshot.valid&&snapshot.world==driver_seat_transform(world,offset,grid,rotation),"seat transform not copied");
    check(snapshot.world[12]==10001&&snapshot.world[13]==4&&snapshot.world[14]==-19996,"native grid/COM/reflection formula");
    check(!native_driver_seat(0).valid&&!native_driver_seat(s+1).valid,"foreign identity reused snapshot");
    std::this_thread::sleep_for(std::chrono::milliseconds(110));check(!native_driver_seat(s).valid,"stale snapshot accepted");
    put(d,0x7f8,int{2});invoke();check(!native_driver_seat(s).valid,"helm identified as Driver seat");put(d,0x7f8,int{5});
    put(s,0x58,U{1});invoke();check(!native_driver_seat(s).valid,"unreadable definition accepted");put(s,0x58,d);
    exit_seat=true;invoke();check(!native_driver_seat(s).valid,"seat changed during native call");exit_seat=false;local=s;
    world[0]=2;put(t,0x18,world);invoke();check(!native_driver_seat(s).valid,"scaled invalid basis accepted");
    std::cout<<"PASS Driver seat observer: native ABI, identity, grid, rotation, freshness and guards\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
