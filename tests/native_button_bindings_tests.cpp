#include "swvr/native_button_bindings.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace swvr;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T raw_at(const unsigned char* pointer,std::size_t offset) {
    T value{};std::memcpy(&value,pointer+offset,sizeof(value));return value;
}
template<class F>void rejects(F&& function,const char* message) {
    bool caught{};try{function();}catch(const std::exception&){caught=true;}require(caught,message);
}
struct Prefix {std::uintptr_t rva;std::vector<unsigned char> bytes;};
const std::array<Prefix,7> prefixes{{
    {0x95c6f8,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xeb,0x0f,0x48,0x8b,0xcb,0xe8,0xb1,0xc8,0x05,0x00,0x85,0xc0,0x74,0x13,0x48}},
    {0x17ca10,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xbb,0x20,0x00,0x00,0x00,0x48,0x8b,0xf9,0x3b,0xd3,0x76,0x06,0x03,0xdb}},
    {0x9b6750,{0x48,0x83,0xec,0x28,0x48,0x85,0xc9,0x74,0x0d,0x48,0x83,0xe1,0xf8,0x48,0x8b,0x49,0xf8,0xe8,0xce,0x3d,0xff,0xff,0x48,0x83}},
    {0x95c2ec,{0xe9,0x8f,0xe4,0x7f,0xff}},
    {0x236ed0,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x30,0x4c,0x8b,0xf2,0x48,0x8b,0xf1}},
    {0x231bd0,{0x4c,0x8b,0xc2,0x4c,0x8b,0xc9,0x8b,0x52,0x10,0x41,0x80,0x78,0x14,0x00,0x75,0x11,0x83,0xfa,0x04,0x72,0x2b,0x49,0x8b,0x40}},
    {0x32b470,{0x48,0x89,0x54,0x24,0x10,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0xda,0xc7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,0x48,0x8b,0x01}}
}};
struct Image {
    unsigned char* data{};
    Image() {
        data=static_cast<unsigned char*>(VirtualAlloc(nullptr,0xb80000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        require(data!=nullptr,"VirtualAlloc fixture image failed");
        for(const auto& prefix:prefixes)std::memcpy(data+prefix.rva,prefix.bytes.data(),prefix.bytes.size());
        const auto serializer=base()+0x231bd0;std::memcpy(data+0xb0fec0,&serializer,sizeof(serializer));
        const auto getter=base()+0x32b470;std::memcpy(data+0xb75658+0x2a8,&getter,sizeof(getter));
    }
    ~Image(){if(data)VirtualFree(data,0,MEM_RELEASE);}
    std::uintptr_t base()const{return reinterpret_cast<std::uintptr_t>(data);}
    void tail_jump(std::uintptr_t rva,void* function) {
        // jmp qword ptr [rip+0]; target. No register or stack argument is changed.
        const std::array<unsigned char,6> jump{0xff,0x25,0,0,0,0};
        std::memcpy(data+rva,jump.data(),jump.size());std::memcpy(data+rva+jump.size(),&function,sizeof(function));
        require(FlushInstructionCache(GetCurrentProcess(),data+rva,14)!=0,"fixture instruction-cache flush failed");
    }
};
struct FakeNative;
FakeNative* current{};
struct FakeNative {
    enum ReserveMode {good,throw_before_buffer,throw_after_buffer,inconsistent_capacity} reserve_mode=good;
    enum PushMode {queue_normally,synchronous_destroy,throw_before_push,throw_after_destroy} push_mode=queue_normally;
    enum GetterMode {getter_good,getter_throw_before,getter_throw_after,getter_wrong_return,
                     getter_bad_length,getter_bad_capacity,getter_bad_terminator,getter_changes_held} getter_mode=getter_good;
    std::unordered_set<NativeButtonRequestLayout*> requests;
    std::unordered_set<void*> payloads;
    std::vector<NativeButtonRequestLayout*> pending;
    std::vector<char> calls;
    unsigned allocations{},reserves{},buffer_frees{},deletes{},pushes{},serializers{},getters{};
    std::uint32_t last_reserve_bytes{};
    bool null_allocation{},throw_after_buffer_free{},throw_after_delete{};
    void* expected_button{};void* capture_controller{};
    std::array<float,3> expected_saved_hit{};
    std::string getter_payload{"p\0ayload",7};
    bool verify_captured_push{};
    std::uintptr_t expected_request_vtable{};
    std::uint32_t expected_vehicle{},expected_section{};
    std::array<std::int32_t,3> expected_grid{};
    std::string expected_request_payload;
    int queue_token{};
    FakeNative(){require(!current,"nested fake native fixture");current=this;}
    ~FakeNative() {
        for(auto pointer:payloads)::operator delete[](pointer);
        for(auto pointer:requests)VirtualFree(pointer,0,MEM_RELEASE);
        current=nullptr;
    }
    unsigned count()const{return allocations+reserves+buffer_frees+deletes+pushes+serializers+getters;}
    static void* allocate(std::size_t bytes) {
        auto& self=*current;++self.allocations;self.calls.push_back('a');
        require(bytes==0x48,"allocate RCX did not contain 72 bytes");
        if(self.null_allocation)return nullptr;
        const auto result=static_cast<NativeButtonRequestLayout*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        require(result!=nullptr,"fake request allocation failed");
        std::memset(result,0xa5,bytes);self.requests.insert(result);return result;
    }
    static void reserve(NativeButtonString* output,std::uint32_t bytes) {
        auto& self=*current;++self.reserves;self.calls.push_back('r');self.last_reserve_bytes=bytes;
        require(output && !output->data && !output->length && !output->capacity,"reserve RCX did not point to a zero string");
        require(bytes>=2 && bytes<=65536,"reserve EDX did not include terminating NUL");
        if(self.reserve_mode==throw_before_buffer)throw std::runtime_error("fake reserve before buffer");
        std::uint32_t capacity=32;while(capacity<bytes)capacity*=2;
        auto memory=::operator new[](capacity);self.payloads.insert(memory);
        output->data=reinterpret_cast<std::uintptr_t>(memory);output->capacity=capacity;
        if(self.reserve_mode==throw_after_buffer)throw std::runtime_error("fake reserve after buffer");
        if(self.reserve_mode==inconsistent_capacity)output->capacity=bytes-1;
    }
    static void free_buffer(void* buffer) {
        auto& self=*current;++self.buffer_frees;self.calls.push_back('f');
        require(buffer && self.payloads.erase(buffer)==1,"free RCX was not the native payload pointer");
        ::operator delete[](buffer);
        if(self.throw_after_buffer_free)throw std::runtime_error("fake free completed then threw");
    }
    static void sized_delete(void* pointer,std::size_t bytes) {
        auto& self=*current;++self.deletes;self.calls.push_back('d');
        require(bytes==0x48,"sized delete RDX did not contain 72 bytes");
        require(self.requests.erase(static_cast<NativeButtonRequestLayout*>(pointer))==1,"sized delete RCX was not the native request pointer");
        require(VirtualFree(pointer,0,MEM_RELEASE)!=0,"fake request release failed");
        if(self.throw_after_delete)throw std::runtime_error("fake delete completed then threw");
    }
    static void push(void* queue,void* pointer) {
        auto& self=*current;++self.pushes;self.calls.push_back('p');
        require(queue==&self.queue_token,"push RCX changed the live request queue");
        const auto request=static_cast<NativeButtonRequestLayout*>(pointer);
        require(self.requests.contains(request),"push RDX changed the native request pointer");
        require(request->type==10 && request->pressed==0,"bindings sent a non-release request");
        if(self.verify_captured_push) {
            std::array<unsigned char,72> bytes{};std::memcpy(bytes.data(),request,bytes.size());
            require(raw_at<std::uintptr_t>(bytes.data(),0)==self.expected_request_vtable &&
                    raw_at<std::uint32_t>(bytes.data(),8)==10 && raw_at<std::uint32_t>(bytes.data(),0xc)==0,
                    "combined capture request header bytes changed");
            require(raw_at<std::uint32_t>(bytes.data(),0x10)==self.expected_vehicle &&
                    raw_at<std::uint32_t>(bytes.data(),0x14)==self.expected_section &&
                    raw_at<std::array<std::int32_t,3>>(bytes.data(),0x18)==self.expected_grid,
                    "combined capture request vehicle/section/grid bytes changed");
            require(bytes[0x24]==0 && bytes[0x34]==1 &&
                    std::memcmp(bytes.data()+0x28,self.expected_saved_hit.data(),sizeof(self.expected_saved_hit))==0,
                    "combined capture request used hover or wrong release/skip flags");
            for(const auto offset:{0x25,0x26,0x27,0x35,0x36,0x37})require(bytes[offset]==0,"combined request padding changed");
            const auto payload=raw_at<NativeButtonString>(bytes.data(),0x38);
            require(payload.length==self.expected_request_payload.size() && payload.capacity>payload.length &&
                    std::memcmp(reinterpret_cast<const void*>(payload.data),self.expected_request_payload.data(),payload.length)==0 &&
                    reinterpret_cast<const char*>(payload.data)[payload.length]==0,
                    "combined request payload changed or aliases released capture data");
        }
        if(self.push_mode==throw_before_push)throw std::runtime_error("fake unknown push outcome");
        if(self.push_mode==synchronous_destroy || self.push_mode==throw_after_destroy) {
            if(request->payload.data)free_buffer(reinterpret_cast<void*>(request->payload.data));
            sized_delete(request,0x48); // Mapping is now absent; later dereference would fault.
        } else self.pending.push_back(request);
        if(self.push_mode==throw_after_destroy)throw std::runtime_error("fake push destroyed request then threw");
    }
    static void serialize(void*,void*) {
        ++current->serializers;throw std::runtime_error("serializer was called as a destructor");
    }
    static NativeButtonString* getter(void* button,NativeButtonString* output,const float* hit) {
        auto& self=*current;++self.getters;self.calls.push_back('g');
        require(button==self.expected_button,"payload getter RCX changed current held button");
        require(output && !output->data && !output->length && !output->capacity,"payload getter RDX was not a zero temporary");
        require(hit && std::memcmp(hit,self.expected_saved_hit.data(),sizeof(self.expected_saved_hit))==0,
                "payload getter R8 used new hover hit or changed saved hit bits");
        if(self.getter_mode==getter_throw_before)throw std::runtime_error("getter before temporary output");
        if(!self.getter_payload.empty()) {
            const auto bytes=self.getter_payload.size()+1;
            auto memory=static_cast<char*>(::operator new[](bytes));self.payloads.insert(memory);
            std::memcpy(memory,self.getter_payload.data(),self.getter_payload.size());memory[bytes-1]=0;
            output->data=reinterpret_cast<std::uintptr_t>(memory);
            output->length=static_cast<std::uint32_t>(self.getter_payload.size());output->capacity=static_cast<std::uint32_t>(bytes);
            if(self.getter_mode==getter_bad_length)output->length=65536;
            if(self.getter_mode==getter_bad_capacity)output->capacity=output->length;
            if(self.getter_mode==getter_bad_terminator)memory[bytes-1]='!';
        }
        if(self.getter_mode==getter_throw_after)throw std::runtime_error("getter after temporary allocation");
        if(self.getter_mode==getter_changes_held) {
            const std::uintptr_t empty{};std::memcpy(static_cast<unsigned char*>(self.capture_controller)+0x558,&empty,sizeof(empty));
        }
        if(self.getter_mode==getter_wrong_return)return nullptr;
        return output;
    }
    void connect(Image& image) {
        image.tail_jump(0x95c6f8,reinterpret_cast<void*>(allocate));
        image.tail_jump(0x17ca10,reinterpret_cast<void*>(reserve));
        image.tail_jump(0x9b6750,reinterpret_cast<void*>(free_buffer));
        image.tail_jump(0x95c2ec,reinterpret_cast<void*>(sized_delete));
        image.tail_jump(0x236ed0,reinterpret_cast<void*>(push));
        image.tail_jump(0x231bd0,reinterpret_cast<void*>(serialize));
        image.tail_jump(0x32b470,reinterpret_cast<void*>(getter));
    }
};
NativeButtonReleaseData release_data(std::string_view payload="button payload") {
    return {51,2,{3,-4,5},{-.0f,1.25f,-2.5f},true,payload};
}
void guarded_binding_has_no_native_callbacks() {
    FakeNative native;
    bool sha_rejected{};
    try {NativeButtonRequestBindings invalid(1,false);}
    catch(const std::invalid_argument&){sha_rejected=true;}
    require(sha_rejected,"false SHA reached mapping verification or accepted an invalid base");
    rejects([]{NativeButtonRequestBindings invalid(0,true);},"null image accepted");
    rejects([]{NativeButtonRequestBindings invalid(1,true);},"unmapped image accepted");
    rejects([]{NativeButtonRequestBindings invalid(std::numeric_limits<std::uintptr_t>::max(),true);},"overflowing image accepted");
    for(std::size_t index=0;index<prefixes.size();++index) {
        Image image;image.data[prefixes[index].rva]^=0x01;
        rejects([&]{NativeButtonRequestBindings invalid(image.base(),true);},"mismatched native entry prefix accepted");
    }
    {
        Image image;const auto wrong=image.base()+0x95c2ec;
        std::memcpy(image.data+0xb0fec0,&wrong,sizeof(wrong));
        rejects([&]{NativeButtonRequestBindings invalid(image.base(),true);},"serializer vtable identity was not checked");
    }
    {
        Image image;const auto wrong=image.base()+0x231bd0;
        std::memcpy(image.data+0xb75658+0x2a8,&wrong,sizeof(wrong));
        rejects([&]{NativeButtonRequestBindings invalid(image.base(),true);},"button payload getter slot identity was not checked");
    }
    {
        Image image;DWORD prior{};
        require(VirtualProtect(image.data+0x17ca10,24,PAGE_READWRITE,&prior)!=0,"fixture page protection");
        rejects([&]{NativeButtonRequestBindings invalid(image.base(),true);},"non-executable native helper accepted");
    }
    {
        Image image;NativeButtonRequestBindings bindings(image.base(),true);
        const auto api=bindings.api();
        require(api.context && api.context==bindings.api().context && api.request_vtable==image.base()+0xb0fec0,
                "API context/vtable is not stable");
    }
    require(native.count()==0,"constructor/destructor invoked a native helper");
}
void real_callback_abi_copy_and_explicit_cleanup() {
    FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
    const auto api=bindings.api();std::string source{"a\0bc",4};const auto expected=source;
    NativeButtonReleaseRequest request(api);request.prepare(release_data(source));
    const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request.address_token());
    require(pointer->vtable==api.request_vtable && pointer->payload.length==4 && pointer->payload.capacity==32 &&
            native.last_reserve_bytes==5,"native reserve byte capacity ABI changed");
    source.assign(30,'X');
    require(std::memcmp(reinterpret_cast<const void*>(pointer->payload.data),expected.data(),expected.size())==0 &&
            reinterpret_cast<const char*>(pointer->payload.data)[4]==0,"payload copy aliases input or lost embedded NUL");
    rejects([&]{api.assign_payload(api.context,&pointer->payload,"new");},"occupied string was overwritten");
    rejects([&]{api.assign_payload(api.context,nullptr,"new");},"null string destination accepted");
    NativeButtonString foreign{};
    rejects([&]{api.assign_payload(api.context,&foreign,"new");},"foreign string passed to native reserve");
    NativeButtonRequestLayout foreign_request{};
    rejects([&]{api.destroy_unsubmitted(api.context,&foreign_request);},"foreign request passed to native cleanup");
    rejects([&]{api.destroy_unsubmitted(api.context,nullptr);},"null request passed to native cleanup");
    rejects([&]{api.enqueue(api.context,&native.queue_token,nullptr);},"null request passed to native push");
    rejects([&]{api.allocate(api.context,16);},"wrong request allocation size accepted");
    rejects([&]{api.enqueue(api.context,nullptr,pointer);},"null queue accepted");
    require(native.pushes==0 && native.reserves==1,"invalid callback touched native helpers");
    request.discard();
    require(native.calls==std::vector<char>({'a','r','f','d'}) && native.requests.empty() && native.payloads.empty(),
            "pre-handoff cleanup did not free payload then 72-byte object exactly once");
    rejects([&]{api.destroy_unsubmitted(api.context,pointer);},"bindings retried native cleanup after release");
    require(native.deletes==1 && native.serializers==0,"cleanup used serializer or ran twice");
}
void empty_payload_and_null_allocation() {
    FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
    {
        NativeButtonReleaseRequest request(bindings.api());request.prepare(release_data({}));
        const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request.address_token());
        require(!pointer->payload.data && !pointer->payload.length && !pointer->payload.capacity && native.reserves==0,
                "empty payload called reserve or invented a buffer");
        request.discard();require(native.buffer_frees==0 && native.deletes==1,"empty payload cleanup ABI");
    }
    native.null_allocation=true;
    NativeButtonReleaseRequest request(bindings.api());rejects([&]{request.prepare(release_data());},"null allocator result accepted");
    request.discard();require(native.deletes==1 && native.reserves==0,"null allocator result called native payload cleanup");
}
void maximum_payload_passes_capacity_without_truncation() {
    FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
    const std::string source(65535,'Q');NativeButtonReleaseRequest request(bindings.api());
    request.prepare(release_data(source));
    const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request.address_token());
    require(native.last_reserve_bytes==65536 && pointer->payload.length==65535 && pointer->payload.capacity==65536,
            "maximum wire payload truncated reserve capacity or length");
    const auto bytes=reinterpret_cast<const char*>(pointer->payload.data);
    require(bytes[0]=='Q' && bytes[65534]=='Q' && bytes[65535]==0,"maximum payload bytes/terminator changed");
    request.discard();require(native.requests.empty() && native.payloads.empty(),"maximum payload cleanup failed");
}
void partial_reserve_and_cleanup_failure_are_not_retried() {
    for(const auto mode:{FakeNative::throw_before_buffer,FakeNative::throw_after_buffer,FakeNative::inconsistent_capacity}) {
        FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);native.reserve_mode=mode;
        NativeButtonReleaseRequest request(bindings.api());rejects([&]{request.prepare(release_data());},"partial reserve was accepted");
        request.discard();
        require(native.deletes==1 && native.requests.empty() && native.payloads.empty(),"partial reserve leaked native allocations");
        require(native.buffer_frees==(mode==FakeNative::throw_before_buffer?0u:1u),"partial reserve freed the wrong payload");
    }
    for(const bool fail_buffer:{true,false}) {
        FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
        const auto api=bindings.api();NativeButtonReleaseRequest request(api);request.prepare(release_data());
        const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request.address_token());
        native.throw_after_buffer_free=fail_buffer;native.throw_after_delete=!fail_buffer;
        rejects([&]{request.discard();},"partial native cleanup did not report uncertainty");
        require(request.state()==NativeButtonRequestState::discard_uncertain,"partial native cleanup lost uncertainty state");
        rejects([&]{api.destroy_unsubmitted(api.context,pointer);},"uncertain cleanup was retried by bindings");
        require(native.buffer_frees==1 && native.deletes==(fail_buffer?0u:1u),"partial cleanup double-free or retry");
    }
}
void native_queue_handoff_never_allows_late_discard_or_dereference() {
    for(const auto mode:{FakeNative::queue_normally,FakeNative::synchronous_destroy,FakeNative::throw_before_push,FakeNative::throw_after_destroy}) {
        FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);native.push_mode=mode;
        const auto api=bindings.api();NativeButtonReleaseRequest request(api);request.prepare(release_data());
        const auto pointer=reinterpret_cast<NativeButtonRequestLayout*>(request.address_token());
        if(mode==FakeNative::throw_before_push || mode==FakeNative::throw_after_destroy) {
            rejects([&]{request.submit(&native.queue_token);},"throwing native push appeared transferred");
            require(request.state()==NativeButtonRequestState::ownership_uncertain,"throwing native push lost uncertainty");
        } else {
            request.submit(&native.queue_token);require(request.state()==NativeButtonRequestState::transferred,"native push transfer state");
        }
        const auto count=native.count();
        rejects([&]{api.destroy_unsubmitted(api.context,pointer);},"post-handoff request was destroyed by bindings");
        rejects([&]{api.enqueue(api.context,&native.queue_token,pointer);},"post-handoff request was enqueued twice");
        require(native.count()==count && native.pushes==1 && native.serializers==0,"post-handoff callback touched native request");
    }
}
void destructors_do_not_call_native_helpers() {
    FakeNative native;Image image;unsigned count{};
    {
        NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
        {
            NativeButtonReleaseRequest request(bindings.api());request.prepare(release_data());count=native.count();
        }
        require(native.count()==count,"builder destructor called native helpers");
    }
    require(native.count()==count && native.requests.size()==1 && native.payloads.size()==1,
            "bindings destructor attempted implicit native cleanup");
    // Fixture owns its fake allocations and removes them after this assertion.
}
template<class T>void store(void* pointer,std::size_t offset,const T& value) {
    std::memcpy(static_cast<unsigned char*>(pointer)+offset,&value,sizeof(value));
}
struct CaptureFixture {
    std::array<unsigned char,0x600> controller{};
    std::array<unsigned char,0x100> button{};
    std::array<unsigned char,0xe00> definition{};
    std::array<unsigned char,0x300> section{};
    std::array<unsigned char,0x200> vehicle{};
    std::array<std::uintptr_t,4> ring{};
    const std::array<std::int32_t,3> grid{std::numeric_limits<std::int32_t>::min(),17,std::numeric_limits<std::int32_t>::max()};
    const std::array<float,3> saved_hit{-.0f,1.25f,-2.5f};
    explicit CaptureFixture(FakeNative& native,const Image& image) {
        controller.fill(0x6a);button.fill(0x71);definition.fill(0x82);section.fill(0x93);vehicle.fill(0xa4);
        store(controller.data(),0x558,reinterpret_cast<std::uintptr_t>(button.data()));
        store(controller.data(),0x568,saved_hit);
        store(controller.data(),0x528,std::array<float,3>{99,98,97}); // Must never replace saved hit.
        store(button.data(),0,image.base()+0xb75658);store(button.data(),0x18,grid);
        store(button.data(),0x28,reinterpret_cast<std::uintptr_t>(section.data()));
        store(button.data(),0x58,reinterpret_cast<std::uintptr_t>(definition.data()));
        store(definition.data(),0xda4,std::uint32_t{0});
        store(section.data(),0x250,reinterpret_cast<std::uintptr_t>(vehicle.data()));
        ring={reinterpret_cast<std::uintptr_t>(section.data()),0x100,0x200,0x300};
        store(vehicle.data(),0,reinterpret_cast<std::uintptr_t>(ring.data()));
        store(vehicle.data(),8,std::uint32_t{4});store(vehicle.data(),0xc,std::uint32_t{3});store(vehicle.data(),0x10,std::uint32_t{3});
        store(vehicle.data(),0x178,std::uint32_t{0xfedcba98});
        native.expected_button=button.data();native.expected_saved_hit=saved_hit;native.capture_controller=controller.data();
    }
    NativeButtonCaptureScope scope() {return {17,0x445566,controller.data(),true};}
    HandEventCertificate accepted() {
        HandEventCertificate result{};result.selected_sequence=41;result.pinch_gesture_id=13;
        result.scene_generation=17;result.owner_token=0x445566;
        result.controller_token=reinterpret_cast<std::uintptr_t>(controller.data());
        result.target_token=reinterpret_cast<std::uintptr_t>(button.data());return result;
    }
};
void capture_context_and_held_mismatch_do_not_dereference_history() {
    FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
    CaptureFixture fixture(native,image);auto scope=fixture.scope();auto accepted=fixture.accepted();
    auto invalid=scope;invalid.controller_live=reinterpret_cast<void*>(1);
    invalid.recognized_live_scope=false;
    require(bindings.capture_release(invalid,accepted).status==NativeButtonCaptureStatus::unrecognized_scope,
            "capture accepted an unrecognized scope");
    invalid=scope;invalid.controller_live=reinterpret_cast<void*>(1);
    require(bindings.capture_release(invalid,accepted).status==NativeButtonCaptureStatus::context_mismatch,
            "capture dereferenced mismatched controller token");
    invalid=scope;++invalid.scene_generation;
    require(bindings.capture_release(invalid,accepted).status==NativeButtonCaptureStatus::context_mismatch,
            "capture accepted mismatched scene generation");
    invalid=scope;++invalid.owner_token;
    require(bindings.capture_release(invalid,accepted).status==NativeButtonCaptureStatus::context_mismatch,
            "capture accepted mismatched owner token");
    accepted.target_token=1;
    require(bindings.capture_release(scope,accepted).status==NativeButtonCaptureStatus::held_mismatch,
            "capture dereferenced historical held token");
    accepted=fixture.accepted();store(fixture.controller.data(),0x558,std::uintptr_t{1});
    require(bindings.capture_release(scope,accepted).status==NativeButtonCaptureStatus::held_mismatch,
            "capture dereferenced a mismatched current held pointer");
    require(native.count()==0,"context/held mismatch called a native helper");
}
void capture_copies_ring_grid_saved_hit_and_payload() {
    FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
    CaptureFixture fixture(native,image);const auto before=fixture.controller;
    const auto payload=native.getter_payload;const auto result=bindings.capture_release(fixture.scope(),fixture.accepted());
    require(static_cast<bool>(result) && result.status==NativeButtonCaptureStatus::captured,"valid held capture failed");
    require(result.data.vehicle_id==0xfedcba98 && result.data.section_index==1 && result.data.component_grid==fixture.grid,
            "capture changed vehicle/ring logical index/grid coordinates");
    require(std::memcmp(result.data.saved_local_hit.data(),fixture.saved_hit.data(),sizeof(fixture.saved_hit))==0,
            "capture changed saved hit bits or used hover hit");
    native.getter_payload.assign(40,'Q');
    require(result.data.payload==payload && native.payloads.empty() && native.getters==1 && native.buffer_frees==1,
            "capture did not own payload after freeing native temporary");
    require(fixture.controller==before && native.pushes==0 && native.allocations==0 && native.deletes==0,
            "capture mutated controller or emitted/cleaned a request");
    const auto view=result.data.release_data(true);
    require(view.payload==payload && view.skip_on_button_press && view.saved_local_hit==fixture.saved_hit,
            "captured data did not produce a borrowed builder input");
    // Native unsigned begin+i wrap is intentional and distinct from pointer overflow.
    store(fixture.vehicle.data(),0xc,std::numeric_limits<std::uint32_t>::max());
    store(fixture.definition.data(),0xda4,std::uint32_t{1});
    const auto wrapped=bindings.capture_release(fixture.scope(),fixture.accepted());
    require(static_cast<bool>(wrapped) && wrapped.data.section_index==1,"capture changed native uint32 ring wrap or rejected toggle type");
    native.getter_payload.clear();
    const auto empty=bindings.capture_release(fixture.scope(),fixture.accepted());
    require(static_cast<bool>(empty) && empty.data.payload.empty() && native.buffer_frees==2,
            "empty getter payload was rejected or freed a nonexistent buffer");
    native.getter_payload.assign(65535,'Z');
    const auto maximum=bindings.capture_release(fixture.scope(),fixture.accepted());
    require(static_cast<bool>(maximum) && maximum.data.payload==native.getter_payload && native.buffer_frees==3,
            "maximum valid captured wire payload was rejected or truncated");
    store(fixture.definition.data(),0xda4,std::uint32_t{4});
    require(static_cast<bool>(bindings.capture_release(fixture.scope(),fixture.accepted())),"capture rejected lever type 4");
}
void capture_rejects_bad_ring_class_and_type_before_getter() {
    FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
    CaptureFixture fixture(native,image);
    const auto capture=[&]{return bindings.capture_release(fixture.scope(),fixture.accepted()).status;};
    store(fixture.button.data(),0,image.base()+0xb75679);
    require(capture()==NativeButtonCaptureStatus::wrong_button_class,"capture accepted wrong component class");
    store(fixture.button.data(),0,image.base()+0xb75658);
    store(fixture.definition.data(),0xda4,std::uint32_t{5});
    require(capture()==NativeButtonCaptureStatus::unsupported_button_type,"capture accepted keypad type");
    store(fixture.definition.data(),0xda4,std::uint32_t{0});
    store(fixture.button.data(),0x58,std::uintptr_t{0});
    require(capture()==NativeButtonCaptureStatus::unreadable_native_memory,"capture accepted null definition");
    store(fixture.button.data(),0x58,reinterpret_cast<std::uintptr_t>(fixture.definition.data()));
    store(fixture.button.data(),0x28,std::uintptr_t{0});
    require(capture()==NativeButtonCaptureStatus::unreadable_native_memory,"capture accepted null current section");
    store(fixture.button.data(),0x28,reinterpret_cast<std::uintptr_t>(fixture.section.data()));
    store(fixture.vehicle.data(),0x10,std::uint32_t{5});
    require(capture()==NativeButtonCaptureStatus::invalid_ring,"capture accepted count greater than capacity");
    store(fixture.vehicle.data(),0x10,std::uint32_t{3});
    store(fixture.vehicle.data(),0,std::uintptr_t{0});
    require(capture()==NativeButtonCaptureStatus::invalid_ring,"capture accepted null ring array");
    store(fixture.vehicle.data(),0,std::numeric_limits<std::uintptr_t>::max()-3);
    require(capture()==NativeButtonCaptureStatus::invalid_ring,"capture overflowed ring array address");
    store(fixture.vehicle.data(),0,reinterpret_cast<std::uintptr_t>(fixture.ring.data()));
    store(fixture.vehicle.data(),8,std::uint32_t{0});
    require(capture()==NativeButtonCaptureStatus::invalid_ring,"capture accepted zero ring capacity");
    store(fixture.vehicle.data(),8,std::uint32_t{4});
    fixture.ring[0]=0x9999;
    require(capture()==NativeButtonCaptureStatus::section_not_found,"capture defaulted missing section to index zero");
    fixture.ring[0]=reinterpret_cast<std::uintptr_t>(fixture.section.data());
    store(fixture.controller.data(),0x568,std::array<float,3>{0,std::numeric_limits<float>::infinity(),0});
    require(capture()==NativeButtonCaptureStatus::nonfinite_saved_hit,"capture accepted nonfinite saved hit");
    require(native.count()==0,"invalid class/type/ring called a payload helper");
}
void capture_getter_failure_always_frees_temporary_once() {
    for(const auto mode:{FakeNative::getter_throw_before,FakeNative::getter_throw_after,FakeNative::getter_wrong_return,
                         FakeNative::getter_bad_length,FakeNative::getter_bad_capacity,FakeNative::getter_bad_terminator,
                         FakeNative::getter_changes_held}) {
        FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
        CaptureFixture fixture(native,image);const auto before=fixture.controller;native.getter_mode=mode;
        const auto result=bindings.capture_release(fixture.scope(),fixture.accepted());
        auto expected=NativeButtonCaptureStatus::invalid_payload;
        if(mode==FakeNative::getter_throw_before || mode==FakeNative::getter_throw_after)expected=NativeButtonCaptureStatus::getter_failed;
        if(mode==FakeNative::getter_wrong_return)expected=NativeButtonCaptureStatus::getter_return_mismatch;
        if(mode==FakeNative::getter_changes_held)expected=NativeButtonCaptureStatus::held_changed;
        require(!result && result.status==expected && result.data.payload.empty(),"getter failure returned captured data or wrong status");
        require(native.getters==1 && native.buffer_frees==(mode==FakeNative::getter_throw_before?0u:1u) && native.payloads.empty(),
                "getter partial/failure temporary was leaked or freed twice");
        if(mode!=FakeNative::getter_changes_held)require(fixture.controller==before,"capture failure mutated native controller bytes");
        require(native.pushes==0 && native.allocations==0 && native.deletes==0,"capture failure emitted or disposed request");
    }
    FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
    CaptureFixture fixture(native,image);native.throw_after_buffer_free=true;
    const auto result=bindings.capture_release(fixture.scope(),fixture.accepted());
    require(!result && result.status==NativeButtonCaptureStatus::payload_cleanup_failed && native.buffer_frees==1 && native.payloads.empty(),
            "throwing native temporary cleanup was retried or returned usable captured data");
}
void capture_builder_submit_is_connected_through_one_api() {
    FakeNative native;Image image;NativeButtonRequestBindings bindings(image.base(),true);native.connect(image);
    CaptureFixture fixture(native,image);const auto before=fixture.controller;
    auto captured=bindings.capture_release(fixture.scope(),fixture.accepted());
    require(static_cast<bool>(captured),"combined fixture capture failed");
    native.verify_captured_push=true;native.expected_request_vtable=bindings.api().request_vtable;
    native.expected_vehicle=0xfedcba98;native.expected_section=1;native.expected_grid=fixture.grid;
    native.expected_request_payload=native.getter_payload;
    native.getter_payload.assign(50,'X'); // Captured storage must already be independent.
    NativeButtonReleaseRequest request(bindings.api());request.prepare(captured.data.release_data(true));
    captured.data.payload.assign(60,'Y'); // Native request storage must also be independent.
    request.submit(&native.queue_token); // Fake push checks the raw 72-byte layout.
    require(request.state()==NativeButtonRequestState::transferred && native.pending.size()==1 &&
            native.getters==1 && native.buffer_frees==1 && native.allocations==1 && native.reserves==1 && native.pushes==1,
            "combined capture/builder/push callback count or ownership changed");
    require(fixture.controller==before && native.serializers==0,"combined release capture mutated C or called serializer");
    // No claim about network retirement: the fake queue now owns this request.
}
}
int main() {
    try {
        guarded_binding_has_no_native_callbacks();real_callback_abi_copy_and_explicit_cleanup();
        empty_payload_and_null_allocation();maximum_payload_passes_capacity_without_truncation();
        partial_reserve_and_cleanup_failure_are_not_retried();
        native_queue_handoff_never_allows_late_discard_or_dereference();destructors_do_not_call_native_helpers();
        capture_context_and_held_mismatch_do_not_dereference_history();capture_copies_ring_grid_saved_hit_and_payload();
        capture_rejects_bad_ring_class_and_type_before_getter();capture_getter_failure_always_frees_temporary_once();
        capture_builder_submit_is_connected_through_one_api();
        std::cout<<"native button bindings tests passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
