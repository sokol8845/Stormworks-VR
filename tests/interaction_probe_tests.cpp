#include "swvr/interaction_probe.hpp"
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
using Selection=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,const int*);
using Origin=void*(*)(void*,double*,std::uint8_t,std::uint8_t);
using Action=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,int,int);
std::uintptr_t base{};
void* selection_detour{};void* origin_detour{};void* action_detour{};
unsigned create_calls{},remove_calls{},selection_calls{},origin_calls{},action_calls{};
unsigned fail_create{};
std::array<std::uint64_t,4> expected_contexts{0x1020304050607080,0x2030405060708090,0x3040506070809010,0x4050607080901020};
std::array<unsigned char,0xab0> controller;
int mode=1;
bool call_origin_in_selection=true;
bool throw_origin{};
template<class T>void put(std::size_t offset,const T& value){std::memcpy(controller.data()+offset,&value,sizeof(value));}
void* native_origin(void* character,double* destination,std::uint8_t a3,std::uint8_t a4) {
    require(character==controller.data() && a3==0x81 && a4==0xfe,"origin ABI changed");
    if(throw_origin)throw std::runtime_error("expected native Origin exception");
    ++origin_calls;destination[0]=10.125;destination[1]=-20.25;destination[2]=30.5;
    return destination+1; // Ensure wrapper preserves RAX instead of guessing destination.
}
void native_selection(void* pointer,std::uint64_t a2,std::uint64_t a3,std::uint64_t a4,
                      std::uint64_t a5,const int* a6) {
    require(pointer==controller.data() && a6==&mode &&
            std::array{a2,a3,a4,a5}==expected_contexts,"selection ABI changed");
    ++selection_calls;
    if(call_origin_in_selection) {
        double result[3]{};
        // Executable thunk calls the origin detour at the measured return RVA.
        const auto stub=reinterpret_cast<Origin>(base+0x74f37d);
        require(stub(controller.data(),result,0x81,0xfe)==result+1,"origin return pointer lost");
        require(result[0]==10.125 && result[1]==-20.25 && result[2]==30.5,"origin output changed");
        // Same native helper at a different return address must not be counted.
        require(reinterpret_cast<Origin>(origin_detour)(controller.data(),result,0x81,0xfe)==result+1,
                "unrelated origin return pointer lost");
    }
    put<std::uintptr_t>(0x520,0x2222);put<std::uintptr_t>(0x538,0x4444);
    put(0x528,std::array<float,3>{4,5,6});
}
void native_action(void* pointer,std::uint64_t a2,std::uint64_t a3,std::uint64_t a4,
                   std::uint64_t a5,int id,int phase) {
    require(pointer==controller.data() && std::array{a2,a3,a4,a5}==expected_contexts,"action ABI changed");
    ++action_calls;
    if(id==0x14)put<std::uintptr_t>(0x558,phase==1?0x2222:0);
}
void call_selection() {
    reinterpret_cast<Selection>(selection_detour)(controller.data(),expected_contexts[0],expected_contexts[1],
                                                expected_contexts[2],expected_contexts[3],&mode);
}
void call_action(int id,int phase) {
    reinterpret_cast<Action>(action_detour)(controller.data(),expected_contexts[0],expected_contexts[1],
                                          expected_contexts[2],expected_contexts[3],id,phase);
}
std::vector<std::string> columns(const std::string& row) {
    std::vector<std::string> values;std::size_t from{};
    for(;;){const auto at=row.find('\t',from);values.push_back(row.substr(from,at-from));if(at==std::string::npos)return values;from=at+1;}
}
void initialize_image(unsigned char* image) {
    constexpr unsigned char selection[]{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0x58,0xf9,0xff,0xff,0x48,0x81};
    constexpr unsigned char origin[]{0x40,0x55,0x53,0x57,0x48,0x8d,0xac,0x24,0x20,0xfe,0xff,0xff,0x48,0x81,0xec,0xe0,0x02,0x00,0x00,0x48,0x8b,0x05,0x56,0x4c};
    constexpr unsigned char action[]{0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x4c,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41};
    std::memcpy(image+0x74f260,selection,sizeof(selection));std::memcpy(image+0x4e33a0,origin,sizeof(origin));
    std::memcpy(image+0x78c7a0,action,sizeof(action));
}
RUNTIME_FUNCTION stub_function{};
void initialize_stub(unsigned char* image) {
    // sub rsp,28h; mov rax,absolute detour; call rax; add rsp,28h; ret.
    // Return address of CALL is exactly image_base+74F38D.
    std::array<unsigned char,21> code{0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,0x28,0xc3};
    const auto target=reinterpret_cast<std::uintptr_t>(origin_detour);std::memcpy(code.data()+6,&target,sizeof(target));
    std::memcpy(image+0x74f37d,code.data(),code.size());
    const std::array<unsigned char,8> unwind{1,4,1,0,4,0x42,0,0}; // UWOP_ALLOC_SMALL: SUB RSP,28h.
    std::memcpy(image+0x7f0000,unwind.data(),unwind.size());
    stub_function={0x74f37d,0x74f37d+static_cast<DWORD>(code.size()),0x7f0000};
    require(RtlAddFunctionTable(&stub_function,1,base)!=0,"Could not register generated Origin thunk unwind metadata");
    FlushInstructionCache(GetCurrentProcess(),image+0x74f37d,code.size());
}
}
// Test double implements only lifecycle operations used by install. It neither
// changes executable entry points nor initializes/enables any real hooks.
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID detour,LPVOID* original) {
    ++create_calls;if(fail_create && create_calls==fail_create)return MH_ERROR_MEMORY_ALLOC;
    const auto offset=reinterpret_cast<std::uintptr_t>(target)-base;
    if(offset==0x74f260){selection_detour=detour;*original=reinterpret_cast<void*>(native_selection);}
    else if(offset==0x4e33a0){origin_detour=detour;*original=reinterpret_cast<void*>(native_origin);}
    else if(offset==0x78c7a0){action_detour=detour;*original=reinterpret_cast<void*>(native_action);}
    else return MH_ERROR_NOT_EXECUTABLE;
    return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID){++remove_calls;return MH_OK;}

int main() {
    using swvr::InteractionProbeStatus;
    try {
        const auto root=std::filesystem::current_path()/"artifacts";
        std::filesystem::create_directories(root);
        const auto name="interaction-probe-test-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
        const auto trace=root/name;
        auto image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
        require(image!=nullptr,"test image allocation");base=reinterpret_cast<std::uintptr_t>(image);initialize_image(image);
        SetEnvironmentVariableW(L"SWVR_INTERACTION_PROBE",L"0");
        require(swvr::install_interaction_probe(base,true,trace)==InteractionProbeStatus::disabled && !create_calls,"disabled probe installed hooks");
        SetEnvironmentVariableW(L"SWVR_INTERACTION_PROBE",L"1");
        require(swvr::install_interaction_probe(base,false,trace)==InteractionProbeStatus::unsupported_profile && !create_calls,"SHA guard failed");
        image[0x4e33a0]^=1;
        require(swvr::install_interaction_probe(base,true,trace)==InteractionProbeStatus::signature_mismatch && !create_calls,"signature guard failed");
        image[0x4e33a0]^=1;
        require(swvr::install_interaction_probe(base,true,"relative.tsv")==InteractionProbeStatus::io_failure,"relative output accepted");
        const auto occupied=root/(name+"-occupied");
        {std::ofstream stream(occupied.wstring()+L".interaction.tsv");stream<<"sentinel";}
        require(swvr::install_interaction_probe(base,true,occupied)==InteractionProbeStatus::io_failure && !create_calls,"existing output overwritten");
        fail_create=2;
        require(swvr::install_interaction_probe(base,true,root/(name+"-failed"))==InteractionProbeStatus::hook_failure && remove_calls==1,"partial hooks not rolled back");
        fail_create=0;create_calls=0;
        require(swvr::install_interaction_probe(base,true,trace)==InteractionProbeStatus::installed && create_calls==3,"probe installation failed");
        require(swvr::install_interaction_probe(base,true,trace)==InteractionProbeStatus::already_installed && create_calls==3,"double install changed hooks");
        initialize_stub(image);
        controller.fill(0);put<std::uintptr_t>(0x520,0x1111);put<std::uintptr_t>(0x538,0x3333);
        put(0x528,std::array<float,3>{1,2,3});put(0xaa0,std::array<float,3>{0,0,1});
        call_selection();call_action(0x14,1);call_action(0x14,0);call_action(0x20,1);
        require(origin_calls==2 && selection_calls==1 && action_calls==3,"native calls skipped or duplicated");
        throw_origin=true;bool exception_seen{};
        try {call_selection();}catch(const std::runtime_error&){exception_seen=true;}
        throw_origin=false;require(exception_seen,"Native Origin exception was swallowed or failed to unwind through fixture thunk");
        swvr::flush_interaction_probe();
        std::ifstream input(trace.wstring()+L".interaction.tsv");std::string row;std::vector<std::vector<std::string>> records;
        while(std::getline(input,row))if(!row.empty() && row[0]!='#' && row.rfind("seq\t",0)!=0)records.push_back(columns(row));
        require(records.size()==3,"unrelated action/origin was logged");
        const auto& selected=records[0];require(selected.size()==36 && selected[3]=="selection" && selected[13]=="1","caller/TLS filter failed");
        require(selected[14]=="10.125" && selected[15]=="-20.25" && selected[16]=="30.5" && selected[19]=="1","ray values changed");
        require(selected[20]=="1111" && selected[21]=="2222" && selected[24]=="1" && selected[27]=="4","selection snapshots wrong");
        require(records[1][11]=="20" && records[1][12]=="1" && records[1][30]=="0" && records[1][31]=="2222","press snapshots wrong");
        require(records[2][12]=="0" && records[2][30]=="2222" && records[2][31]=="0","release snapshots wrong");
        // The instrumentation itself leaves the entire native object unchanged.
        const auto before=controller;call_action(0x13,0);require(controller==before,"probe mutated native bytes");
        call_origin_in_selection=false;
        for(unsigned i=0;i<10005;++i)call_selection();
        require(selection_calls==10007,"event cap suppressed original game work");
        swvr::flush_interaction_probe();std::ifstream capped(trace.wstring()+L".interaction.tsv");
        unsigned count{};bool limit{};
        while(std::getline(capped,row)){if(row=="#state\tlimit_reached")limit=true;else if(!row.empty() && row[0]!='#' && row.rfind("seq\t",0)!=0)++count;}
        require(count==10000 && limit,"probe cap/flush incorrect");
        RtlDeleteFunctionTable(&stub_function);VirtualFree(image,0,MEM_RELEASE);SetEnvironmentVariableW(L"SWVR_INTERACTION_PROBE",nullptr);
        std::cout<<"Interaction probe ABI, caller/TLS filter, snapshots, guards and cap passed (fake MinHook/native functions).\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
