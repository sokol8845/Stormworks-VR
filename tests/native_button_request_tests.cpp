#include "swvr/native_button_request.hpp"
#include <algorithm>
#include <array>
#include <bit>
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
template<class F>void rejects(F&& fn,const char* message) {
    bool caught{};try{fn();}catch(const std::exception&){caught=true;}require(caught,message);
}
struct FakeNative {
    enum Assignment { good,throw_before_payload,throw_after_payload,bad_length,bad_capacity,bad_terminator,bad_copy } assignment=good;
    enum Submission { queued,synchronous_destroy,throw_before_enqueue,throw_after_enqueue,throw_after_destroy } submission=queued;
    std::unordered_set<NativeButtonRequestLayout*> requests;
    std::unordered_set<char*> payloads;
    std::vector<NativeButtonRequestLayout*> pending;
    unsigned allocations{},assignments{},disposals{},submissions{};
    bool allocation_null{},throw_after_dispose{};
    int queue_token{};
    static constexpr std::uintptr_t vtable=0x140b0fff0;
    ~FakeNative(){while(!requests.empty())destroy(*requests.begin());}
    void destroy(NativeButtonRequestLayout* request) {
        require(requests.erase(request)==1,"Native object freed twice or by wrong allocator");
        const auto payload=reinterpret_cast<char*>(request->payload.data);
        if(payload){require(payloads.erase(payload)==1,"Native payload freed twice or by wrong allocator");delete[] payload;}
        ++disposals;::operator delete(request);
    }
    NativeButtonRequestApi api() {
        return {this,vtable,
            [](void* context,std::size_t bytes)->void* {
                auto& f=*static_cast<FakeNative*>(context);++f.allocations;
                require(bytes==0x48,"Wrong native request allocation size");
                if(f.allocation_null)return nullptr;
                auto p=static_cast<NativeButtonRequestLayout*>(::operator new(bytes));
                std::memset(p,0xa5,bytes);f.requests.insert(p);return p;
            },
            [](void* context,NativeButtonString* output,std::string_view input) {
                auto& f=*static_cast<FakeNative*>(context);++f.assignments;
                require(output->data==0 && output->length==0 && output->capacity==0,"Native string was not initialized");
                if(f.assignment==throw_before_payload)throw std::runtime_error("assign before allocation");
                if(input.empty())return;
                auto bytes=new char[input.size()+1];f.payloads.insert(bytes);
                std::memcpy(bytes,input.data(),input.size());bytes[input.size()]=0;
                *output={reinterpret_cast<std::uintptr_t>(bytes),static_cast<std::uint32_t>(input.size()),static_cast<std::uint32_t>(input.size()+1)};
                if(f.assignment==throw_after_payload)throw std::runtime_error("assign after allocation");
                if(f.assignment==bad_length)++output->length;
                if(f.assignment==bad_capacity)output->capacity=output->length;
                if(f.assignment==bad_terminator)bytes[input.size()]='!';
                if(f.assignment==bad_copy)bytes[0]^=1;
            },
            [](void* context,NativeButtonRequestLayout* request) {
                auto& f=*static_cast<FakeNative*>(context);f.destroy(request);
                if(f.throw_after_dispose)throw std::runtime_error("destroy completed then threw");
            },
            [](void* context,void* queue,NativeButtonRequestLayout* request) {
                auto& f=*static_cast<FakeNative*>(context);++f.submissions;
                require(queue==&f.queue_token,"Native queue argument changed");
                require(f.requests.contains(request),"Enqueued a nonnative request");
                if(f.submission==throw_before_enqueue)throw std::runtime_error("push unknown outcome before queue");
                if(f.submission==synchronous_destroy || f.submission==throw_after_destroy)f.destroy(request);
                else f.pending.push_back(request);
                if(f.submission==throw_after_enqueue || f.submission==throw_after_destroy)throw std::runtime_error("push unknown outcome after handoff");
            }};
    }
};
NativeButtonReleaseData release_data(std::string_view payload="button label") {
    return {0xffffffff,0,{std::numeric_limits<std::int32_t>::min(),17,std::numeric_limits<std::int32_t>::max()},
            {-.0f,1.25f,-2.5f},true,payload};
}
template<class T>T at(const void* object,std::size_t offset) {
    T out{};std::memcpy(&out,static_cast<const unsigned char*>(object)+offset,sizeof(out));return out;
}
void layout_payload_and_explicit_discard() {
    FakeNative f;std::string payload{"a\0bc",4};const auto expected=payload;
    NativeButtonReleaseRequest request(f.api());
    require(request.state()==NativeButtonRequestState::empty && request.address_token()==0,"Initial request state");
    request.prepare(release_data(payload));const auto p=reinterpret_cast<void*>(request.address_token());
    require(request.state()==NativeButtonRequestState::prepared && f.allocations==1 && f.assignments==1,"Preparation did not use native API exactly once");
    require(at<std::uintptr_t>(p,0)==FakeNative::vtable && at<std::uint32_t>(p,8)==10 && at<std::uint32_t>(p,0xc)==0,"Wrong native header");
    require(at<std::uint32_t>(p,0x10)==0xffffffff && at<std::uint32_t>(p,0x14)==0,"Wrong vehicle/section");
    require(at<std::array<std::int32_t,3>>(p,0x18)==release_data().component_grid,"Grid coordinates were converted or truncated");
    require(at<unsigned char>(p,0x24)==0 && at<unsigned char>(p,0x34)==1,"Release/skip flags changed");
    for(const auto offset:{0x25,0x26,0x27,0x35,0x36,0x37})require(at<unsigned char>(p,offset)==0,"Native padding was not cleared");
    const auto hit=at<std::array<float,3>>(p,0x28);
    require(std::bit_cast<std::uint32_t>(hit[0])==0x80000000u && hit[1]==1.25f && hit[2]==-2.5f,"Saved hit bits changed");
    const auto string=at<NativeButtonString>(p,0x38);payload.assign(40,'X');
    require(string.length==expected.size() && string.capacity==expected.size()+1 &&
            std::memcmp(reinterpret_cast<const void*>(string.data),expected.data(),expected.size())==0,"Payload aliases source or truncates embedded NUL");
    request.discard();
    require(request.state()==NativeButtonRequestState::discarded && f.disposals==1 && f.requests.empty() && f.payloads.empty(),"Explicit pre-handoff discard leaked");
    rejects([&]{request.discard();},"Discard retried");rejects([&]{request.prepare(release_data());},"Preparation reused one-shot object");
}
void invalid_inputs_before_native_allocation() {
    FakeNative f;auto api=f.api();api.request_vtable=0;
    rejects([&]{NativeButtonReleaseRequest invalid(api);},"Missing profile vtable accepted");
    api=f.api();api.enqueue=nullptr;
    rejects([&]{NativeButtonReleaseRequest invalid(api);},"Missing native enqueue accepted");
    NativeButtonReleaseRequest request(f.api());std::string too_large(65536,'x');
    rejects([&]{request.prepare(release_data(too_large));},"Truncating payload accepted");
    auto invalid=release_data();invalid.saved_local_hit[1]=std::numeric_limits<float>::infinity();
    rejects([&]{request.prepare(invalid);},"Nonfinite saved hit accepted");
    require(f.allocations==0 && request.state()==NativeButtonRequestState::empty,"Invalid input touched native allocation");
    request.prepare(release_data({}));
    require(at<NativeButtonString>(reinterpret_cast<void*>(request.address_token()),0x38).data==0,"Empty payload invented a buffer");
    rejects([&]{request.submit(nullptr);},"Null live queue accepted");
    require(request.state()==NativeButtonRequestState::prepared && f.submissions==0,"Invalid queue transferred ownership");
    request.discard();
}
void partial_payload_and_failed_allocation() {
    for(const auto assignment:{FakeNative::throw_before_payload,FakeNative::throw_after_payload,
        FakeNative::bad_length,FakeNative::bad_capacity,FakeNative::bad_terminator,FakeNative::bad_copy}) {
        FakeNative f;f.assignment=assignment;NativeButtonReleaseRequest request(f.api());
        rejects([&]{request.prepare(release_data());},"Broken native payload accepted");
        require(request.state()==NativeButtonRequestState::preparation_failed,"Preparation failure state lost");
        rejects([&]{request.submit(&f.queue_token);},"Partial request submitted");
        request.discard();require(f.disposals==1 && f.payloads.empty() && f.requests.empty(),"Partial native payload leaked or freed twice");
    }
    FakeNative f;f.allocation_null=true;NativeButtonReleaseRequest request(f.api());
    rejects([&]{request.prepare(release_data());},"Null native allocation accepted");
    require(request.address_token()==0 && f.assignments==0,"Allocation failure called payload API");
    request.discard();require(f.disposals==0,"Null allocation sent to native destructor");
}
void ownership_handoff_and_uncertainty() {
    for(const auto mode:{FakeNative::queued,FakeNative::synchronous_destroy,FakeNative::throw_before_enqueue,
                        FakeNative::throw_after_enqueue,FakeNative::throw_after_destroy}) {
        FakeNative f;f.submission=mode;
        {
            NativeButtonReleaseRequest request(f.api());request.prepare(release_data());const auto identity=request.address_token();
            if(mode==FakeNative::queued || mode==FakeNative::synchronous_destroy) {
                request.submit(&f.queue_token);require(request.state()==NativeButtonRequestState::transferred,"Successful handoff not recorded");
            } else {
                rejects([&]{request.submit(&f.queue_token);},"Native push exception swallowed");
                require(request.state()==NativeButtonRequestState::ownership_uncertain,"Unknown push outcome guessed");
            }
            require(request.address_token()==identity,"Transferred identity lost");
            rejects([&]{request.submit(&f.queue_token);},"Native push retried after attempted handoff");
            rejects([&]{request.discard();},"Transferred/uncertain allocation freed by caller");
            require(f.submissions==1,"One-shot handoff called native push twice");
        }
        const auto before=f.disposals;
        while(!f.requests.empty())f.destroy(*f.requests.begin()); // Native owner/fixture accounts for uncertainty.
        require(f.disposals-before==(mode==FakeNative::synchronous_destroy || mode==FakeNative::throw_after_destroy?0u:1u),"Destructor performed an unexpected native free");
    }
}
void explicit_cleanup_is_not_retried_by_destructor() {
    FakeNative f;std::uintptr_t identity{};
    {NativeButtonReleaseRequest request(f.api());request.prepare(release_data());identity=request.address_token();}
    require(f.disposals==0 && f.requests.size()==1,"C++ destructor called native cleanup");
    f.destroy(reinterpret_cast<NativeButtonRequestLayout*>(identity));
    f.throw_after_dispose=true;
    {
        NativeButtonReleaseRequest request(f.api());request.prepare(release_data());
        rejects([&]{request.discard();},"Native discard exception swallowed");
        require(request.state()==NativeButtonRequestState::discard_uncertain,"Partial discard guessed");
        rejects([&]{request.discard();},"Uncertain native discard retried");
    }
    require(f.disposals==2 && f.requests.empty(),"Destructor retried a throwing cleanup");
    FakeNative maximum;std::string payload(65535,'z');NativeButtonReleaseRequest request(maximum.api());
    auto release=release_data(payload);release.skip_on_button_press=false;request.prepare(release);
    require(at<unsigned char>(reinterpret_cast<void*>(request.address_token()),0x34)==0,"Normal release skip flag changed");
    request.discard();
}
}
int main() {
    try {
        layout_payload_and_explicit_discard();invalid_inputs_before_native_allocation();
        partial_payload_and_failed_allocation();ownership_handoff_and_uncertainty();
        explicit_cleanup_is_not_retried_by_destructor();
        std::cout<<"Native button release request tests passed: exact layout, owned payload, one-shot handoff and no destructor callbacks.\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
