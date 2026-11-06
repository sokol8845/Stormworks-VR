#include "swvr/native_button_request.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

namespace swvr {
struct NativeButtonReleaseRequest::Impl {
    NativeButtonRequestApi api;
    NativeButtonRequestState state{NativeButtonRequestState::empty};
    NativeButtonRequestLayout* request{};
    std::uintptr_t token{};
    explicit Impl(NativeButtonRequestApi callbacks):api(callbacks) {
        if(!api.request_vtable || !api.allocate || !api.assign_payload ||
           !api.destroy_unsubmitted || !api.enqueue)
            throw std::invalid_argument("Incomplete native button request API");
    }
};
NativeButtonReleaseRequest::NativeButtonReleaseRequest(NativeButtonRequestApi api):impl_(std::make_unique<Impl>(api)){}
NativeButtonReleaseRequest::~NativeButtonReleaseRequest()=default;
void NativeButtonReleaseRequest::prepare(const NativeButtonReleaseData& data) {
    auto& s=*impl_;
    if(s.state!=NativeButtonRequestState::empty)throw std::logic_error("Native button request preparation is one-shot");
    // Native string lengths are signed int32 and the wire length is uint16.
    // A larger payload would be truncated by the game's serializer.
    if(data.payload.size()>std::numeric_limits<std::uint16_t>::max())
        throw std::invalid_argument("Button payload exceeds the native wire length");
    for(const auto value:data.saved_local_hit)if(!std::isfinite(value))
        throw std::invalid_argument("Nonfinite saved button hit");
    s.state=NativeButtonRequestState::preparing;
    try {
        const auto memory=s.api.allocate(s.api.context,sizeof(NativeButtonRequestLayout));
        if(!memory)throw std::bad_alloc{};
        s.request=static_cast<NativeButtonRequestLayout*>(memory);
        s.token=reinterpret_cast<std::uintptr_t>(memory);
        NativeButtonRequestLayout value{};
        value.vtable=s.api.request_vtable;value.type=10;
        value.vehicle_id=data.vehicle_id;value.section_index=data.section_index;
        value.component_grid=data.component_grid;value.local_hit=data.saved_local_hit;
        value.skip_on_button_press=data.skip_on_button_press?1:0;
        std::memcpy(s.request,&value,sizeof(value));
        s.api.assign_payload(s.api.context,&s.request->payload,data.payload);
        // A complete native string must include a terminator. Source bytes may
        // contain NUL; its explicit length, not strlen, defines the payload.
        const auto& payload=s.request->payload;
        if(payload.length!=data.payload.size() ||
           (!payload.data && (payload.length || payload.capacity)) ||
           (payload.data && payload.capacity<=payload.length))
            throw std::runtime_error("Native payload assignment returned an inconsistent string");
        if(payload.data) {
            const auto bytes=reinterpret_cast<const char*>(payload.data);
            if(bytes[payload.length]!=0 ||
               (payload.length && (bytes==data.payload.data() || std::memcmp(bytes,data.payload.data(),payload.length)!=0)))
                throw std::runtime_error("Native payload assignment did not make a terminated independent copy");
        }
        s.state=NativeButtonRequestState::prepared;
    } catch(...) {s.state=NativeButtonRequestState::preparation_failed;throw;}
}
void NativeButtonReleaseRequest::submit(void* live_queue) {
    auto& s=*impl_;
    if(s.state!=NativeButtonRequestState::prepared)throw std::logic_error("Native button request is not prepared");
    if(!live_queue)throw std::invalid_argument("A live native request queue is required");
    // The native push has no accepted/rejected return. Mark uncertainty BEFORE
    // invoking it, and never read request afterwards: a synchronous consumer
    // could already have destroyed its allocation.
    s.state=NativeButtonRequestState::ownership_uncertain;
    auto request=s.request;s.request=nullptr;
    s.api.enqueue(s.api.context,live_queue,request);
    s.state=NativeButtonRequestState::transferred;
}
void NativeButtonReleaseRequest::discard() {
    auto& s=*impl_;
    if(s.state!=NativeButtonRequestState::prepared && s.state!=NativeButtonRequestState::preparation_failed)
        throw std::logic_error("Native button request cannot be discarded in this state");
    auto request=s.request;s.request=nullptr;s.state=NativeButtonRequestState::discarding;
    try {
        if(request)s.api.destroy_unsubmitted(s.api.context,request);
        s.state=NativeButtonRequestState::discarded;
    } catch(...) {s.state=NativeButtonRequestState::discard_uncertain;throw;}
}
NativeButtonRequestState NativeButtonReleaseRequest::state() const noexcept {return impl_->state;}
std::uintptr_t NativeButtonReleaseRequest::address_token() const noexcept {return impl_->token;}
}
