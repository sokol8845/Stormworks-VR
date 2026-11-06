#include "swvr/native_button_retirement.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace swvr;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message){bool caught{};try{f();}catch(const std::exception&){caught=true;}require(caught,message);}
template<class T>T at(const void* object,std::size_t offset){T value{};std::memcpy(&value,static_cast<const unsigned char*>(object)+offset,sizeof(T));return value;}
template<class T>void put(void* object,std::size_t offset,const T& value){std::memcpy(static_cast<unsigned char*>(object)+offset,&value,sizeof(T));}
using Serializer=void(*)(void*,void*);using Dispatch=void(*)(void*,void*,void*,void*,void*);
using Clear=void(*)(void*);using Delete=void(*)(void*,std::size_t);
struct Hook {std::uintptr_t rva;void* detour{};};
std::array<Hook,5> hooks{{{0x23ca90},{0x855ab0},{0x8d3bb0},{0x89d030},{0x95c2ec}}};
unsigned create_calls{},remove_calls{},fail_create{},unexpected_hook_calls{};
std::uintptr_t image_base{};
void* original_for(std::uintptr_t rva);
struct Image {
    unsigned char* data{};std::array<RUNTIME_FUNCTION,16> unwind{};unsigned thunk_count{};bool registered{};
    static constexpr std::array<std::uintptr_t,10> returns{0x23cd40,0x86a7ff,0x240898,0x240a21,0x89d0a8,0x86a817,0x8d3bee,0x89d0cf,0x24005b,0x23eb68};
    Image() {
        data=static_cast<unsigned char*>(VirtualAlloc(nullptr,0xe00000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        require(data!=nullptr,"fixture image allocation failed");image_base=base();
        const std::array<std::pair<std::uintptr_t,std::vector<unsigned char>>,6> entries{{
            {0x23ca90,{0x83,0x7a,0x10,0x02,0x4c,0x8b,0xc9,0x72,0x2a,0x80,0x7a,0x14,0x00,0x75,0x0f,0x48,0x8b,0x42,0x08,0x44,0x0f,0xb7,0x00,0x66}},
            {0x855ab0,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0x78,0xe8,0xff,0xff,0xb8,0x18,0x19}},
            {0x8d3bb0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x33,0xf6,0x48,0x8b,0xd9,0x8b,0xfe,0x39,0x71}},
            {0x89d030,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x38,0x48,0x8b,0xd9,0x45,0x33,0xff,0x41,0x8b,0xef,0x44,0x39}},
            {0x95c2ec,{0xe9,0x8f,0xe4,0x7f,0xff}},
            {0x9b6750,{0x48,0x83,0xec,0x28,0x48,0x85,0xc9,0x74,0x0d,0x48,0x83,0xe1,0xf8,0x48,0x8b,0x49,0xf8,0xe8,0xce,0x3d,0xff,0xff,0x48,0x83}}
        }};
        for(const auto& entry:entries)std::memcpy(data+entry.first,entry.second.data(),entry.second.size());
        put(data,0xb89760,base()+0x23ca90);data[0x23cd3e]=0xff;data[0x23cd3f]=0x10;
        call(0x86a7ff,0x855ab0);call(0x240898,0x8d3bb0);call(0x240a21,0x8d3bb0);
        call(0x89d0a8,0x8d3bb0);call(0x86a817,0x89d030);call(0x8d3bee,0x95c2ec);call(0x89d0cf,0x95c2ec);
        call(0x24005b,0x8d3bb0);call(0x23eb68,0x8d3bb0);
        const std::array<unsigned char,24> reset{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf9,0x80};
        const std::array<unsigned char,24> destroy{0x48,0x89,0x4c,0x24,0x08,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x38,0x48,0x8b,0xf9,0xe8,0x57,0xe3,0xf3};
        std::memcpy(data+0x23ff80,reset.data(),reset.size());std::memcpy(data+0x23ea20,destroy.data(),destroy.size());
    }
    ~Image(){if(registered)RtlDeleteFunctionTable(unwind.data());if(data)VirtualFree(data,0,MEM_RELEASE);}
    std::uintptr_t base()const{return reinterpret_cast<std::uintptr_t>(data);}
    void call(std::uintptr_t after,std::uintptr_t target) {
        data[after-5]=0xe8;const auto displacement=static_cast<std::int32_t>(target-after);put(data,after-4,displacement);
    }
    void jump(std::uintptr_t at_rva,void* target) {
        const std::array<unsigned char,6> prefix{0xff,0x25,0,0,0,0};
        std::memcpy(data+at_rva,prefix.data(),prefix.size());put(data,at_rva+6,target);
    }
    void thunk(std::uintptr_t after,void* detour) {
        const auto begin=after-19;const auto slot=thunk_count++;
        // sub rsp,38; copy original fifth arg from [rsp+60] to [rsp+20].
        // Shadow space/alignment are valid for BOTH 1/2-arg hooks and dispatch5.
        const std::array<unsigned char,14> prolog{0x48,0x83,0xec,0x38,0x48,0x8b,0x44,0x24,0x60,0x48,0x89,0x44,0x24,0x20};
        std::memcpy(data+begin,prolog.data(),prolog.size());
        const auto stub=0xd00000+slot*32;jump(stub,detour);call(after,stub);
        const std::array<unsigned char,5> epilog{0x48,0x83,0xc4,0x38,0xc3};std::memcpy(data+after,epilog.data(),epilog.size());
        const auto unwind_rva=0xd10000+slot*8;
        // UNWIND_INFO v1, 4-byte prolog, UWOP_ALLOC_SMALL(56). The argument
        // moves are volatile and need no unwind codes. This supports real C++
        // exceptions crossing the executable fixture thunk.
        const std::array<unsigned char,8> info{1,4,1,0,4,0x62,0,0};std::memcpy(data+unwind_rva,info.data(),info.size());
        unwind[slot]={static_cast<DWORD>(begin),static_cast<DWORD>(after+5),static_cast<DWORD>(unwind_rva)};
    }
    void connect(void* free_callback) {
        thunk(0x23cd40,hooks[0].detour);thunk(0x86a7ff,hooks[1].detour);
        thunk(0x240898,hooks[2].detour);thunk(0x240a21,hooks[2].detour);thunk(0x89d0a8,hooks[2].detour);
        thunk(0x86a817,hooks[3].detour);thunk(0x8d3bee,hooks[4].detour);thunk(0x89d0cf,hooks[4].detour);
        thunk(0x11020,hooks[4].detour);thunk(0x11120,hooks[2].detour);thunk(0x11220,hooks[0].detour);
        thunk(0x24005b,hooks[2].detour);thunk(0x23eb68,hooks[2].detour);thunk(0x23eb0f,hooks[2].detour);thunk(0x242057,hooks[2].detour);
        std::sort(unwind.begin(),unwind.begin()+thunk_count,[](const auto& a,const auto& b){return a.BeginAddress<b.BeginAddress;});
        require(RtlAddFunctionTable(unwind.data(),thunk_count,base())!=0,"dynamic thunk unwind registration failed");registered=true;
        jump(0x9b6750,free_callback);FlushInstructionCache(GetCurrentProcess(),data,0xe00000);
    }
    template<class Function>Function caller(std::uintptr_t after)const{return reinterpret_cast<Function>(base()+after-19);}
};
struct Ring {std::uintptr_t array{};std::uint32_t capacity{},begin{},count{};};
struct Queue {
    std::vector<std::uintptr_t> entries;Ring ring;
    Queue(std::initializer_list<std::uintptr_t> values):entries(values){refresh();}
    void refresh(){ring={reinterpret_cast<std::uintptr_t>(entries.data()),static_cast<std::uint32_t>(entries.size()),0,static_cast<std::uint32_t>(entries.size())};}
};
struct Memory {
    enum Kind {request,payload,parent,foreign};
    struct Slot {bool live{};Kind kind{};};
    unsigned char* arena{};std::array<Slot,1024> slots{};
    Memory(){arena=static_cast<unsigned char*>(VirtualAlloc(nullptr,slots.size()*4096,MEM_RESERVE,PAGE_NOACCESS));require(arena!=nullptr,"fixture arena reserve");}
    ~Memory(){if(arena)VirtualFree(arena,0,MEM_RELEASE);}
    void* allocate(Kind kind) {
        for(std::size_t i=0;i<slots.size();++i)if(!slots[i].live) {
            const auto pointer=VirtualAlloc(arena+i*4096,4096,MEM_COMMIT,PAGE_READWRITE);require(pointer!=nullptr,"fixture arena commit");
            slots[i]={true,kind};return pointer;
        }
        throw std::runtime_error("fixture arena full");
    }
    Slot& slot(void* pointer) {
        const auto offset=static_cast<unsigned char*>(pointer)-arena;
        require(offset>=0 && static_cast<std::size_t>(offset)<slots.size()*4096 && offset%4096==0,"wrong fixture allocation pointer");
        return slots[static_cast<std::size_t>(offset)/4096];
    }
    bool live(void* pointer){return slot(pointer).live;}
    void release(void* pointer) {
        auto& value=slot(pointer);require(value.live,"native double free");value.live=false;
        require(VirtualFree(pointer,4096,MEM_DECOMMIT)!=0,"fixture guard-page decommit");
    }
};
struct Native;
Native* active{};
struct Native {
    Image& image;Memory memory;int queue_token{},owner_token{},graphics_token{},context_token{},fifth_token{};
    void* expected_queue{&queue_token};
    std::vector<NativeButtonRequestLayout*> queued;
    unsigned allocations{},assignments{},source_discards{},enqueues{},payload_frees{},request_deletes{},parent_deletes{},foreign_deletes{},serializes{},dispatches{},raw_clears{},typed_clears{};
    bool enqueue_throw{},enqueue_sync_write{},serializer_throw{},dispatch_throw{},clear_throw_after{},typed_throw_after{},delete_throw_after{},free_throw_after{},duplicate_local_delete{};
    bool assign_throw_after{},discard_throw_after{},reuse_after_inner{},delete_request_during_free{};
    bool wrong_source_delete{},nested_unknown_clear{};
    NativeButtonRequestLayout* delete_during_free{};
    std::unique_ptr<NativeButtonReleaseRequest> reused_request;
    explicit Native(Image& value):image(value){require(!active,"nested fake native");active=this;}
    ~Native(){active=nullptr;}
    static void free_buffer(void* pointer) {
        auto& self=*active;(void)native_button_retirement_stats(); // A callback must not inherit the ledger mutex.
        require(self.memory.slot(pointer).kind==Memory::payload,"retirement freed foreign/container buffer");
        ++self.payload_frees;self.memory.release(pointer);
        if(self.delete_request_during_free) {
            self.delete_request_during_free=false;
            self.image.caller<Delete>(0x11020)(self.delete_during_free,72);
        }
        if(self.free_throw_after)throw std::runtime_error("free completed then threw");
    }
    static void native_delete(void* pointer,std::size_t bytes) {
        auto& self=*active;(void)native_button_retirement_stats();
        const auto kind=self.memory.slot(pointer).kind;
        if(kind==Memory::request){require(bytes==16 || bytes==72,"request sized-delete ABI changed");++self.request_deletes;}
        else if(kind==Memory::parent){require(bytes==16 || bytes==208,"parent delete ABI changed");++self.parent_deletes;}
        else {require(kind==Memory::foreign && bytes==16,"foreign delete arguments changed");++self.foreign_deletes;}
        self.memory.release(pointer);if(self.delete_throw_after)throw std::runtime_error("delete completed then threw");
    }
    static void raw_clear(void* pointer) {
        auto& self=*active;++self.raw_clears;const auto copy=at<Ring>(pointer,0);
        if(self.nested_unknown_clear && copy.count) {
            self.nested_unknown_clear=false;Queue nested{at<std::uintptr_t>(reinterpret_cast<void*>(copy.array),0)};
            self.image.caller<Clear>(0x11120)(&nested.ring);put(pointer,0,Ring{});return;
        }
        for(std::uint32_t i=0;i<copy.count;++i) {
            const auto slot=static_cast<std::uint32_t>(copy.begin+i)%copy.capacity;
            const auto child=at<std::uintptr_t>(reinterpret_cast<void*>(copy.array),slot*8);
            self.image.caller<Delete>(self.wrong_source_delete?0x11020:0x8d3bee)(reinterpret_cast<void*>(child),16);
            if(self.duplicate_local_delete && pointer!=&self.queue_token && self.memory.slot(reinterpret_cast<void*>(child)).kind==Memory::request)
                self.image.caller<Delete>(0x8d3bee)(reinterpret_cast<void*>(child),16);
        }
        put(pointer,0,Ring{});
        if(self.clear_throw_after)throw std::runtime_error("raw clear completed then threw");
    }
    static void typed_clear(void* pointer) {
        auto& self=*active;++self.typed_clears;const auto copy=at<Ring>(pointer,0);
        for(std::uint32_t i=0;i<copy.count;++i) {
            const auto child=at<std::uintptr_t>(reinterpret_cast<void*>(copy.array),((copy.begin+i)%copy.capacity)*8);
            self.image.caller<Clear>(0x89d0a8)(reinterpret_cast<void*>(child+0x18));
            if(self.reuse_after_inner) {
                self.reuse_after_inner=false;self.reused_request=self.request({});self.reused_request->submit(&self.queue_token);
            }
            self.image.caller<Delete>(0x89d0cf)(reinterpret_cast<void*>(child),208);
        }
        put(pointer,0,Ring{});if(self.typed_throw_after)throw std::runtime_error("typed clear completed then threw");
    }
    static void serialize(void* message,void* buffer) {
        auto& self=*active;++self.serializes;require(at<unsigned char>(buffer,0x14)<=1,"serializer buffer argument changed");
        require(at<std::uintptr_t>(message,0)==image_base+0xb89760,"serializer parent argument changed");
        if(self.serializer_throw)throw std::runtime_error("serializer failed");
    }
    static void dispatch(void* owner,void* graphics,void* context,void* parent,void* fifth) {
        auto& self=*active;++self.dispatches;
        require(owner==&self.owner_token && graphics==&self.graphics_token && context==&self.context_token && fifth==&self.fifth_token,
                "server dispatcher ABI did not preserve all FIVE arguments");
        require(at<std::uintptr_t>(parent,0)==image_base+0xb89760,"server dispatcher parent is not argument FOUR");
        if(self.dispatch_throw)throw std::runtime_error("server dispatch failed");
    }
    NativeButtonRequestApi source() {
        return {this,image_base+0xb0fec0,
            [](void* context,std::size_t bytes)->void* {auto& self=*static_cast<Native*>(context);require(bytes==72,"source allocate ABI");
                ++self.allocations;(void)native_button_retirement_stats();return self.memory.allocate(Memory::request);},
            [](void* context,NativeButtonString* out,std::string_view source) {auto& self=*static_cast<Native*>(context);++self.assignments;
                if(source.empty())return;require(source.size()<4096,"fixture payload too large");const auto buffer=self.memory.allocate(Memory::payload);
                std::memcpy(buffer,source.data(),source.size());static_cast<char*>(buffer)[source.size()]=0;
                *out={reinterpret_cast<std::uintptr_t>(buffer),static_cast<std::uint32_t>(source.size()),4096};
                if(self.assign_throw_after)throw std::runtime_error("source assigned partial payload then threw");},
            [](void* context,NativeButtonRequestLayout* request) {auto& self=*static_cast<Native*>(context);++self.source_discards;
                if(request->payload.data)free_buffer(reinterpret_cast<void*>(request->payload.data));native_delete(request,72);
                if(self.discard_throw_after)throw std::runtime_error("source discard completed then threw");},
            [](void* context,void* queue,NativeButtonRequestLayout* request) {auto& self=*static_cast<Native*>(context);++self.enqueues;
                require(queue==self.expected_queue,"wrapped enqueue changed queue/context");(void)native_button_retirement_stats();
                self.queued.push_back(request);
                if(self.enqueue_sync_write)self.write(request);
                if(self.enqueue_throw)throw std::runtime_error("source enqueue outcome unknown");}};
    }
    std::unique_ptr<NativeButtonReleaseRequest> request(std::string_view payload="owned payload") {
        auto result=std::make_unique<NativeButtonReleaseRequest>(wrap_native_button_request_api(source()));
        result->prepare({51,2,{3,-4,5},{-.0f,1.25f,-2.5f},true,payload});return result;
    }
    struct Carrier {void* pointer{};Queue children;Carrier(void* p,std::uintptr_t request):pointer(p),children{request}{}};
    Carrier carrier(NativeButtonRequestLayout* request) {
        const auto parent=memory.allocate(Memory::parent);Carrier result{parent,reinterpret_cast<std::uintptr_t>(request)};
        put(parent,0,image_base+0xb89760);put(parent,8,std::uint32_t{3});put(parent,0x18,result.children.ring);return result;
    }
    void observe_write(void* parent,bool known=true,bool write=true) {
        std::array<unsigned char,32> buffer{};buffer[0x14]=write?1:0;
        image.caller<Serializer>(known?0x23cd40:0x11220)(parent,buffer.data());
    }
    void observe_dispatch(void* parent) {image.caller<Dispatch>(0x86a7ff)(&owner_token,&graphics_token,&context_token,parent,&fifth_token);}
    void write(NativeButtonRequestLayout* request,std::uintptr_t clear_return=0x240898) {
        auto parent=carrier(request);observe_write(parent.pointer);Queue outer{reinterpret_cast<std::uintptr_t>(parent.pointer)};
        image.caller<Clear>(clear_return)(&outer.ring);
    }
    void local(NativeButtonRequestLayout* request) {
        auto parent=carrier(request);observe_dispatch(parent.pointer);Queue outer{reinterpret_cast<std::uintptr_t>(parent.pointer)};
        image.caller<Clear>(0x86a817)(&outer.ring);
    }
};
void* original_for(std::uintptr_t rva) {
    if(rva==0x23ca90)return reinterpret_cast<void*>(Native::serialize);
    if(rva==0x855ab0)return reinterpret_cast<void*>(Native::dispatch);
    if(rva==0x8d3bb0)return reinterpret_cast<void*>(Native::raw_clear);
    if(rva==0x89d030)return reinterpret_cast<void*>(Native::typed_clear);
    if(rva==0x95c2ec)return reinterpret_cast<void*>(Native::native_delete);
    throw std::runtime_error("unexpected MH hook target");
}
std::filesystem::path trace_path() {
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
    return std::filesystem::path(path).parent_path()/(L"button-retirement-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".tsv");
}
void guard_case(const std::string& mode) {
    SetEnvironmentVariableW(L"SWVR_HAND_BUTTONS",mode=="disabled"?L"0":L"1");
    if(mode=="disabled" || mode=="hash") {
        const auto result=install_native_button_retirement(1,false,trace_path());
        require(result==(mode=="disabled"?NativeButtonRetirementStatus::disabled:NativeButtonRetirementStatus::unsupported_profile),"SHA/disabled guard touched image");
        require(create_calls==0,"guard created hooks");return;
    }
    Image image;
    if(mode.rfind("prefix",0)==0) {
        const auto index=static_cast<std::size_t>(std::stoi(mode.substr(6)));
        image.data[index<5?hooks[index].rva:index==5?0x9b6750:index==6?0x23ff80:0x23ea20]^=1;
        require(install_native_button_retirement(image.base(),true,trace_path())==NativeButtonRetirementStatus::signature_mismatch,"prefix guard accepted mismatch");
        require(create_calls==0,"prefix mismatch created hooks");return;
    }
    if(mode.rfind("call",0)==0) {
        const auto index=static_cast<std::size_t>(std::stoi(mode.substr(4)));
        const auto offset=Image::returns[index]==0x23cd40?0x23cd3e:Image::returns[index]-5;image.data[offset]^=1;
        require(install_native_button_retirement(image.base(),true,trace_path())==NativeButtonRetirementStatus::signature_mismatch,"CALL guard accepted mismatch");return;
    }
    if(mode=="vtable") {
        put(image.data,0xb89760,image.base()+0x95c2ec);
        require(install_native_button_retirement(image.base(),true,trace_path())==NativeButtonRetirementStatus::signature_mismatch,"carrier vtable guard failed");return;
    }
    if(mode=="io") {require(install_native_button_retirement(image.base(),true,"relative.tsv")==NativeButtonRetirementStatus::io_failure,"relative log path accepted");return;}
    if(mode.rfind("rollback",0)==0) {
        fail_create=static_cast<unsigned>(std::stoul(mode.substr(8)));
        require(install_native_button_retirement(image.base(),true,trace_path())==NativeButtonRetirementStatus::hook_failure,"partial hook install succeeded");
        require(create_calls==fail_create && remove_calls==fail_create-1,"partial hook creation not rolled back");
        require(native_button_retirement_stats().retained_records==0,"failed install changed ownership ledger");return;
    }
    throw std::runtime_error("unknown fixture guard mode");
}
void run_child(const std::string& mode) {
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    const std::wstring argument(mode.begin(),mode.end());std::wstring command=L"\""+std::wstring(executable)+L"\" --guard "+argument;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0,"guard subprocess launch failed");
    const auto wait=WaitForSingleObject(process.hProcess,30000);DWORD exit_code=1;GetExitCodeProcess(process.hProcess,&exit_code);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);require(wait==WAIT_OBJECT_0 && exit_code==0,"guard subprocess failed");
}
void normal_routes_and_synchronous_ownership(Native& native) {
    require(native_button_retirement_stats().retained_records==0,"initial ledger not idle");
    for(const auto route:{0x240898u,0x240a21u}) {
        const auto frees=native.payload_frees,deletes=native.request_deletes;auto request=native.request();
        const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request->address_token());request->submit(&native.queue_token);
        auto parent=native.carrier(pointer);native.observe_write(parent.pointer);
        require(native.memory.live(pointer) && native.payload_frees==frees,"serializer alone retired a request");
        Queue outer{reinterpret_cast<std::uintptr_t>(parent.pointer)};native.image.caller<Clear>(route)(&outer.ring);
        require(!native.memory.live(pointer) && native.payload_frees==frees+1 && native.request_deletes==deletes+1,"WRITE did not retire request and own payload");
        native.image.caller<Clear>(route)(&outer.ring);require(native.request_deletes==deletes+1,"duplicate WRITE callback deleted request again");
    }
    {
        const auto frees=native.payload_frees,deletes=native.request_deletes;auto request=native.request();
        const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request->address_token());request->submit(&native.queue_token);
        native.duplicate_local_delete=true;native.local(pointer);native.duplicate_local_delete=false;
        require(native.payload_frees==frees+1 && native.request_deletes==deletes+1,"localhost duplicated native request deletion or missed payload");
    }
    {
        auto request=native.request();native.enqueue_sync_write=true;native.enqueue_throw=true;
        rejects([&]{request->submit(&native.queue_token);},"sync destroy then throw did not preserve unknown source outcome");
        native.enqueue_sync_write=false;native.enqueue_throw=false;
        require(request->state()==NativeButtonRequestState::ownership_uncertain && native_button_retirement_stats().retained_records==0,
                "wrapper accessed or retained a synchronously retired request after source exception");
    }
    {
        auto request=native.request();const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request->address_token());native.enqueue_throw=true;
        rejects([&]{request->submit(&native.queue_token);},"uncertain enqueue did not throw");native.enqueue_throw=false;
        note_native_button_retirement_reset();native.write(pointer);
        require(native_button_retirement_stats().retained_records==0,"actual parent observation did not resolve uncertain enqueue/reset");
    }
    auto unsubmitted=native.request();unsubmitted->discard();
    require(native.source_discards==1 && native_button_retirement_stats().retained_records==0,"source pre-handoff discard did not release reservation");
    {
        NativeButtonReleaseRequest partial(wrap_native_button_request_api(native.source()));native.assign_throw_after=true;
        rejects([&]{partial.prepare({51,2,{3,4,5},{0,0,0},true,"partial"});},"source partial assignment did not propagate");
        native.assign_throw_after=false;partial.discard();
        require(native_button_retirement_stats().retained_records==0,"partial preparation lost explicit pre-handoff disposal");
    }
    {
        auto old=native.request();const auto old_pointer=old->address_token();old->submit(&native.queue_token);
        native.reuse_after_inner=true;native.local(reinterpret_cast<NativeButtonRequestLayout*>(old_pointer));
        require(native.reused_request && native.reused_request->address_token()==old_pointer &&
                native.memory.live(reinterpret_cast<void*>(old_pointer)),"localhost old ID prevented safe request-address reuse or deleted new request");
        native.write(reinterpret_cast<NativeButtonRequestLayout*>(old_pointer));native.reused_request.reset();
        require(native_button_retirement_stats().retained_records==0,"localhost address reuse lost separate allocation IDs");
    }
}
void source_queue_routes(Native& native) {
    for(const auto route:{0x24005bu,0x23eb68u}) {
        auto request=native.request();const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request->address_token());
        const auto payload=pointer->payload.data;const auto frees=native.payload_frees,deletes=native.request_deletes;
        const auto foreign=native.memory.allocate(Memory::foreign);Queue source{reinterpret_cast<std::uintptr_t>(pointer),reinterpret_cast<std::uintptr_t>(foreign)};
        native.expected_queue=&source.ring;request->submit(&source.ring);note_native_button_retirement_reset();
        native.duplicate_local_delete=true;native.image.caller<Clear>(route)(&source.ring);native.duplicate_local_delete=false;native.expected_queue=&native.queue_token;
        require(!native.memory.live(pointer) && !native.memory.live(reinterpret_cast<void*>(payload)) &&
            native.request_deletes==deletes+1 && native.payload_frees==frees+1 && native_button_retirement_stats().retained_records==0,
            "pending source-Q teardown did not retire only its payload after native request deletion");
        native.image.caller<Clear>(route)(&source.ring);
        require(native.request_deletes==deletes+1 && native.payload_frees==frees+1,"source-Q teardown retired the same allocation twice");
    }
}
void foreign_and_unknown_routes_fail_closed(Native& native) {
    for(const auto mode:{0,1,2,3,4,5,6}) {
        auto request=native.request();const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request->address_token());
        const auto payload=pointer->payload.data;Queue source{reinterpret_cast<std::uintptr_t>(pointer)},other{reinterpret_cast<std::uintptr_t>(pointer)};
        native.expected_queue=&source.ring;request->submit(&source.ring);native.expected_queue=&native.queue_token;
        const auto frees=native.payload_frees,deletes=native.request_deletes;
        if(mode==3){auto carrier=native.carrier(pointer);native.observe_write(carrier.pointer);}
        native.wrong_source_delete=mode==4;native.clear_throw_after=mode==5;native.nested_unknown_clear=mode==6;
        const auto route=mode==0?0x23eb0fu:mode==1?0x242057u:0x24005bu;
        const auto action=[&]{native.image.caller<Clear>(route)(mode==2?&other.ring:&source.ring);};
        if(mode==5)rejects(action,"source clear exception did not propagate");else action();
        native.wrong_source_delete=false;native.clear_throw_after=false;native.nested_unknown_clear=false;
        require(!native.memory.live(pointer) && native.memory.live(reinterpret_cast<void*>(payload)) && native.payload_frees==frees && native.request_deletes==deletes+1,
            "unproven source-Q caller/queue/parent/delete/nesting/return incorrectly freed a payload or deleted twice");
    }
    const auto foreign=native.memory.allocate(Memory::foreign);Queue queue{reinterpret_cast<std::uintptr_t>(foreign)};
    const auto foreign_before=native.foreign_deletes;native.image.caller<Clear>(0x11120)(&queue.ring);
    require(native.foreign_deletes==foreign_before+1,"foreign delete was intercepted");
    for(const auto observation:{0,1}) {
        auto request=native.request();const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request->address_token());
        const auto payload=pointer->payload.data;request->submit(&native.queue_token);auto parent=native.carrier(pointer);
        native.observe_write(parent.pointer,observation!=0,observation==0); // unknown caller OR READ, neither is valid WRITE proof.
        Queue outer{reinterpret_cast<std::uintptr_t>(parent.pointer)};const auto frees=native.payload_frees;
        native.image.caller<Clear>(0x240898)(&outer.ring);
        require(native.payload_frees==frees && native.memory.live(pointer) && native.memory.live(reinterpret_cast<void*>(payload)),
                "unknown/read serializer route guessed retirement");
        native.image.caller<Delete>(0x11020)(pointer,72);
    }
    auto old=native.request();const auto old_pointer=reinterpret_cast<NativeButtonRequestLayout*>(old->address_token());
    const auto old_payload=old_pointer->payload.data;old->submit(&native.queue_token);auto old_parent=native.carrier(old_pointer);
    const auto parent_address=old_parent.pointer;native.observe_write(old_parent.pointer);
    const auto frees=native.payload_frees;native.image.caller<Delete>(0x11020)(old_pointer,72);
    require(native.payload_frees==frees && native.memory.live(reinterpret_cast<void*>(old_payload)),"unknown delete guessed own payload free");
    Queue old_outer{reinterpret_cast<std::uintptr_t>(old_parent.pointer)};native.image.caller<Clear>(0x240898)(&old_outer.ring);
    auto replacement=native.request({});const auto replacement_pointer=reinterpret_cast<NativeButtonRequestLayout*>(replacement->address_token());
    require(replacement_pointer==old_pointer,"fixture did not reuse request address");replacement->submit(&native.queue_token);
    auto next_parent=native.carrier(replacement_pointer);require(next_parent.pointer==parent_address,"fixture did not reuse parent address");
    native.observe_write(next_parent.pointer);Queue next_outer{reinterpret_cast<std::uintptr_t>(next_parent.pointer)};
    native.image.caller<Clear>(0x240898)(&next_outer.ring);
    require(!native.memory.live(replacement_pointer) && native.memory.live(reinterpret_cast<void*>(old_payload)) && native.payload_frees==frees,
            "address reuse changed old quarantine or freed its payload");
}
void exceptions_preserve_uncertainty(Native& native) {
    for(const bool local:{false,true}) {
        auto request=native.request();const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request->address_token());
        const auto payload=pointer->payload.data;request->submit(&native.queue_token);const auto frees=native.payload_frees;
        if(local)native.typed_throw_after=true;else native.clear_throw_after=true;
        rejects([&]{if(local)native.local(pointer);else native.write(pointer);},"clear exception did not unwind executable thunk");
        native.typed_throw_after=false;native.clear_throw_after=false;
        require(native.payload_frees==frees && native.memory.live(reinterpret_cast<void*>(payload)),"failed clear guessed payload retirement");
    }
    for(const bool local:{false,true}) {
        auto request=native.request();const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request->address_token());request->submit(&native.queue_token);
        auto parent=native.carrier(pointer);const auto frees=native.payload_frees;
        if(local){native.dispatch_throw=true;rejects([&]{native.observe_dispatch(parent.pointer);},"dispatch exception lost");native.dispatch_throw=false;}
        else {native.serializer_throw=true;rejects([&]{native.observe_write(parent.pointer);},"serializer exception lost");native.serializer_throw=false;}
        Queue outer{reinterpret_cast<std::uintptr_t>(parent.pointer)};native.image.caller<Clear>(local?0x86a817:0x240898)(&outer.ring);
        require(native.payload_frees==frees,"failed last-reader observation authorized retirement");
    }
    auto request=native.request();const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request->address_token());request->submit(&native.queue_token);
    native.free_throw_after=true;const auto deletes=native.request_deletes;native.write(pointer);native.free_throw_after=false;
    require(native.request_deletes==deletes && native.memory.live(pointer),"throwing free was followed by guessed request delete");
    {
        auto reentrant=native.request();const auto address=reinterpret_cast<NativeButtonRequestLayout*>(reentrant->address_token());
        reentrant->submit(&native.queue_token);native.delete_during_free=address;native.delete_request_during_free=true;
        const auto before=native.request_deletes;native.write(address);
        require(native.request_deletes==before+1 && !native.memory.live(address),"unknown delete during payload free caused a second request delete");
    }
    {
        auto partial=native.request();native.discard_throw_after=true;
        rejects([&]{partial->discard();},"source discard uncertainty was hidden");native.discard_throw_after=false;
        require(partial->state()==NativeButtonRequestState::discard_uncertain,"source discard outcome was guessed");
    }
}
void logging_and_capacity_do_not_forget_ownership(Native& native) {
    for(unsigned i=0;i<10010;++i)note_native_button_retirement_reset();
    require(native_button_retirement_stats().log_rows==10000,"diagnostic log cap not exercised");
    auto working=native.request();const auto working_pointer=reinterpret_cast<NativeButtonRequestLayout*>(working->address_token());working->submit(&native.queue_token);
    const auto retired=native_button_retirement_stats().write_retired;native.write(working_pointer);
    require(native_button_retirement_stats().write_retired==retired+1,"logging cap stopped ownership cleanup");
    std::vector<std::unique_ptr<NativeButtonReleaseRequest>> waiting;
    auto stats=native_button_retirement_stats();const auto available=stats.capacity-stats.retained_records;
    for(std::size_t i=0;i<available;++i){auto request=native.request();request->submit(&native.queue_token);waiting.push_back(std::move(request));}
    const auto allocations=native.allocations;
    rejects([&]{native.request();},"full ledger evicted a live/quarantined record");
    require(native.allocations==allocations,"full ledger rejected only after allocating an untrackable native object");
    stats=native_button_retirement_stats();note_native_button_retirement_reset();
    require(native_button_retirement_stats().retained_records==stats.capacity,"reset forgot queued native ownership");
    Queue source{};for(const auto& request:waiting)source.entries.push_back(request->address_token());source.refresh();
    const auto frees=native.payload_frees;native.image.caller<Clear>(0x11120)(&source.ring);
    require(native.payload_frees==frees && native_button_retirement_stats().retained_records==stats.capacity,
            "unrecognized source teardown guessed string cleanup or forgot quarantine");
    require(native_button_retirement_stats().live_records==0 && native_button_retirement_stats().retained_records>0,
            "fixture did not distinguish quarantine from actual idle");
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID detour,LPVOID* original) {
    ++create_calls;if(fail_create==create_calls)return MH_ERROR_MEMORY_ALLOC;
    const auto rva=reinterpret_cast<std::uintptr_t>(target)-image_base;
    for(auto& hook:hooks)if(hook.rva==rva){hook.detour=detour;*original=original_for(rva);return MH_OK;}
    ++unexpected_hook_calls;return MH_ERROR_UNSUPPORTED_FUNCTION;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID target) {
    ++remove_calls;const auto rva=reinterpret_cast<std::uintptr_t>(target)-image_base;
    for(auto& hook:hooks)if(hook.rva==rva){hook.detour=nullptr;return MH_OK;}
    ++unexpected_hook_calls;return MH_ERROR_NOT_CREATED;
}
int main(int argc,char** argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--guard"){guard_case(argv[2]);return 0;}
        run_child("disabled");run_child("hash");run_child("vtable");run_child("io");
        for(unsigned i=0;i<8;++i)run_child("prefix"+std::to_string(i));
        for(unsigned i=0;i<Image::returns.size();++i)run_child("call"+std::to_string(i));
        for(unsigned i=1;i<=5;++i)run_child("rollback"+std::to_string(i));
        SetEnvironmentVariableW(L"SWVR_HAND_BUTTONS",L"1");Image image;Native native(image);
        const auto trace=trace_path();require(install_native_button_retirement(image.base(),true,trace)==NativeButtonRetirementStatus::installed,"retirement hook install failed");
        require(create_calls==5 && remove_calls==0 && unexpected_hook_calls==0,"retirement installed wrong hook set");
        require(native.allocations==0 && native.payload_frees==0 && native.request_deletes==0,"installation called a native helper");
        image.connect(reinterpret_cast<void*>(Native::free_buffer));
        normal_routes_and_synchronous_ownership(native);source_queue_routes(native);foreign_and_unknown_routes_fail_closed(native);
        exceptions_preserve_uncertainty(native);logging_and_capacity_do_not_forget_ownership(native);
        flush_native_button_retirement();require(remove_calls==0,"live retirement hooks were disabled or removed");
        std::cout<<"native button retirement tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
