#include "swvr/native_button_bindings.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace swvr {
namespace {
constexpr std::uintptr_t allocate_rva=0x95c6f8,reserve_rva=0x17ca10,free_buffer_rva=0x9b6750;
constexpr std::uintptr_t delete_rva=0x95c2ec,push_rva=0x236ed0,serialize_rva=0x231bd0,vtable_rva=0xb0fec0;
constexpr std::uintptr_t button_vtable_rva=0xb75658,payload_getter_rva=0x32b470;
constexpr std::array<unsigned char,24> allocate_prefix{
    0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xeb,0x0f,0x48,
    0x8b,0xcb,0xe8,0xb1,0xc8,0x05,0x00,0x85,0xc0,0x74,0x13,0x48};
constexpr std::array<unsigned char,24> reserve_prefix{
    0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xbb,0x20,
    0x00,0x00,0x00,0x48,0x8b,0xf9,0x3b,0xd3,0x76,0x06,0x03,0xdb};
constexpr std::array<unsigned char,24> free_buffer_prefix{
    0x48,0x83,0xec,0x28,0x48,0x85,0xc9,0x74,0x0d,0x48,0x83,0xe1,
    0xf8,0x48,0x8b,0x49,0xf8,0xe8,0xce,0x3d,0xff,0xff,0x48,0x83};
constexpr std::array<unsigned char,5> delete_prefix{0xe9,0x8f,0xe4,0x7f,0xff};
constexpr std::array<unsigned char,24> push_prefix{
    0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,
    0x41,0x56,0x48,0x83,0xec,0x30,0x4c,0x8b,0xf2,0x48,0x8b,0xf1};
constexpr std::array<unsigned char,24> serialize_prefix{
    0x4c,0x8b,0xc2,0x4c,0x8b,0xc9,0x8b,0x52,0x10,0x41,0x80,0x78,
    0x14,0x00,0x75,0x11,0x83,0xfa,0x04,0x72,0x2b,0x49,0x8b,0x40};
constexpr std::array<unsigned char,24> payload_getter_prefix{
    0x48,0x89,0x54,0x24,0x10,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,
    0xda,0xc7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,0x48,0x8b,0x01};

bool readable(std::uintptr_t address,std::size_t bytes,bool executable) noexcept {
    if(!address || bytes>std::numeric_limits<std::uintptr_t>::max()-address)return false;
    const auto end=address+bytes;
    while(address<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) ||
           info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto protection=info.Protect&0xffu;
        const bool is_executable=protection==PAGE_EXECUTE_READ || protection==PAGE_EXECUTE_READWRITE ||
                                 protection==PAGE_EXECUTE_WRITECOPY;
        const bool is_readable=is_executable || protection==PAGE_READONLY || protection==PAGE_READWRITE ||
                               protection==PAGE_WRITECOPY;
        if(!is_readable || (executable && !is_executable))return false;
        const auto region=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if(info.RegionSize>std::numeric_limits<std::uintptr_t>::max()-region)return false;
        const auto next=region+info.RegionSize;if(next<=address)return false;address=next;
    }
    return true;
}
template<std::size_t N>void verify(std::uintptr_t base,std::uintptr_t rva,
                                  const std::array<unsigned char,N>& prefix) {
    const auto address=base+rva;
    if(!readable(address,prefix.size(),true) ||
       std::memcmp(reinterpret_cast<const void*>(address),prefix.data(),prefix.size()))
        throw std::runtime_error("Native button helper signature or mapping mismatch");
}
template<class T>bool read_native(std::uintptr_t address,std::size_t offset,T& output) noexcept {
    if(offset>std::numeric_limits<std::uintptr_t>::max()-address)return false;
    address+=offset;
    if(!readable(address,sizeof(T),false))return false;
    std::memcpy(&output,reinterpret_cast<const void*>(address),sizeof(T));return true;
}
}
struct NativeButtonRequestBindings::Impl {
    using Allocate=void*(*)(std::size_t);
    using Reserve=void(*)(NativeButtonString*,std::uint32_t);
    using FreeBuffer=void(*)(void*);
    using SizedDelete=void(*)(void*,std::size_t);
    using Push=void(*)(void*,void*);
    using PayloadGetter=NativeButtonString*(*)(void*,NativeButtonString*,const float*);
    Allocate allocate_native{};Reserve reserve_native{};FreeBuffer free_buffer_native{};
    SizedDelete delete_native{};Push push_native{};
    std::uintptr_t vtable{},button_vtable{};
    PayloadGetter payload_getter_native{};
    std::mutex mutex;
    std::unordered_set<NativeButtonRequestLayout*> pre_handoff;

    explicit Impl(std::uintptr_t base,bool sha_matched) {
        // The false-SHA branch must not even inspect the supplied base mapping.
        if(!sha_matched)throw std::invalid_argument("Native button bindings require the full executable SHA-256 match");
        if(!base || base>std::numeric_limits<std::uintptr_t>::max()-button_vtable_rva-0x2a8-sizeof(std::uintptr_t))
            throw std::invalid_argument("Invalid native executable base");
        verify(base,allocate_rva,allocate_prefix);verify(base,reserve_rva,reserve_prefix);
        verify(base,free_buffer_rva,free_buffer_prefix);verify(base,delete_rva,delete_prefix);
        verify(base,push_rva,push_prefix);verify(base,serialize_rva,serialize_prefix);
        verify(base,payload_getter_rva,payload_getter_prefix);
        std::uintptr_t serializer{};
        if(!readable(base+vtable_rva,sizeof(serializer),false))
            throw std::runtime_error("Native button request vtable is not readable");
        std::memcpy(&serializer,reinterpret_cast<const void*>(base+vtable_rva),sizeof(serializer));
        if(serializer!=base+serialize_rva)throw std::runtime_error("Native button request slot zero is not the known serializer");
        std::uintptr_t getter{};
        if(!read_native(base+button_vtable_rva,0x2a8,getter) || getter!=base+payload_getter_rva)
            throw std::runtime_error("Native button payload slot is not the known getter");
        allocate_native=reinterpret_cast<Allocate>(base+allocate_rva);
        reserve_native=reinterpret_cast<Reserve>(base+reserve_rva);
        free_buffer_native=reinterpret_cast<FreeBuffer>(base+free_buffer_rva);
        delete_native=reinterpret_cast<SizedDelete>(base+delete_rva);
        push_native=reinterpret_cast<Push>(base+push_rva);vtable=base+vtable_rva;
        button_vtable=base+button_vtable_rva;
        payload_getter_native=reinterpret_cast<PayloadGetter>(base+payload_getter_rva);
    }
    bool owns(NativeButtonRequestLayout* request) {
        std::lock_guard lock(mutex);return pre_handoff.contains(request);
    }
    void relinquish(NativeButtonRequestLayout* request) {
        std::lock_guard lock(mutex);
        if(pre_handoff.erase(request)!=1)throw std::logic_error("Request is not an owned pre-handoff allocation");
    }
    static void* allocate(void* context,std::size_t bytes) {
        auto& self=*static_cast<Impl*>(context);
        if(bytes!=sizeof(NativeButtonRequestLayout))throw std::invalid_argument("Native button request allocation must be 72 bytes");
        const auto request=static_cast<NativeButtonRequestLayout*>(self.allocate_native(bytes));
        if(!request)return nullptr;
        bool inserted{};
        try {std::lock_guard lock(self.mutex);inserted=self.pre_handoff.insert(request).second;}
        catch(...) {self.delete_native(request,bytes);throw;}
        if(!inserted)throw std::runtime_error("Native allocator returned an existing live request address");
        return request;
    }
    static void assign(void* context,NativeButtonString* destination,std::string_view source) {
        auto& self=*static_cast<Impl*>(context);
        if(!destination)throw std::invalid_argument("Native button string destination is null");
        const auto address=reinterpret_cast<std::uintptr_t>(destination);
        if(address<offsetof(NativeButtonRequestLayout,payload) ||
           !self.owns(reinterpret_cast<NativeButtonRequestLayout*>(address-offsetof(NativeButtonRequestLayout,payload))))
            throw std::logic_error("Native button string does not belong to an owned request");
        if(destination->data || destination->length || destination->capacity)
            throw std::logic_error("Native button string must be zero initialized");
        if(source.size()>std::numeric_limits<std::uint16_t>::max() || (!source.empty() && !source.data()))
            throw std::invalid_argument("Invalid native button payload length or data");
        if(source.empty())return;
        const auto required=static_cast<std::uint32_t>(source.size()+1);
        self.reserve_native(destination,required); // void ABI: RAX is undefined on the no-reallocation branch.
        if(!destination->data || destination->capacity<required || destination->length)
            throw std::runtime_error("Native reserve returned an inconsistent string buffer");
        auto output=reinterpret_cast<char*>(destination->data);
        std::memcpy(output,source.data(),source.size());output[source.size()]=0;
        destination->length=static_cast<std::uint32_t>(source.size());
    }
    static void destroy(void* context,NativeButtonRequestLayout* request) {
        auto& self=*static_cast<Impl*>(context);
        if(!request)throw std::invalid_argument("Native button request is null");
        self.relinquish(request); // Never retry partial cleanup, even when a helper throws.
        const auto payload=request->payload.data;
        if(payload)self.free_buffer_native(reinterpret_cast<void*>(payload));
        self.delete_native(request,sizeof(NativeButtonRequestLayout));
    }
    static void enqueue(void* context,void* queue,NativeButtonRequestLayout* request) {
        auto& self=*static_cast<Impl*>(context);
        if(!queue || !request)throw std::invalid_argument("A live native request and queue are required");
        self.relinquish(request); // Handoff uncertainty begins before calling the native queue.
        self.push_native(queue,request);
        // Request may already have been destroyed by a synchronous consumer.
    }
    NativeButtonCaptureResult capture(const NativeButtonCaptureScope& current,const HandEventCertificate& accepted) noexcept {
        using Status=NativeButtonCaptureStatus;
        const auto failed=[](Status status){NativeButtonCaptureResult result;result.status=status;return result;};
        if(!current.recognized_live_scope)return failed(Status::unrecognized_scope);
        const auto controller=reinterpret_cast<std::uintptr_t>(current.controller_live);
        if(!controller || !current.scene_generation || !current.owner_token ||
           current.scene_generation!=accepted.scene_generation || current.owner_token!=accepted.owner_token ||
           controller!=accepted.controller_token || (accepted.action!=HandAction::e&&accepted.action!=HandAction::q))return failed(Status::context_mismatch);
        // Only the CURRENT native held pointer may be dereferenced. A matching
        // certificate address by itself is never used as an object source.
        std::uintptr_t held{};
        const auto held_offset=hand_held_offset(accepted.action);
        if(!read_native(controller,held_offset,held))return failed(Status::unreadable_native_memory);
        if(!held || held!=accepted.target_token)return failed(Status::held_mismatch);
        std::uintptr_t type{},definition{},section{},vehicle{},array{};
        if(!read_native(held,0,type))return failed(Status::unreadable_native_memory);
        if(type!=button_vtable)return failed(Status::wrong_button_class);
        std::uint32_t button_type{};
        if(!read_native(held,0x58,definition) || !definition || !read_native(definition,0xda4,button_type))
            return failed(Status::unreadable_native_memory);
        if(button_type!=0 && button_type!=1 && button_type!=4)return failed(Status::unsupported_button_type);
        if(!read_native(held,0x28,section) || !section || !read_native(section,0x250,vehicle) || !vehicle)
            return failed(Status::unreadable_native_memory);
        std::uint32_t capacity{},begin{},count{};
        if(!read_native(vehicle,0,array) || !read_native(vehicle,8,capacity) ||
           !read_native(vehicle,0xc,begin) || !read_native(vehicle,0x10,count))
            return failed(Status::unreadable_native_memory);
        if(!capacity || count>capacity || !array ||
           static_cast<std::size_t>(capacity)>std::numeric_limits<std::size_t>::max()/sizeof(std::uintptr_t) ||
           !readable(array,static_cast<std::size_t>(capacity)*sizeof(std::uintptr_t),false))
            return failed(Status::invalid_ring);
        NativeButtonCapturedRelease copied;
        bool found{};
        for(std::uint32_t index=0;index<count;++index) {
            // Native code adds begin+i as uint32 BEFORE modulo. Preserve that
            // defined wrap, while checking pointer/byte arithmetic separately.
            const auto slot=static_cast<std::uint32_t>(begin+index)%capacity;
            std::uintptr_t candidate{};
            if(!read_native(array,static_cast<std::size_t>(slot)*sizeof(candidate),candidate))
                return failed(Status::invalid_ring);
            if(candidate==section){copied.section_index=index;found=true;break;}
        }
        if(!found)return failed(Status::section_not_found);
        if(!read_native(vehicle,0x178,copied.vehicle_id) || !read_native(held,0x18,copied.component_grid) ||
           !read_native(controller,hand_hit_offset(accepted.action),copied.saved_local_hit))return failed(Status::unreadable_native_memory);
        for(const auto value:copied.saved_local_hit)if(!std::isfinite(value))return failed(Status::nonfinite_saved_hit);
        NativeButtonString temporary{};NativeButtonString* returned{};Status status=Status::captured;
        try {returned=payload_getter_native(reinterpret_cast<void*>(held),&temporary,copied.saved_local_hit.data());}
        catch(...) {status=Status::getter_failed;}
        if(status==Status::captured) {
            if(returned!=&temporary)status=Status::getter_return_mismatch;
            else if(temporary.length>std::numeric_limits<std::uint16_t>::max() ||
                    (!temporary.data && (temporary.length || temporary.capacity)) ||
                    (temporary.data && temporary.capacity<=temporary.length))status=Status::invalid_payload;
            else if(temporary.data) {
                if(!readable(temporary.data,static_cast<std::size_t>(temporary.length)+1,false) ||
                   reinterpret_cast<const char*>(temporary.data)[temporary.length]!=0)status=Status::invalid_payload;
                else try {copied.payload.assign(reinterpret_cast<const char*>(temporary.data),temporary.length);}
                     catch(...) {status=Status::payload_copy_failed;}
            }
        }
        std::uintptr_t after{};
        if(!read_native(controller,held_offset,after))status=Status::unreadable_native_memory;
        else if(after!=held)status=Status::held_changed;
        // Remove the temporary pointer before native free. A throwing cleanup is
        // attempted once, and no catch path may retry an already freed buffer.
        const auto buffer=temporary.data;temporary.data=0;
        if(buffer)try {free_buffer_native(reinterpret_cast<void*>(buffer));}
                   catch(...) {status=Status::payload_cleanup_failed;}
        if(status!=Status::captured)return failed(status);
        NativeButtonCaptureResult result;result.status=status;result.data=std::move(copied);return result;
    }
};
NativeButtonRequestBindings::NativeButtonRequestBindings(std::uintptr_t base,bool sha_matched):
    impl_(std::make_unique<Impl>(base,sha_matched)){}
NativeButtonRequestBindings::~NativeButtonRequestBindings()=default;
NativeButtonRequestApi NativeButtonRequestBindings::api() noexcept {
    return {impl_.get(),impl_->vtable,Impl::allocate,Impl::assign,Impl::destroy,Impl::enqueue};
}
NativeButtonCaptureResult NativeButtonRequestBindings::capture_release(const NativeButtonCaptureScope& current,
    const HandEventCertificate& accepted_press) noexcept {return impl_->capture(current,accepted_press);}
const char* native_button_capture_status_name(NativeButtonCaptureStatus status) noexcept {
    switch(status) {
    case NativeButtonCaptureStatus::captured:return "captured";
    case NativeButtonCaptureStatus::unrecognized_scope:return "unrecognized_scope";
    case NativeButtonCaptureStatus::context_mismatch:return "context_mismatch";
    case NativeButtonCaptureStatus::held_mismatch:return "held_mismatch";
    case NativeButtonCaptureStatus::unreadable_native_memory:return "unreadable_native_memory";
    case NativeButtonCaptureStatus::wrong_button_class:return "wrong_button_class";
    case NativeButtonCaptureStatus::unsupported_button_type:return "unsupported_button_type";
    case NativeButtonCaptureStatus::invalid_ring:return "invalid_ring";
    case NativeButtonCaptureStatus::section_not_found:return "section_not_found";
    case NativeButtonCaptureStatus::nonfinite_saved_hit:return "nonfinite_saved_hit";
    case NativeButtonCaptureStatus::getter_failed:return "getter_failed";
    case NativeButtonCaptureStatus::getter_return_mismatch:return "getter_return_mismatch";
    case NativeButtonCaptureStatus::invalid_payload:return "invalid_payload";
    case NativeButtonCaptureStatus::payload_copy_failed:return "payload_copy_failed";
    case NativeButtonCaptureStatus::held_changed:return "held_changed";
    case NativeButtonCaptureStatus::payload_cleanup_failed:return "payload_cleanup_failed";
    }
    return "unknown";
}
}
