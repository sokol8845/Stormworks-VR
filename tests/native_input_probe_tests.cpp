#include "swvr/native_input_probe.hpp"
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
using Pop=bool(*)(void*,void**);
using Dispatch=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,void*);
std::uintptr_t image_base{};
void* pop_detour{};void* dispatch_detour{};
std::atomic<unsigned> create_calls{},remove_calls{},pop_calls{},dispatch_calls{},unexpected_hooks{};
unsigned fail_create{};
std::array<std::uint64_t,7> arguments{},expected_arguments{};
void* expected_queue{};void** expected_slot{};
bool pop_result{},write_slot{},throw_pop{},throw_dispatch{},mutate_dispatch{};
void* next_event{};
constexpr std::uintptr_t queue_offset=0xce6b8;
constexpr int unknown=std::numeric_limits<int>::min();
template<class T>void put(std::vector<unsigned char>& bytes,std::size_t offset,const T& value) {
    require(offset+sizeof(value)<=bytes.size(),"Fixture put overflow");std::memcpy(bytes.data()+offset,&value,sizeof(value));
}
template<class T>T get(const std::vector<unsigned char>& bytes,std::size_t offset) {
    require(offset+sizeof(T)<=bytes.size(),"Fixture get overflow");T value{};std::memcpy(&value,bytes.data()+offset,sizeof(value));return value;
}
std::uintptr_t address(const std::vector<unsigned char>& bytes){return reinterpret_cast<std::uintptr_t>(bytes.data());}
struct Fixture {
    std::vector<unsigned char> owner=std::vector<unsigned char>(0xcf000),simulation=std::vector<unsigned char>(0x4000),
        application=std::vector<unsigned char>(0x240),pause=std::vector<unsigned char>(0x100),
        component=std::vector<unsigned char>(0x60),definition=std::vector<unsigned char>(0xdb0),
        key_event=std::vector<unsigned char>(0x10),mode_event=std::vector<unsigned char>(0x10);
    void initialize() {
        for(auto* bytes:{&owner,&simulation,&application,&pause,&component,&definition,&key_event,&mode_event})std::fill(bytes->begin(),bytes->end(),static_cast<unsigned char>(0));
        put(owner,0,image_base+0xb0f4a0);put(owner,0xce190,address(simulation));put(owner,0x350,address(simulation));
        put(owner,0xce150,address(pause));put(simulation,0,image_base+0xb1a988);put(application,0,image_base+0xb17cc0);
        put(pause,0,image_base+0xb07c10);put<unsigned char>(simulation,0x3010,1);
        put(component,0,image_base+0xb75658);put(component,0x58,address(definition));put<int>(definition,0xda4,0);
        put(simulation,0x38+0x520,address(component));put(simulation,0x38+0x558,address(component));
        put(key_event,0,image_base+0xbb2110);put<int>(key_event,8,20);put<int>(key_event,0xc,1);
        put(mode_event,0,image_base+0xb666b0);put<unsigned char>(mode_event,8,1);
        owner[0xcd001]=0x73;simulation[0x127]=0xab;application[0x23f]=0x42;definition[0xdaf]=0x77;
        arguments={address(simulation)+0x38,address(application),0x1020304050607080ULL,0x2030405060708090ULL,
            address(owner)+0xce5d0,address(owner)+0xce600,address(key_event)};
        mutate_dispatch=throw_dispatch=throw_pop=write_slot=false;pop_result=false;
    }
    std::vector<unsigned char> memory()const {
        std::vector<unsigned char> result;
        for(const auto* bytes:{&owner,&simulation,&application,&pause,&component,&definition,&key_event,&mode_event})result.insert(result.end(),bytes->begin(),bytes->end());
        return result;
    }
};
std::unique_ptr<Fixture> fixture;
struct Guarded {
    unsigned char* base{};std::size_t size{};
    explicit Guarded(std::size_t length):size(length) {
        base=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE,PAGE_NOACCESS));
        require(base!=nullptr,"Guarded reservation failed");
    }
    std::uintptr_t last_word(std::uintptr_t value) {
        require(VirtualAlloc(base,0x1000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"Guarded first-page commit failed");
        std::memcpy(base+0xff8,&value,sizeof(value));return reinterpret_cast<std::uintptr_t>(base+0xff8);
    }
    ~Guarded(){if(base)VirtualFree(base,0,MEM_RELEASE);}
    Guarded(const Guarded&)=delete;Guarded& operator=(const Guarded&)=delete;
};
bool native_pop(void* queue,void** slot) {
    require(queue==expected_queue && slot==expected_slot,"Pop ABI changed");++pop_calls;
    if(throw_pop)throw std::runtime_error("expected pop exception");
    if(write_slot)*slot=next_event;
    return pop_result;
}
void native_dispatch(void* controller,std::uint64_t a2,std::uint64_t a3,std::uint64_t a4,
    std::uint64_t a5,std::uint64_t a6,void* input) {
    require(std::array<std::uint64_t,7>{reinterpret_cast<std::uintptr_t>(controller),a2,a3,a4,a5,a6,
        reinterpret_cast<std::uintptr_t>(input)}==expected_arguments,"Dispatch seven-argument ABI changed");
    ++dispatch_calls;
    if(throw_dispatch)throw std::runtime_error("expected dispatch exception");
    if(mutate_dispatch) {
        put<unsigned char>(fixture->simulation,0x38+0x49c,1);put<unsigned char>(fixture->simulation,0x38+0x49f,1);
        put<unsigned char>(fixture->simulation,0x38+0x4bf,1);put<std::uintptr_t>(fixture->simulation,0x38+0x560,address(fixture->component));
    }
}
void call_dispatch(std::uintptr_t caller=0x86a76c) {
    expected_arguments=arguments;
    const auto call=caller?reinterpret_cast<Dispatch>(image_base+caller-39):reinterpret_cast<Dispatch>(dispatch_detour);
    call(reinterpret_cast<void*>(arguments[0]),arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],reinterpret_cast<void*>(arguments[6]));
}
bool call_pop(void* queue,void** slot,std::uintptr_t caller=0x86a728) {
    expected_queue=queue;expected_slot=slot;
    const auto call=caller?reinterpret_cast<Pop>(image_base+caller-9):reinterpret_cast<Pop>(pop_detour);
    return call(queue,slot);
}
void initialize_image(unsigned char* image) {
    constexpr unsigned char pop[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30};
    constexpr unsigned char dispatch[]{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xb8,0xfc,0xff,0xff,0x48,0x81};
    std::memcpy(image+0x694230,pop,sizeof(pop));std::memcpy(image+0x790850,dispatch,sizeof(dispatch));
    for(const auto [returned,target]:std::array<std::array<std::uintptr_t,2>,6>{{
        {0x86a728,0x694230},{0x86ccef,0x694230},{0x86a77c,0x694230},{0x86cd4e,0x694230},
        {0x86a76c,0x790850},{0x86cd3d,0x790850}}}) {
        image[returned-5]=0xe8;const auto displacement=static_cast<std::int32_t>(target-returned);
        std::memcpy(image+returned-4,&displacement,4);
    }
}
void initialize_thunks(unsigned char* image) {
    for(const auto [stub,detour]:std::array<std::array<std::uintptr_t,2>,2>{{
        {0x880000,reinterpret_cast<std::uintptr_t>(pop_detour)},
        {0x880020,reinterpret_cast<std::uintptr_t>(dispatch_detour)}}}) {
        const std::array<unsigned char,2> mov{0x48,0xb8},jump{0xff,0xe0};
        std::memcpy(image+stub,mov.data(),2);std::memcpy(image+stub+2,&detour,8);std::memcpy(image+stub+10,jump.data(),2);
    }
    for(const auto returned:{0x86a728u,0x86ccefu,0x86a77cu,0x86cd4eu}) {
        // Relative CALL to a nearby absolute-jump stub keeps adjacent native
        // return RVAs usable without overlapping another thunk's epilogue.
        std::array<unsigned char,14> thunk{0x48,0x83,0xec,0x28,0xe8,0,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
        const auto displacement=static_cast<std::int32_t>(0x880000-returned);std::memcpy(thunk.data()+5,&displacement,4);
        std::memcpy(image+returned-9,thunk.data(),thunk.size());
    }
    for(const auto returned:{0x86a76cu,0x86cd3du}) {
        std::array<unsigned char,44> thunk{
            0x48,0x83,0xec,0x38,0x48,0x8b,0x44,0x24,0x60,0x48,0x89,0x44,0x24,0x20,
            0x48,0x8b,0x44,0x24,0x68,0x48,0x89,0x44,0x24,0x28,
            0x48,0x8b,0x44,0x24,0x70,0x48,0x89,0x44,0x24,0x30,
            0xe8,0,0,0,0,0x48,0x83,0xc4,0x38,0xc3};
        const auto displacement=static_cast<std::int32_t>(0x880020-returned);std::memcpy(thunk.data()+35,&displacement,4);
        std::memcpy(image+returned-39,thunk.data(),thunk.size());
    }
    FlushInstructionCache(GetCurrentProcess(),image,0x900000);
}
std::array<RUNTIME_FUNCTION,6> thunk_functions{};
void register_thunk_unwind(unsigned char* image) {
    // Generated x64 functions adjust RSP. Windows must know that allocation to
    // unwind a native C++ exception through this test-only call-site fixture.
    // One UWOP_ALLOC_SMALL code describes SUB RSP,28h/38h at prolog byte 4.
    const std::array<unsigned char,8> pop_info{1,4,1,0,4,0x42,0,0},dispatch_info{1,4,1,0,4,0x62,0,0};
    std::memcpy(image+0x880100,pop_info.data(),pop_info.size());std::memcpy(image+0x880108,dispatch_info.data(),dispatch_info.size());
    std::size_t index{};
    for(const auto returned:{0x86a728u,0x86a76cu,0x86a77cu,0x86ccefu,0x86cd3du,0x86cd4eu}) {
        const bool dispatch=returned==0x86a76c || returned==0x86cd3d;
        thunk_functions[index++]={returned-(dispatch?39u:9u),returned+5,dispatch?0x880108u:0x880100u};
    }
    require(RtlAddFunctionTable(thunk_functions.data(),static_cast<DWORD>(thunk_functions.size()),image_base)!=0,
        "Could not register fixture thunk unwind metadata");
}
std::vector<std::string> split(const std::string& row) {
    std::vector<std::string> result;std::size_t from{};
    for(;;){const auto end=row.find('\t',from);result.push_back(row.substr(from,end-from));if(end==std::string::npos)return result;from=end+1;}
}
struct Log {
    std::vector<std::string> header,metadata;
    std::vector<std::vector<std::string>> rows;
    std::string value(std::size_t row,const std::string& field)const {
        const auto it=std::find(header.begin(),header.end(),field);require(it!=header.end(),"Missing diagnostic column");
        return rows.at(row).at(static_cast<std::size_t>(it-header.begin()));
    }
};
std::filesystem::path log_path;
Log log() {
    swvr::flush_native_input_probe();std::ifstream stream(log_path);require(stream.good(),"Cannot read probe log");
    Log result;std::string line;
    while(std::getline(stream,line)) {
        if(line.empty())continue;
        if(line[0]=='#'){result.metadata.push_back(line);continue;}
        auto values=split(line);
        if(values[0]=="seq"){result.header=std::move(values);continue;}
        require(values.size()==result.header.size(),"Diagnostic header/row schema mismatch");result.rows.push_back(std::move(values));
    }
    return result;
}
void assert_last(const std::string& field,const std::string& expected) {
    const auto records=log();require(!records.rows.empty(),"No diagnostic records");
    if(records.value(records.rows.size()-1,field)!=expected)throw std::runtime_error("Wrong diagnostic field: "+field);
}
void assert_readonly_dispatch(std::uintptr_t caller=0x86a76c) {
    const auto before=fixture->memory();call_dispatch(caller);require(fixture->memory()==before,"Read-only dispatch probe changed native memory");
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID detour,LPVOID* original) {
    ++create_calls;if(fail_create && create_calls==fail_create)return MH_ERROR_MEMORY_ALLOC;
    const auto rva=reinterpret_cast<std::uintptr_t>(target)-image_base;
    if(rva==0x694230){pop_detour=detour;*original=reinterpret_cast<void*>(native_pop);}
    else if(rva==0x790850){dispatch_detour=detour;*original=reinterpret_cast<void*>(native_dispatch);}
    else {++unexpected_hooks;return MH_ERROR_NOT_EXECUTABLE;}
    return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID){++remove_calls;return MH_OK;}
int main() {
    using swvr::NativeInputProbeStatus;
    try {
        const auto directory=std::filesystem::current_path()/"artifacts";std::filesystem::create_directories(directory);
        const auto name="native-input-probe-test-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
        const auto trace=directory/name;log_path=trace.wstring()+L".input-native.tsv";
        auto image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
        require(image!=nullptr,"Fake executable allocation failed");image_base=reinterpret_cast<std::uintptr_t>(image);initialize_image(image);
        require(!swvr::rearm_native_input_probe_log(),"Uninstalled probe log rearmed");
        SetEnvironmentVariableW(L"SWVR_INPUT_PROBE",L"0");
        require(swvr::install_native_input_probe(image_base,true,trace)==NativeInputProbeStatus::disabled && !create_calls,"Default created hooks");
        SetEnvironmentVariableW(L"SWVR_INPUT_PROBE",L"123456789012345");
        require(swvr::install_native_input_probe(image_base,true,trace)==NativeInputProbeStatus::disabled && !create_calls,"Long opt-in value accepted");
        SetEnvironmentVariableW(L"SWVR_INPUT_PROBE",L"1");
        require(swvr::install_native_input_probe(image_base,false,trace)==NativeInputProbeStatus::unsupported_profile && !create_calls,"SHA guard failed");
        for(const auto offset:{0x694230u,0x790850u,0x86a723u,0x86cceau,0x86a777u,0x86cd49u,0x86a767u,0x86cd38u}) {
            image[offset]^=1;
            require(swvr::install_native_input_probe(image_base,true,trace)==NativeInputProbeStatus::signature_mismatch && !create_calls,"Entry/call-site signature guard failed");
            image[offset]^=1;
        }
        require(swvr::install_native_input_probe(image_base,true,"relative")==NativeInputProbeStatus::io_failure,"Relative trace path accepted");
        const auto occupied=directory/(name+"-occupied");{std::ofstream stream(occupied.wstring()+L".input-native.tsv");stream<<"sentinel";}
        require(swvr::install_native_input_probe(image_base,true,occupied)==NativeInputProbeStatus::io_failure && !create_calls,"Existing trace overwritten");
        fail_create=2;
        require(swvr::install_native_input_probe(image_base,true,directory/(name+"-failed"))==NativeInputProbeStatus::hook_failure && remove_calls==1,"Partial hooks were not rolled back");
        fail_create=0;create_calls=0;
        require(swvr::install_native_input_probe(image_base,true,trace)==NativeInputProbeStatus::installed && create_calls==2,"Probe did not create exactly two hooks");
        require(swvr::install_native_input_probe(image_base,true,trace)==NativeInputProbeStatus::already_installed && create_calls==2,"Double installation changed hooks");
        initialize_thunks(image);register_thunk_unwind(image);fixture=std::make_unique<Fixture>();fixture->initialize();
        assert_readonly_dispatch();assert_last("before_relation","dispatch_observed_ui_unproven");assert_last("before_component_button_type","0");
        assert_last("before_input_known_type","23");assert_last("before_input_action","20");assert_last("before_input_phase","1");
        assert_last("before_right_action_active","0");assert_readonly_dispatch(0x86cd3d);
        for(const auto returned:{0x86a728u,0x86ccefu,0x86a77cu,0x86cd4eu}) {
            void* slot=nullptr;pop_result=true;write_slot=true;next_event=fixture->key_event.data();const auto before=fixture->memory();
            require(call_pop(reinterpret_cast<void*>(address(fixture->owner)+queue_offset),&slot,returned) && slot==next_event,"True pop return/output changed");
            require(fixture->memory()==before,"Pop probe wrote native memory");assert_last("before_input","0");assert_last("after_input_action","20");
            assert_last("first_pop",returned==0x86a728 || returned==0x86ccef?"1":"0");
            pop_result=false;write_slot=false;
            require(!call_pop(reinterpret_cast<void*>(address(fixture->owner)+queue_offset),&slot,returned) && slot==next_event,"False pop changed retained event");
            assert_last("pop_return","0");assert_last("before_input_action","20");assert_last("after_input_action","20");
        }
        {
            const auto before_count=log().rows.size();arguments={1,2,3,4,5,6,7};assert_readonly_dispatch(0);
            require(!call_pop(reinterpret_cast<void*>(1),reinterpret_cast<void**>(2),0),"Unknown pop caller changed return");
            require(log().rows.size()==before_count,"Unknown caller was sampled");
        }
        fixture->initialize();arguments[4]=1;arguments[5]=2;arguments[0]=arguments[1]=arguments[6]=1;
        assert_readonly_dispatch();assert_last("before_relation","owner_context_relation");
        {
            Guarded owner(0xd0000);const auto pointer=owner.last_word(image_base+0xb0f4f8);
            fixture->initialize();arguments={1,1,3,4,pointer+0xce5d0,pointer+0xce600,1};assert_readonly_dispatch();
            assert_last("before_relation","owner_profile");
            require(!call_pop(reinterpret_cast<void*>(pointer+queue_offset),reinterpret_cast<void**>(1)),"Owner mismatch changed pop");
            assert_last("before_relation","owner_profile");
        }
        {
            Guarded inaccessible(0x5000);fixture->initialize();
            put(fixture->owner,0xce190,reinterpret_cast<std::uintptr_t>(inaccessible.base));put<std::uintptr_t>(fixture->owner,0x350,0);
            arguments[0]=arguments[1]=arguments[6]=1;assert_readonly_dispatch();assert_last("before_relation","active_mismatch");
            require(!call_pop(reinterpret_cast<void*>(address(fixture->owner)+queue_offset),reinterpret_cast<void**>(1)),"Inactive simulation changed pop");
            assert_last("before_relation","active_mismatch");
        }
        {
            Guarded simulation(0x5000);const auto pointer=simulation.last_word(image_base+0xb1a798);fixture->initialize();
            put(fixture->owner,0xce190,pointer);put(fixture->owner,0x350,pointer);
            arguments[0]=pointer+0x38;arguments[1]=arguments[6]=1;assert_readonly_dispatch();assert_last("before_relation","simulation_profile");
        }
        fixture->initialize();arguments[0]=arguments[1]=arguments[6]=1;assert_readonly_dispatch();assert_last("before_relation","controller_mismatch");
        fixture->initialize();put<std::uintptr_t>(fixture->owner,0xce190,0);arguments[0]=arguments[1]=arguments[6]=1;
        assert_readonly_dispatch();assert_last("before_relation","null_simulation");
        {
            Guarded application(0x2000);const auto pointer=application.last_word(image_base+0xb17bf8);fixture->initialize();
            arguments[1]=pointer;arguments[6]=1;assert_readonly_dispatch();assert_last("before_relation","application_profile");
            assert_last("before_benchmark",std::to_string(unknown));
        }
        fixture->initialize();arguments[1]=0;arguments[6]=1;assert_readonly_dispatch();assert_last("before_relation","null_application");
        {
            Guarded opaque(0x2000);const auto pointer=opaque.last_word(image_base+0x1234);fixture->initialize();
            put(fixture->simulation,0x38+0x520,pointer);put(fixture->simulation,0x38+0x558,pointer);arguments[6]=pointer;
            assert_readonly_dispatch();assert_last("before_component_button_type",std::to_string(unknown));
            assert_last("before_input_known_type",std::to_string(unknown));
            put(fixture->owner,0xce150,pointer);assert_readonly_dispatch();assert_last("before_relation","pause_ui_unproven");
            assert_last("before_pause_pane",std::to_string(unknown));
        }
        {
            Guarded photo_event(0x2000);const auto pointer=photo_event.last_word(image_base+0xb86978);fixture->initialize();
            arguments[6]=pointer;assert_readonly_dispatch();assert_last("before_input_known_type","8");
            assert_last("before_input_mode",std::to_string(unknown));
        }
        fixture->initialize();put<std::uintptr_t>(fixture->owner,0xce150,0);assert_readonly_dispatch();assert_last("before_relation","pause_ui_unproven");
        fixture->initialize();put<std::uintptr_t>(fixture->component,0x58,0);assert_readonly_dispatch();assert_last("before_component_button_type",std::to_string(unknown));
        for(int type:{0,1,5,6}) {
            fixture->initialize();put(fixture->definition,0xda4,type);assert_readonly_dispatch();assert_last("before_component_button_type",std::to_string(type));
        }
        fixture->initialize();put<unsigned char>(fixture->application,0x233,3);put<int>(fixture->pause,0x58,9);
        put<std::uintptr_t>(fixture->owner,0xcde80,0x1111);put<std::uintptr_t>(fixture->simulation,0x25b0,0x2222);
        put<std::uintptr_t>(fixture->simulation,0x1d00,0x3333);put<std::uintptr_t>(fixture->simulation,0x25a8,0x4444);
        put<unsigned char>(fixture->simulation,0x3010,0);
        for(const auto offset:{0x494u,0x49cu,0x49fu,0x4bcu,0x4bdu})put<unsigned char>(fixture->simulation,0x38+offset,2);
        put<unsigned char>(fixture->simulation,0x38+0x4bf,7);assert_readonly_dispatch();
        assert_last("before_benchmark","3");assert_last("before_pause_pane","9");assert_last("before_ui_input","1111");
        assert_last("before_ui_captured","2222");assert_last("before_ui_active","3333");assert_last("before_ui_hover","4444");
        assert_last("before_right_action_active","7");assert_last("before_observed_blockers","4095");
        fixture->initialize();arguments[6]=address(fixture->mode_event);mutate_dispatch=true;
        auto expected_transition=fixture->memory();const auto simulation_start=fixture->owner.size();
        for(const auto offset:{0x49cu,0x49fu,0x4bfu})expected_transition[simulation_start+0x38+offset]=1;
        const auto held_left=address(fixture->component);
        std::memcpy(expected_transition.data()+simulation_start+0x38+0x560,&held_left,sizeof(held_left));
        call_dispatch();require(fixture->memory()==expected_transition,"Probe changed memory beyond original dispatch writes");
        assert_last("before_paused","0");assert_last("after_paused","1");assert_last("after_pause_menu","1");
        assert_last("before_right_action_active","0");assert_last("after_right_action_active","1");
        assert_last("before_input_known_type","2");assert_last("after_input_mode","1");
        require(get<unsigned char>(fixture->simulation,0x38+0x49c)==1,"Original dispatch mutation was suppressed");
        fixture->initialize();throw_dispatch=true;bool seen{};
        try {call_dispatch();}catch(const std::runtime_error&){seen=true;}
        require(seen,"Dispatch exception was swallowed");assert_last("completion","native_exception");
        throw_dispatch=false;throw_pop=true;seen=false;void* slot=nullptr;
        try {call_pop(reinterpret_cast<void*>(address(fixture->owner)+queue_offset),&slot);}catch(const std::runtime_error&){seen=true;}
        require(seen,"Pop exception was swallowed");assert_last("completion","native_exception");throw_pop=false;
        fixture->initialize();expected_arguments=arguments;
        std::array<std::thread,4> workers;
        std::atomic<bool> worker_failed{};
        for(auto& worker:workers)worker=std::thread([&] {
            try {
                for(unsigned i=0;i<25;++i)reinterpret_cast<Dispatch>(image_base+0x86cd3d-39)(reinterpret_cast<void*>(arguments[0]),
                    arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],reinterpret_cast<void*>(arguments[6]));
            }catch(...){worker_failed=true;}
        });
        for(auto& worker:workers)worker.join();require(!worker_failed,"Concurrent diagnostic calls failed");
        const auto before_cap=dispatch_calls.load();for(unsigned i=0;i<10005;++i)call_dispatch();
        require(dispatch_calls==before_cap+10005,"Log cap suppressed original dispatch");
        const auto capped=log();require(capped.rows.size()==10000,"Probe cap failed");
        require(std::find(capped.metadata.begin(),capped.metadata.end(),"#state\tlimit_reached")!=capped.metadata.end(),"Missing cap marker");
        arguments={1,1,3,4,1+0xce5d0,1+0xce600,1};call_dispatch();
        require(log().rows.size()==10000,"Capped probe inspected a new transaction");fixture->initialize();
        require(swvr::rearm_native_input_probe_log(),"Capped probe did not rearm");assert_readonly_dispatch(0x86cd3d);
        const auto rearmed=log();require(rearmed.rows.size()==10001,"Rearm did not preserve and append old records");
        require(rearmed.value(9999,"seq")=="10000" && rearmed.value(10000,"seq")=="10001" &&
            rearmed.value(10000,"epoch")=="1" && rearmed.value(10000,"epoch_row")=="1","Rearm counters were not independent");
        for(std::size_t i=0;i<rearmed.rows.size();++i)require(rearmed.value(i,"seq")==std::to_string(i+1),"Concurrent rows lost monotonic sequence");
        require(!unexpected_hooks && create_calls==2,"Probe installed unexpected hooks or emitted input");
        RtlDeleteFunctionTable(thunk_functions.data());VirtualFree(image,0,MEM_RELEASE);SetEnvironmentVariableW(L"SWVR_INPUT_PROBE",nullptr);
        std::cout<<"Native input probe tests passed: ABI, caller/profile/owner guards, read-only UI/target snapshots, native transitions, cap/rearm and threads.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
