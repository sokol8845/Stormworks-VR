#include "swvr/native_button_retirement.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace swvr {
namespace {
constexpr std::size_t record_capacity=128,wrapper_capacity=32;
constexpr std::uintptr_t serializer_rva=0x23ca90,dispatch_rva=0x855ab0,raw_clear_rva=0x8d3bb0;
constexpr std::uintptr_t typed_clear_rva=0x89d030,delete_rva=0x95c2ec,free_rva=0x9b6750;
constexpr std::uintptr_t request_vtable_rva=0xb0fec0,parent_vtable_rva=0xb89760;
using Serializer=void(*)(void*,void*);
using Dispatch=void(*)(void*,void*,void*,void*,void*);
using Clear=void(*)(void*);
using Delete=void(*)(void*,std::size_t);
using Free=void(*)(void*);
Serializer real_serializer{};Dispatch real_dispatch{};Clear real_raw_clear{},real_typed_clear{};
Delete real_delete{};Free native_free{};
std::uintptr_t image_base{};
std::atomic<bool> installed{};
std::mutex ledger_mutex,log_mutex;
FILE* log_file{};
std::uint64_t log_rows{};
constexpr std::uint64_t log_limit=10000;

enum class Phase { available, reserving, preparing, discarding, submitted, retiring, quarantined };
struct Wrapper {bool used{};NativeButtonRequestApi source{};};
struct Record {
    Phase phase{Phase::available};std::uint64_t id{},parent_id{};
    Wrapper* wrapper{};std::uintptr_t address{},payload{},parent{},source_queue{};
    bool lookup{},enqueue_uncertain{},serialized{},dispatched{},cancelled{};
    bool expected_delete_started{},expected_delete_completed{};
};
std::array<Wrapper,wrapper_capacity> wrappers{};
std::array<Record,record_capacity> records{};
std::uint64_t next_id=1,next_parent_id=1,registrations{},write_retired{},localhost_retired{},quarantined{},unknown_deletes{},reset_generation=1;
std::uint64_t source_reset_retired{},source_destroy_retired{};

struct Candidate {std::uint64_t id{},parent_id{};std::uintptr_t address{},payload{},parent{};bool eligible{};std::uintptr_t queue{};};
struct Batch {std::array<Candidate,record_capacity> entries{};std::size_t size{};bool localhost{},source{},destroy{};};
struct TypedScope {Batch& batch;TypedScope* previous{};};
thread_local TypedScope* active_typed{};
thread_local std::uintptr_t active_inner_queue{};
thread_local Batch* active_source{};

bool readable(std::uintptr_t address,std::size_t bytes,bool executable=false) noexcept {
    if(!address || bytes>std::numeric_limits<std::uintptr_t>::max()-address)return false;
    const auto end=address+bytes;
    while(address<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto p=info.Protect&0xffu;
        const bool exec=p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
        if((!exec && p!=PAGE_READONLY && p!=PAGE_READWRITE && p!=PAGE_WRITECOPY) || (executable && !exec))return false;
        const auto base=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        if(info.RegionSize>std::numeric_limits<std::uintptr_t>::max()-base)return false;
        const auto next=base+info.RegionSize;if(next<=address)return false;address=next;
    }
    return true;
}
template<class T>bool read(std::uintptr_t object,std::size_t offset,T& out) noexcept {
    if(offset>std::numeric_limits<std::uintptr_t>::max()-object)return false;
    object+=offset;if(!readable(object,sizeof(T)))return false;
    std::memcpy(&out,reinterpret_cast<const void*>(object),sizeof(T));return true;
}
template<class F>bool visit_ring(std::uintptr_t ring,F&& visit) {
    std::uintptr_t array{};std::uint32_t capacity{},begin{},count{};
    if(!read(ring,0,array)||!read(ring,8,capacity)||!read(ring,0xc,begin)||!read(ring,0x10,count))return false;
    if(!count)return true;
    if(!array || !capacity || count>capacity || count>65536 ||
       !readable(array,static_cast<std::size_t>(capacity)*sizeof(std::uintptr_t)))return false;
    for(std::uint32_t index=0;index<count;++index) {
        std::uintptr_t value{};
        const auto slot=static_cast<std::uint32_t>(begin+index)%capacity;
        if(!read(array,static_cast<std::size_t>(slot)*sizeof(value),value))return false;
        visit(value);
    }
    return true;
}
Record* by_id(std::uint64_t id) noexcept {
    if(id)for(auto& record:records)if(record.phase!=Phase::available && record.id==id)return &record;
    return nullptr;
}
Record* by_lookup(std::uintptr_t address) noexcept {
    if(address)for(auto& record:records)if(record.lookup && record.address==address)return &record;
    return nullptr;
}
Record* by_prepared(Wrapper* wrapper,std::uintptr_t address) noexcept {
    for(auto& record:records)if(record.phase==Phase::preparing && record.wrapper==wrapper && record.address==address)return &record;
    return nullptr;
}
void quarantine(Record& record) noexcept {
    if(record.phase!=Phase::quarantined){record.phase=Phase::quarantined;++quarantined;}
    record.lookup=false;
}
void log_event(const char* event,std::uint64_t id=0,std::uintptr_t request=0,std::uintptr_t payload=0,
               std::uintptr_t parent=0,std::uint64_t parent_id=0) noexcept {
    try {
        std::lock_guard lock(log_mutex);if(!log_file || log_rows>=log_limit)return;
        LARGE_INTEGER counter{};QueryPerformanceCounter(&counter);++log_rows;
        if(std::fprintf(log_file,"%llu\t%lld\t%lu\t%s\t%llu\t%llx\t%llx\t%llx\t%llu\n",
            static_cast<unsigned long long>(log_rows),static_cast<long long>(counter.QuadPart),GetCurrentThreadId(),event,
            static_cast<unsigned long long>(id),static_cast<unsigned long long>(request),static_cast<unsigned long long>(payload),
            static_cast<unsigned long long>(parent),static_cast<unsigned long long>(parent_id))<0){std::fclose(log_file);log_file=nullptr;return;}
        if(log_rows==log_limit)std::fputs("#state\tlog_limit_reached_ownership_ledger_still_active\n",log_file);
        if(log_rows%30==0 || log_rows==log_limit)std::fflush(log_file);
    }catch(...){}
}
bool parent_members(std::uintptr_t parent,Batch& batch) {
    std::uintptr_t vtable{};std::uint32_t type{};
    if(!read(parent,0,vtable)||vtable!=image_base+parent_vtable_rva || !read(parent,8,type)||type!=3)return false;
    return visit_ring(parent+0x18,[&](std::uintptr_t request) {
        std::lock_guard lock(ledger_mutex);const auto record=by_lookup(request);if(!record)return;
        for(std::size_t i=0;i<batch.size;++i)if(batch.entries[i].id==record->id)return;
        if(batch.size<batch.entries.size())batch.entries[batch.size++]={record->id,record->parent_id,record->address,record->payload,parent,false};
    });
}
Batch observe_parent(std::uintptr_t parent) {
    Batch batch;if(!parent_members(parent,batch))return {};
    std::lock_guard lock(ledger_mutex);
    // A parent address whose previous child disappeared cannot lend that old
    // observation to a new allocation at the same address.
    for(auto& record:records)if(record.lookup && record.parent==parent) {
        bool present{};for(std::size_t i=0;i<batch.size;++i)present|=batch.entries[i].id==record.id;
        if(!present)quarantine(record);
    }
    std::uint64_t parent_id{};
    for(std::size_t i=0;i<batch.size;++i) {
        const auto record=by_id(batch.entries[i].id);
        if(record && record->lookup && record->parent==parent)parent_id=record->parent_id;
    }
    if(!parent_id && batch.size) {
        if(!next_parent_id){for(std::size_t i=0;i<batch.size;++i)if(auto record=by_id(batch.entries[i].id))quarantine(*record);return {};}
        parent_id=next_parent_id++;
    }
    for(std::size_t i=0;i<batch.size;++i)if(auto record=by_id(batch.entries[i].id)) {
        if(record->parent && record->parent!=parent){quarantine(*record);continue;}
        if(!record->lookup)continue;
        record->parent=parent;record->parent_id=parent_id;batch.entries[i].parent_id=parent_id;
    }
    return batch;
}
void finish_observation(const Batch& batch,bool dispatch,bool success) {
    std::lock_guard lock(ledger_mutex);
    for(std::size_t i=0;i<batch.size;++i) {
        const auto& item=batch.entries[i];const auto record=by_id(item.id);
        if(!record || !record->lookup || record->parent!=item.parent || record->parent_id!=item.parent_id)continue;
        if(dispatch)record->dispatched=success;else record->serialized=success;
    }
}
void serializer(void* message,void* buffer) {
    std::uint8_t write{};
    const bool known=reinterpret_cast<std::uintptr_t>(_ReturnAddress())==image_base+0x23cd40 &&
                     read(reinterpret_cast<std::uintptr_t>(buffer),0x14,write) && write==1;
    const auto batch=known?observe_parent(reinterpret_cast<std::uintptr_t>(message)):Batch{};
    try {real_serializer(message,buffer);}catch(...) {finish_observation(batch,false,false);log_event("serializer_exception");throw;}
    finish_observation(batch,false,true);
}
void dispatch(void* owner,void* graphics,void* context,void* message,void* game_data) {
    const bool known=reinterpret_cast<std::uintptr_t>(_ReturnAddress())==image_base+0x86a7ff;
    const auto batch=known?observe_parent(reinterpret_cast<std::uintptr_t>(message)):Batch{};
    try {real_dispatch(owner,graphics,context,message,game_data);}
    catch(...) {finish_observation(batch,true,false);log_event("dispatch_exception");throw;}
    finish_observation(batch,true,true);
}
Batch snapshot_clear(void* queue,bool localhost) {
    Batch result;result.localhost=localhost;
    visit_ring(reinterpret_cast<std::uintptr_t>(queue),[&](std::uintptr_t parent) {
        Batch members;if(!parent_members(parent,members))return;
        for(std::size_t i=0;i<members.size && result.size<result.entries.size();++i) {
            auto candidate=members.entries[i];bool duplicate{};
            for(std::size_t j=0;j<result.size;++j)duplicate|=result.entries[j].id==candidate.id;
            if(duplicate)continue;
            std::lock_guard lock(ledger_mutex);const auto record=by_id(candidate.id);
            candidate.eligible=record && record->lookup && record->parent==parent && record->parent_id &&
                               (localhost?record->dispatched:record->serialized);
            candidate.parent_id=record?record->parent_id:0;
            result.entries[result.size++]=candidate;
        }
    });
    return result;
}
Batch snapshot_source(void* queue,bool destroy) {
    Batch batch;batch.source=true;batch.destroy=destroy;
    const auto address=reinterpret_cast<std::uintptr_t>(queue);
    visit_ring(address,[&](std::uintptr_t request) {
        std::lock_guard lock(ledger_mutex);const auto record=by_lookup(request);
        if(!record || record->phase!=Phase::submitted || record->source_queue!=address || record->parent || record->parent_id)return;
        for(std::size_t i=0;i<batch.size;++i)if(batch.entries[i].id==record->id)return;
        if(batch.size<batch.entries.size())batch.entries[batch.size++]={record->id,0,record->address,record->payload,0,true,address};
    });
    return batch;
}
void quarantine_batch(const Batch& batch) noexcept {
    try {
        std::lock_guard lock(ledger_mutex);
        for(std::size_t i=0;i<batch.size;++i)if(auto record=by_id(batch.entries[i].id))quarantine(*record);
    }catch(...){}
}
void retire_batch(const Batch& batch) {
    for(std::size_t i=0;i<batch.size;++i) {
        const auto candidate=batch.entries[i];bool retire{};
        {
            std::lock_guard lock(ledger_mutex);const auto record=by_id(candidate.id);if(!record)continue;
            const bool matches=record->address==candidate.address && (batch.source?
                (!record->parent && !record->parent_id && record->source_queue==candidate.queue):
                (record->parent==candidate.parent && record->parent_id==candidate.parent_id));
            if(!candidate.eligible || !matches || record->phase!=Phase::submitted ||
               ((batch.localhost||batch.source)?!record->expected_delete_completed:!record->lookup)) {quarantine(*record);continue;}
            record->phase=Phase::retiring;
            // WRITE request is still alive while its payload is freed. Keep it
            // discoverable so an unexpected native delete in that interval can
            // quarantine its ID before address reuse. Localhost already deleted
            // the request and therefore must not reinsert that old address.
            if(batch.localhost||batch.source)record->lookup=false;
            retire=true;
        }
        if(!retire)continue;
        try {
            if(candidate.payload)native_free(reinterpret_cast<void*>(candidate.payload));
            if(!batch.localhost && !batch.source) {
                {
                    std::lock_guard lock(ledger_mutex);const auto record=by_id(candidate.id);
                    if(!record || record->phase!=Phase::retiring || !record->lookup)continue;
                    record->lookup=false;
                }
                real_delete(reinterpret_cast<void*>(candidate.address),sizeof(NativeButtonRequestLayout));
            }
        }catch(...) {
            {std::lock_guard lock(ledger_mutex);if(auto record=by_id(candidate.id))quarantine(*record);}
            log_event("retirement_exception",candidate.id,candidate.address,candidate.payload,candidate.parent,candidate.parent_id);
            // Original clear already succeeded. Propagate no guessed retry or
            // altered host exception; preserve the quarantined allocation ID.
            continue;
        }
        {
            std::lock_guard lock(ledger_mutex);if(auto record=by_id(candidate.id)) {
                if(record->phase==Phase::retiring){*record={};
                    if(batch.source){if(batch.destroy)++source_destroy_retired;else ++source_reset_retired;}
                    else if(batch.localhost)++localhost_retired;else ++write_retired;}
            }
        }
        const auto event=batch.source?(batch.destroy?"source_destroy_retired":"source_reset_retired"):
            (batch.localhost?"localhost_retired":"write_retired");
        log_event(event,candidate.id,candidate.address,candidate.payload,candidate.parent,candidate.parent_id);
    }
}
bool expected_inner(void* queue) noexcept {
    if(!active_typed)return false;
    const auto value=reinterpret_cast<std::uintptr_t>(queue);
    for(std::size_t i=0;i<active_typed->batch.size;++i) {
        const auto& item=active_typed->batch.entries[i];if(item.eligible && value==item.parent+0x18)return true;
    }
    return false;
}
void raw_clear(void* queue) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    struct RestoreSource {Batch* previous;~RestoreSource(){active_source=previous;}} restore_source{active_source};
    active_source=nullptr; // Nested clears cannot borrow an outer source-Q proof.
    if(caller==image_base+0x24005b || caller==image_base+0x23eb68) {
        auto batch=snapshot_source(queue,caller==image_base+0x23eb68);
        active_source=&batch;
        try {real_raw_clear(queue);}catch(...) {quarantine_batch(batch);log_event("source_clear_exception");throw;}
        active_source=nullptr;
        retire_batch(batch);return;
    }
    if(caller==image_base+0x240898 || caller==image_base+0x240a21) {
        const auto batch=snapshot_clear(queue,false);
        try {real_raw_clear(queue);}catch(...) {quarantine_batch(batch);log_event("write_clear_exception");throw;}
        retire_batch(batch);return;
    }
    struct RestoreInner {std::uintptr_t previous;~RestoreInner(){active_inner_queue=previous;}} restore{active_inner_queue};
    if(caller==image_base+0x89d0a8 && expected_inner(queue))active_inner_queue=reinterpret_cast<std::uintptr_t>(queue);
    else active_inner_queue=0;
    real_raw_clear(queue);
}
void typed_clear(void* queue) {
    if(reinterpret_cast<std::uintptr_t>(_ReturnAddress())!=image_base+0x86a817){real_typed_clear(queue);return;}
    auto batch=snapshot_clear(queue,true);TypedScope scope{batch,active_typed};active_typed=&scope;
    struct RestoreTyped {TypedScope* previous;~RestoreTyped(){active_typed=previous;}} restore{scope.previous};
    try {real_typed_clear(queue);}catch(...) {quarantine_batch(batch);log_event("typed_clear_exception");throw;}
    retire_batch(batch);
}
Candidate* expected_delete(std::uintptr_t address) noexcept {
    if(active_source)for(std::size_t i=0;i<active_source->size;++i) {
        auto& item=active_source->entries[i];if(item.eligible && item.address==address)return &item;
    }
    if(!active_typed || !active_inner_queue)return nullptr;
    for(std::size_t i=0;i<active_typed->batch.size;++i) {
        auto& item=active_typed->batch.entries[i];
        if(item.eligible && item.address==address && active_inner_queue==item.parent+0x18)return &item;
    }
    return nullptr;
}
void native_delete(void* pointer,std::size_t bytes) {
    const auto address=reinterpret_cast<std::uintptr_t>(pointer);
    const bool raw_caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())==image_base+0x8d3bee;
    const auto expected=raw_caller?expected_delete(address):nullptr;
    std::uint64_t expected_id{},unknown_id{};bool duplicate{};
    {
        std::lock_guard lock(ledger_mutex);const auto record=by_lookup(address);
        if(record) {
            if(expected && expected->id==record->id && expected->parent_id==record->parent_id && record->phase==Phase::submitted) {
                record->expected_delete_started=true;record->lookup=false;expected_id=record->id;
            } else {unknown_id=record->id;quarantine(*record);++unknown_deletes;}
        } else if(expected) {
            const auto prior=by_id(expected->id);
            duplicate=prior && prior->address==address && prior->expected_delete_started;
        }
    }
    if(unknown_id)log_event("unknown_native_delete_quarantined",unknown_id,address);
    if(duplicate){log_event("duplicate_own_native_delete_suppressed",expected->id,address);return;}
    try {real_delete(pointer,bytes);}catch(...) {
        if(expected_id){std::lock_guard lock(ledger_mutex);if(auto record=by_id(expected_id))quarantine(*record);}
        throw;
    }
    if(expected_id){std::lock_guard lock(ledger_mutex);if(auto record=by_id(expected_id))record->expected_delete_completed=true;}
}

std::uint64_t reserve_record(Wrapper& wrapper) {
    std::lock_guard lock(ledger_mutex);
    if(!next_id)throw std::runtime_error("Native request allocation IDs exhausted");
    for(auto& record:records)if(record.phase==Phase::available) {
        record={};record.phase=Phase::reserving;record.id=next_id++;record.wrapper=&wrapper;return record.id;
    }
    throw std::runtime_error("Native request retirement capacity is full; no allocation attempted");
}
void* wrapped_allocate(void* context,std::size_t bytes) {
    auto& wrapper=*static_cast<Wrapper*>(context);
    if(bytes!=sizeof(NativeButtonRequestLayout))throw std::invalid_argument("Retirement wrapper accepts only 72-byte requests");
    const auto id=reserve_record(wrapper);void* pointer{};
    try {pointer=wrapper.source.allocate(wrapper.source.context,bytes);}catch(...) {
        {std::lock_guard lock(ledger_mutex);if(auto record=by_id(id))quarantine(*record);}
        log_event("allocation_exception",id);throw;
    }
    {
        std::lock_guard lock(ledger_mutex);auto record=by_id(id);
        if(!pointer){if(record)*record={};return nullptr;}
        const auto address=reinterpret_cast<std::uintptr_t>(pointer);
        for(auto& prior:records)if(prior.id!=id && prior.phase!=Phase::available && prior.phase!=Phase::quarantined &&
                                  !prior.expected_delete_completed && prior.address==address) {
            if(record)quarantine(*record);throw std::runtime_error("Native request address is already live");
        }
        if(record){record->address=address;record->phase=Phase::preparing;}
    }
    return pointer;
}
void wrapped_assign(void* context,NativeButtonString* destination,std::string_view source) {
    auto& wrapper=*static_cast<Wrapper*>(context);
    const auto address=reinterpret_cast<std::uintptr_t>(destination);
    if(address<offsetof(NativeButtonRequestLayout,payload))throw std::invalid_argument("Invalid native payload destination");
    {
        std::lock_guard lock(ledger_mutex);
        if(!by_prepared(&wrapper,address- offsetof(NativeButtonRequestLayout,payload)))
            throw std::logic_error("Payload does not belong to a preparing wrapped request");
    }
    wrapper.source.assign_payload(wrapper.source.context,destination,source);
}
void wrapped_discard(void* context,NativeButtonRequestLayout* request) {
    auto& wrapper=*static_cast<Wrapper*>(context);std::uint64_t id{};
    {
        std::lock_guard lock(ledger_mutex);auto record=by_prepared(&wrapper,reinterpret_cast<std::uintptr_t>(request));
        if(!record)throw std::logic_error("Discard is not an owned pre-handoff request");
        id=record->id;record->phase=Phase::discarding;
    }
    try {wrapper.source.destroy_unsubmitted(wrapper.source.context,request);}catch(...) {
        {std::lock_guard lock(ledger_mutex);if(auto record=by_id(id))quarantine(*record);}
        log_event("prehandoff_discard_exception",id);throw;
    }
    std::lock_guard lock(ledger_mutex);if(auto record=by_id(id))*record={};
}
void wrapped_enqueue(void* context,void* queue,NativeButtonRequestLayout* request) {
    auto& wrapper=*static_cast<Wrapper*>(context);NativeButtonRequestLayout copied{};
    const auto address=reinterpret_cast<std::uintptr_t>(request);std::uint64_t id{};
    {
        std::lock_guard lock(ledger_mutex);const auto record=by_prepared(&wrapper,address);
        if(!record)throw std::logic_error("Enqueue is not an owned pre-handoff request");id=record->id;
    }
    if(!queue || !read(address,0,copied) || copied.vtable!=image_base+request_vtable_rva || copied.type!=10 || copied.pressed!=0) {
        std::lock_guard lock(ledger_mutex);if(auto record=by_id(id))quarantine(*record);
        throw std::invalid_argument("Invalid own native button request handoff");
    }
    {
        std::lock_guard lock(ledger_mutex);auto record=by_id(id);
        if(!record || record->phase!=Phase::preparing)throw std::logic_error("Wrapped request handoff state changed");
        record->payload=copied.payload.data;record->source_queue=reinterpret_cast<std::uintptr_t>(queue);
        record->phase=Phase::submitted;record->lookup=true;++registrations;
    }
    log_event("enqueue_started",id,address,copied.payload.data);
    try {wrapper.source.enqueue(wrapper.source.context,queue,request);}catch(...) {
        {std::lock_guard lock(ledger_mutex);if(auto record=by_id(id))record->enqueue_uncertain=true;}
        log_event("enqueue_outcome_uncertain",id,address,copied.payload.data);throw;
    }
    // No native pointer is dereferenced after handoff, including on exceptions.
}
FILE* create_output(const std::filesystem::path& path) noexcept {
    const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return nullptr;
    const auto descriptor=_open_osfhandle(reinterpret_cast<std::intptr_t>(handle),_O_BINARY|_O_WRONLY);
    if(descriptor==-1){CloseHandle(handle);return nullptr;}
    const auto stream=_fdopen(descriptor,"wb");if(!stream)_close(descriptor);return stream;
}
template<std::size_t N>bool prefix(std::uintptr_t base,std::uintptr_t rva,const std::array<unsigned char,N>& expected) noexcept {
    return readable(base+rva,N,true) && !std::memcmp(reinterpret_cast<const void*>(base+rva),expected.data(),N);
}
bool call_guard(std::uintptr_t base,std::uintptr_t after,std::uintptr_t target) noexcept {
    std::array<unsigned char,5> call{};if(!read(base+after-5,0,call)||call[0]!=0xe8)return false;
    std::int32_t displacement{};std::memcpy(&displacement,call.data()+1,sizeof(displacement));
    return static_cast<std::uintptr_t>(static_cast<std::intptr_t>(after)+displacement)==target;
}
}

NativeButtonRetirementStatus install_native_button_retirement(std::uintptr_t base,bool sha_matched,
    const std::filesystem::path& trace_path) noexcept {
    wchar_t option[8]{};
    if(GetEnvironmentVariableW(L"SWVR_HAND_BUTTONS",option,8)!=1 || option[0]!=L'1')return NativeButtonRetirementStatus::disabled;
    if(installed.load(std::memory_order_acquire))return NativeButtonRetirementStatus::already_installed;
    if(!sha_matched || !base || base>std::numeric_limits<std::uintptr_t>::max()-parent_vtable_rva-8)
        return NativeButtonRetirementStatus::unsupported_profile;
    const std::array<void*,5> targets{reinterpret_cast<void*>(base+serializer_rva),reinterpret_cast<void*>(base+dispatch_rva),
        reinterpret_cast<void*>(base+raw_clear_rva),reinterpret_cast<void*>(base+typed_clear_rva),reinterpret_cast<void*>(base+delete_rva)};
    const std::array<void*,5> detours{reinterpret_cast<void*>(serializer),reinterpret_cast<void*>(dispatch),reinterpret_cast<void*>(raw_clear),
        reinterpret_cast<void*>(typed_clear),reinterpret_cast<void*>(native_delete)};
    const std::array<void**,5> originals{reinterpret_cast<void**>(&real_serializer),reinterpret_cast<void**>(&real_dispatch),
        reinterpret_cast<void**>(&real_raw_clear),reinterpret_cast<void**>(&real_typed_clear),reinterpret_cast<void**>(&real_delete)};
    std::size_t created{};FILE* stream{};
    const auto rollback=[&] {
        while(created)MH_RemoveHook(targets[--created]);
        real_serializer=nullptr;real_dispatch=nullptr;real_raw_clear=nullptr;real_typed_clear=nullptr;real_delete=nullptr;
        if(stream){std::fclose(stream);stream=nullptr;}
    };
    try {
        const std::array<unsigned char,24> serializer_bytes{0x83,0x7a,0x10,0x02,0x4c,0x8b,0xc9,0x72,0x2a,0x80,0x7a,0x14,0x00,0x75,0x0f,0x48,0x8b,0x42,0x08,0x44,0x0f,0xb7,0x00,0x66};
        const std::array<unsigned char,24> dispatch_bytes{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0x78,0xe8,0xff,0xff,0xb8,0x18,0x19};
        const std::array<unsigned char,24> raw_bytes{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x33,0xf6,0x48,0x8b,0xd9,0x8b,0xfe,0x39,0x71};
        const std::array<unsigned char,24> typed_bytes{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x38,0x48,0x8b,0xd9,0x45,0x33,0xff,0x41,0x8b,0xef,0x44,0x39};
        const std::array<unsigned char,5> delete_bytes{0xe9,0x8f,0xe4,0x7f,0xff};
        const std::array<unsigned char,24> free_bytes{0x48,0x83,0xec,0x28,0x48,0x85,0xc9,0x74,0x0d,0x48,0x83,0xe1,0xf8,0x48,0x8b,0x49,0xf8,0xe8,0xce,0x3d,0xff,0xff,0x48,0x83};
        const std::array<unsigned char,24> reset_bytes{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xec,0x20,0x8b,0xda,0x48,0x8b,0xf9,0x80};
        const std::array<unsigned char,24> destroy_bytes{0x48,0x89,0x4c,0x24,0x08,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x38,0x48,0x8b,0xf9,0xe8,0x57,0xe3,0xf3};
        std::uintptr_t parent_serializer{};std::array<unsigned char,2> virtual_call{};
        if(!prefix(base,serializer_rva,serializer_bytes)||!prefix(base,dispatch_rva,dispatch_bytes)||!prefix(base,raw_clear_rva,raw_bytes)||
           !prefix(base,typed_clear_rva,typed_bytes)||!prefix(base,delete_rva,delete_bytes)||!prefix(base,free_rva,free_bytes)||
           !prefix(base,0x23ff80,reset_bytes)||!prefix(base,0x23ea20,destroy_bytes)||
           !read(base+parent_vtable_rva,0,parent_serializer)||parent_serializer!=base+serializer_rva ||
           !read(base+0x23cd3e,0,virtual_call) || virtual_call!=std::array<unsigned char,2>{0xff,0x10} ||
           !call_guard(base,0x86a7ff,dispatch_rva)||!call_guard(base,0x240898,raw_clear_rva)||!call_guard(base,0x240a21,raw_clear_rva)||
           !call_guard(base,0x89d0a8,raw_clear_rva)||!call_guard(base,0x86a817,typed_clear_rva)||!call_guard(base,0x8d3bee,delete_rva)||
           !call_guard(base,0x89d0cf,delete_rva)||!call_guard(base,0x24005b,raw_clear_rva)||!call_guard(base,0x23eb68,raw_clear_rva))
            return NativeButtonRetirementStatus::signature_mismatch;
        if(!trace_path.is_absolute())return NativeButtonRetirementStatus::io_failure;
        stream=create_output(trace_path.wstring()+L".button-retirement.tsv");if(!stream)return NativeButtonRetirementStatus::io_failure;
        for(;created<targets.size();++created)if(MH_CreateHook(targets[created],detours[created],originals[created])!=MH_OK)break;
        if(created!=targets.size()){rollback();return NativeButtonRetirementStatus::hook_failure;}
        if(std::fprintf(stream,"#profile\tc38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d\n#state\thooks_created_not_enabled_by_module\n#capacity\t%zu\n#log_limit\t%llu\nseq\tqpc\tthread\tevent\tallocation_id\trequest\tpayload\tparent\tparent_id\n",
            record_capacity,static_cast<unsigned long long>(log_limit))<0 || std::fflush(stream)!=0){rollback();return NativeButtonRetirementStatus::io_failure;}
        image_base=base;native_free=reinterpret_cast<Free>(base+free_rva);log_file=stream;stream=nullptr;
        installed.store(true,std::memory_order_release);return NativeButtonRetirementStatus::installed;
    }catch(...) {rollback();return NativeButtonRetirementStatus::io_failure;}
}
NativeButtonRequestApi wrap_native_button_request_api(NativeButtonRequestApi source) {
    if(!installed.load(std::memory_order_acquire))throw std::logic_error("Native button retirement is not installed");
    if(source.request_vtable!=image_base+request_vtable_rva || !source.allocate || !source.assign_payload || !source.destroy_unsubmitted || !source.enqueue)
        throw std::invalid_argument("Incomplete or unrecognized native button source API");
    std::lock_guard lock(ledger_mutex);Wrapper* unused{};
    for(auto& wrapper:wrappers) {
        if(!wrapper.used){if(!unused)unused=&wrapper;continue;}
        const auto& old=wrapper.source;
        if(old.context==source.context && old.request_vtable==source.request_vtable && old.allocate==source.allocate &&
           old.assign_payload==source.assign_payload && old.destroy_unsubmitted==source.destroy_unsubmitted && old.enqueue==source.enqueue)
            return {&wrapper,source.request_vtable,wrapped_allocate,wrapped_assign,wrapped_discard,wrapped_enqueue};
    }
    if(!unused)throw std::runtime_error("Native retirement wrapper capacity is full");
    unused->used=true;unused->source=source;
    return {unused,source.request_vtable,wrapped_allocate,wrapped_assign,wrapped_discard,wrapped_enqueue};
}
void note_native_button_retirement_reset() noexcept {
    try {std::lock_guard lock(ledger_mutex);if(reset_generation!=std::numeric_limits<std::uint64_t>::max())++reset_generation;
        for(auto& record:records)if(record.phase!=Phase::available)record.cancelled=true;}catch(...){}
    log_event("logical_reset_ownership_retained");
}
NativeButtonRetirementStats native_button_retirement_stats() noexcept {
    NativeButtonRetirementStats out;
    try {
        {std::lock_guard lock(ledger_mutex);out.registrations=registrations;out.write_retired=write_retired;out.localhost_retired=localhost_retired;
            out.quarantined=quarantined;out.unknown_deletes=unknown_deletes;out.reset_generation=reset_generation;out.capacity=record_capacity;
            out.source_reset_retired=source_reset_retired;out.source_destroy_retired=source_destroy_retired;
            for(const auto& record:records)if(record.phase!=Phase::available){++out.retained_records;if(record.phase!=Phase::quarantined)++out.live_records;}}
        {std::lock_guard lock(log_mutex);out.log_rows=log_rows;}
    }catch(...){}
    return out;
}
void flush_native_button_retirement() noexcept {try {std::lock_guard lock(log_mutex);if(log_file)std::fflush(log_file);}catch(...){} }
const char* native_button_retirement_status_name(NativeButtonRetirementStatus status) noexcept {
    switch(status) {
    case NativeButtonRetirementStatus::disabled:return "disabled";
    case NativeButtonRetirementStatus::installed:return "hooks_created";
    case NativeButtonRetirementStatus::already_installed:return "already_installed";
    case NativeButtonRetirementStatus::unsupported_profile:return "unsupported_profile";
    case NativeButtonRetirementStatus::signature_mismatch:return "signature_mismatch";
    case NativeButtonRetirementStatus::io_failure:return "io_failure";
    case NativeButtonRetirementStatus::hook_failure:return "hook_failure";
    }
    return "unknown";
}
}
