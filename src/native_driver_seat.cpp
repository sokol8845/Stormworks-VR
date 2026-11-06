#include "swvr/native_driver_seat.hpp"
#include "swvr/native_locomotion.hpp"
#include <windows.h>
#include <MinHook.h>
#include <mutex>
#include <cstring>

namespace swvr {
namespace {
using U=std::uintptr_t;
constexpr U seat_update_rva=0x3487d0,seat_vtable=0xb09b18;
constexpr std::array<unsigned char,24> seat_update_prefix{
    0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x56,
    0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x00,0x01};
// This vtable override uses RCX and stack slots 5,6,8,16. Preserve all slots;
// RDX/R8/R9 and floating register arguments are unused by the verified body.
using Update=void(*)(U,U,U,U,U,U,U,U,U,U,U,U,U,U,U,U);
Update original{};U image{};
std::mutex mutex;DriverSeatSnapshot snapshot;
template<class T>bool copy(U address,T& value) noexcept {
    SIZE_T read{};return address&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&read)&&read==sizeof(value);
}
DriverSeatSnapshot capture(U seat,U section,U offset) noexcept {
    DriverSeatSnapshot result;
    if(!seat||seat!=native_steering_seat())return result;
    std::array<unsigned char,0x60> component{};U table{},definition{};int pose{};
    Matrix4d matrix{};std::array<float,3> origin{};std::array<int,3> grid{};std::array<int,9> rotation{};
    if(!copy(seat,component))return result;
    std::memcpy(&table,component.data(),8);std::memcpy(&definition,component.data()+0x58,8);
    if(table!=image+seat_vtable||!definition||!copy(definition+0x7f8,pose)||pose!=5||
       !section||!copy(section+0x18,matrix)||!copy(offset,origin))return result;
    std::memcpy(grid.data(),component.data()+0x18,sizeof(grid));std::memcpy(rotation.data(),component.data()+0x30,sizeof(rotation));
    try{result.world=driver_seat_transform(matrix,origin,grid,rotation);result.seat=seat;result.valid=true;}catch(...){}
    return result;
}
void update(U a,U b,U c,U d,U e,U f,U g,U h,U i,U j,U k,U l,U m,U n,U o,U p){
    const bool local=a&&a==native_steering_seat();
    auto next=capture(a,e,f);
    original(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p);
    if(local){
        if(a!=native_steering_seat())next={};
        next.observed=std::chrono::steady_clock::now();std::lock_guard lock(mutex);snapshot=next;
    }
}
}
bool install_native_driver_seat(U base,bool full_sha) noexcept {
    if(!full_sha||!base||original)return false;
    std::array<unsigned char,24> prefix{};U target{};
    if(!copy(base+seat_update_rva,prefix)||prefix!=seat_update_prefix||!copy(base+seat_vtable+0x358,target)||target!=base+seat_update_rva)return false;
    image=base;
    return MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(&update),reinterpret_cast<void**>(&original))==MH_OK;
}
DriverSeatSnapshot native_driver_seat(U local_seat) noexcept {
    std::lock_guard lock(mutex);const auto now=std::chrono::steady_clock::now();
    if(!local_seat||!snapshot.valid||snapshot.seat!=local_seat||now<snapshot.observed||now-snapshot.observed>std::chrono::milliseconds(100))return {};
    return snapshot;
}
}
