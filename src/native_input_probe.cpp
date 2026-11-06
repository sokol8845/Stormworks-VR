#include "swvr/native_input_probe.hpp"
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

namespace swvr {
namespace {
constexpr std::uintptr_t pop_rva=0x694230,dispatch_rva=0x790850;
constexpr std::uintptr_t queue_offset=0xce6b8,context5_offset=0xce5d0,context6_offset=0xce600;
constexpr std::array<unsigned char,24> pop_prefix{
    0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,
    0x74,0x24,0x20,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x30};
constexpr std::array<unsigned char,24> dispatch_prefix{
    0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,
    0x56,0x41,0x57,0x48,0x8d,0xa8,0xb8,0xfc,0xff,0xff,0x48,0x81};
struct CallSite {std::uintptr_t returned;std::array<unsigned char,5> bytes;};
constexpr std::array<CallSite,6> call_sites{{
    {0x86a728,{0xe8,0x08,0x9b,0xe2,0xff}},{0x86ccef,{0xe8,0x41,0x75,0xe2,0xff}},
    {0x86a77c,{0xe8,0xb4,0x9a,0xe2,0xff}},{0x86cd4e,{0xe8,0xe2,0x74,0xe2,0xff}},
    {0x86a76c,{0xe8,0xe4,0x60,0xf2,0xff}},{0x86cd3d,{0xe8,0x13,0x3b,0xf2,0xff}}
}};
using Pop=bool(*)(void*,void**);
using Dispatch=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,void*);
Pop real_pop{};
Dispatch real_dispatch{};
std::uintptr_t image_base{};
std::atomic<bool> observing{};
bool installed{};
FILE* output{};
std::mutex output_mutex;
std::uint64_t rows{},sequence{},epoch{};
bool io_failed{};
constexpr std::uint64_t row_limit=10000;
constexpr int unknown=std::numeric_limits<int>::min();

template<class T>T read(std::uintptr_t object,std::size_t offset=0) noexcept {
    T result{};std::memcpy(&result,reinterpret_cast<const void*>(object+offset),sizeof(result));return result;
}
struct Object {
    std::uintptr_t pointer{},vtable{},definition{};
    int button_type=unknown;
};
Object object(std::uintptr_t pointer) noexcept {
    Object result;result.pointer=pointer;
    if(!pointer)return result;
    result.vtable=read<std::uintptr_t>(pointer);
    // No guessed virtual calls, general component layout, or reads of an
    // unknown object's definition. These exact fields belong to vehicle_button.
    if(result.vtable==image_base+0xb75658) {
        result.definition=read<std::uintptr_t>(pointer,0x58);
        if(result.definition)result.button_type=read<int>(result.definition,0xda4);
    }
    return result;
}
struct Event {
    std::uintptr_t pointer{},vtable{};
    int known_type=unknown,action=unknown,phase=unknown,mode=unknown;
};
Event event(std::uintptr_t pointer) noexcept {
    Event result;result.pointer=pointer;
    if(!pointer)return result;
    result.vtable=read<std::uintptr_t>(pointer);
    if(result.vtable==image_base+0xbb2110) {
        result.known_type=0x17;result.action=read<int>(pointer,8);result.phase=read<int>(pointer,0xc);
    } else if(result.vtable==image_base+0xb59cf0 || result.vtable==image_base+0xb666b0) {
        result.known_type=result.vtable==image_base+0xb59cf0?1:2;
        result.mode=read<unsigned char>(pointer,8);
    } else if(result.vtable==image_base+0xb86978) {
        // Dispatcher case 8 calls the photo-mode helper without reading an
        // event payload. Its identity does not prove a byte exists at +8.
        result.known_type=8;
    }
    return result;
}
struct Snapshot {
    const char* relation="not_sampled";
    std::uintptr_t owner{},owner_vtable{},simulation{},simulation_vtable{},active{},controller{},queue{};
    std::uintptr_t application_vtable{},pause_ui{},pause_vtable{},ui_input{},ui_captured{},ui_active{},ui_hover{};
    int benchmark=unknown,pane=unknown,mouse_route=unknown,right_action_active=unknown;
    std::array<int,5> flags{unknown,unknown,unknown,unknown,unknown};
    std::array<Object,3> targets{};
    Event input{};
    unsigned blockers{};
};
bool owner_relation(Snapshot& s,std::uintptr_t owner,std::uintptr_t controller,bool compare_controller) noexcept {
    s.owner=owner;
    if(!owner){s.relation="null_owner";return false;}
    s.owner_vtable=read<std::uintptr_t>(owner);
    if(s.owner_vtable!=image_base+0xb0f4a0){s.relation="owner_profile";return false;}
    s.simulation=read<std::uintptr_t>(owner,0xce190);s.active=read<std::uintptr_t>(owner,0x350);
    if(!s.simulation){s.relation="null_simulation";return false;}
    // Check the owner's relationship before touching the simulation. This also
    // avoids reading a stale/inactive simulation when a different state is live.
    if(s.active!=s.simulation){s.relation="active_mismatch";return false;}
    s.simulation_vtable=read<std::uintptr_t>(s.simulation);
    if(s.simulation_vtable!=image_base+0xb1a988){s.relation="simulation_profile";return false;}
    if(s.simulation>std::numeric_limits<std::uintptr_t>::max()-0x38){s.relation="controller_overflow";return false;}
    s.controller=s.simulation+0x38;
    if(compare_controller && controller!=s.controller){s.relation="controller_mismatch";return false;}
    s.queue=owner+queue_offset;s.relation="relation_observed";return true;
}
Snapshot pop_snapshot(std::uintptr_t queue,void** slot) noexcept {
    Snapshot s;
    if(queue<queue_offset){s.relation="queue_relation";return s;}
    if(!owner_relation(s,queue-queue_offset,0,false))return s;
    if(!slot){s.relation="null_event_slot";return s;}
    s.input=event(read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(slot)));
    s.relation="pop_observed";return s;
}
Snapshot dispatch_snapshot(void* controller,std::uint64_t application,std::uint64_t context5,
    std::uint64_t context6,void* input) noexcept {
    Snapshot s;
    if(context5<context5_offset || context6<context6_offset ||
       context5-context5_offset!=context6-context6_offset) {s.relation="owner_context_relation";return s;}
    if(!owner_relation(s,context5-context5_offset,reinterpret_cast<std::uintptr_t>(controller),true))return s;
    if(!application){s.relation="null_application";return s;}
    s.application_vtable=read<std::uintptr_t>(application);
    if(s.application_vtable!=image_base+0xb17cc0){s.relation="application_profile";return s;}
    s.benchmark=read<unsigned char>(application,0x233);
    s.flags={read<unsigned char>(s.controller,0x494),read<unsigned char>(s.controller,0x49c),
        read<unsigned char>(s.controller,0x49f),read<unsigned char>(s.controller,0x4bc),read<unsigned char>(s.controller,0x4bd)};
    s.right_action_active=read<unsigned char>(s.controller,0x4bf);
    s.pause_ui=read<std::uintptr_t>(s.owner,0xce150);
    if(s.pause_ui) {
        s.pause_vtable=read<std::uintptr_t>(s.pause_ui);
        if(s.pause_vtable==image_base+0xb07c10)s.pane=read<int>(s.pause_ui,0x58);
    }
    s.ui_input=read<std::uintptr_t>(s.owner,0xcde80);
    s.ui_captured=read<std::uintptr_t>(s.simulation,0x25b0);
    s.ui_active=read<std::uintptr_t>(s.simulation,0x1d00);
    s.ui_hover=read<std::uintptr_t>(s.simulation,0x25a8);
    s.mouse_route=read<unsigned char>(s.simulation,0x3010);
    s.targets={object(read<std::uintptr_t>(s.controller,0x520)),object(read<std::uintptr_t>(s.controller,0x558)),
        object(read<std::uintptr_t>(s.controller,0x560))};
    s.input=event(reinterpret_cast<std::uintptr_t>(input));
    for(unsigned i=0;i<s.flags.size();++i)if(s.flags[i])s.blockers|=1u<<i;
    if(s.benchmark)s.blockers|=1u<<5;
    if(s.pane!=0)s.blockers|=1u<<6; // Includes missing/unrecognized pause UI.
    if(s.ui_input)s.blockers|=1u<<7;
    if(s.ui_captured)s.blockers|=1u<<8;
    if(s.ui_active)s.blockers|=1u<<9;
    if(s.ui_hover)s.blockers|=1u<<10;
    if(!s.mouse_route)s.blockers|=1u<<11;
    s.relation=s.pane==unknown?"pause_ui_unproven":"dispatch_observed_ui_unproven";
    return s;
}
bool known_pop(std::uintptr_t caller) noexcept {
    return caller==image_base+0x86a728 || caller==image_base+0x86ccef ||
           caller==image_base+0x86a77c || caller==image_base+0x86cd4e;
}
bool first_pop(std::uintptr_t caller) noexcept {return caller==image_base+0x86a728 || caller==image_base+0x86ccef;}
bool known_dispatch(std::uintptr_t caller) noexcept {return caller==image_base+0x86a76c || caller==image_base+0x86cd3d;}
void write_snapshot(const Snapshot& s) {
    std::fprintf(output,"\t%s\t%llx\t%llx\t%llx\t%llx\t%llx\t%llx\t%llx\t%llx"
        "\t%d\t%llx\t%llx\t%d\t%llx\t%llx\t%llx\t%llx\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%d",
        s.relation,static_cast<unsigned long long>(s.owner),static_cast<unsigned long long>(s.owner_vtable),
        static_cast<unsigned long long>(s.simulation),static_cast<unsigned long long>(s.simulation_vtable),
        static_cast<unsigned long long>(s.active),static_cast<unsigned long long>(s.controller),static_cast<unsigned long long>(s.queue),
        static_cast<unsigned long long>(s.application_vtable),s.benchmark,static_cast<unsigned long long>(s.pause_ui),
        static_cast<unsigned long long>(s.pause_vtable),s.pane,static_cast<unsigned long long>(s.ui_input),
        static_cast<unsigned long long>(s.ui_captured),static_cast<unsigned long long>(s.ui_active),static_cast<unsigned long long>(s.ui_hover),
        s.mouse_route,s.flags[0],s.flags[1],s.flags[2],s.flags[3],s.flags[4],s.blockers,s.right_action_active);
    for(const auto& t:s.targets)std::fprintf(output,"\t%llx\t%llx\t%llx\t%d",static_cast<unsigned long long>(t.pointer),
        static_cast<unsigned long long>(t.vtable),static_cast<unsigned long long>(t.definition),t.button_type);
    std::fprintf(output,"\t%llx\t%llx\t%d\t%d\t%d\t%d",static_cast<unsigned long long>(s.input.pointer),
        static_cast<unsigned long long>(s.input.vtable),s.input.known_type,s.input.action,s.input.phase,s.input.mode);
}
void record(const char* kind,std::uintptr_t caller,const std::array<std::uint64_t,7>& args,
    int returned,const Snapshot& before,const Snapshot& after,const char* completion="returned") noexcept {
    try {
        LARGE_INTEGER tick{};QueryPerformanceCounter(&tick);std::lock_guard lock(output_mutex);
        if(!output || !observing.load(std::memory_order_relaxed) || rows>=row_limit)return;
        ++rows;++sequence;
        std::fprintf(output,"%llu\t%lld\t%lu\t%llu\t%llu\t%s\t%llx\t%d\t%d\t%s",
            static_cast<unsigned long long>(sequence),static_cast<long long>(tick.QuadPart),GetCurrentThreadId(),
            static_cast<unsigned long long>(epoch),static_cast<unsigned long long>(rows),kind,
            static_cast<unsigned long long>(caller-image_base),first_pop(caller)?1:0,returned,completion);
        for(const auto arg:args)std::fprintf(output,"\t%llx",static_cast<unsigned long long>(arg));
        write_snapshot(before);write_snapshot(after);std::fputc('\n',output);
        if(std::ferror(output)){observing=false;io_failed=true;return;}
        if(rows==row_limit){if(std::fputs("#state\tlimit_reached\n",output)<0)io_failed=true;observing=false;}
        if((rows==row_limit || rows%30==0) && std::fflush(output)!=0){observing=false;io_failed=true;}
    } catch(...) {} // No probe exception crosses a native stack frame.
}
bool pop(void* queue,void** slot) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(!observing.load(std::memory_order_acquire) || !known_pop(caller))return real_pop(queue,slot);
    const auto q=reinterpret_cast<std::uintptr_t>(queue);
    const std::array<std::uint64_t,7> args{q,reinterpret_cast<std::uintptr_t>(slot),0,0,0,0,0};
    const auto before=pop_snapshot(q,slot);bool returned{};
    try {returned=real_pop(queue,slot);}
    catch(...) {record("pop",caller,args,-1,before,{},"native_exception");throw;}
    record("pop",caller,args,returned?1:0,before,pop_snapshot(q,slot));return returned;
}
void dispatch(void* controller,std::uint64_t a2,std::uint64_t a3,std::uint64_t a4,
    std::uint64_t a5,std::uint64_t a6,void* input) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(!observing.load(std::memory_order_acquire) || !known_dispatch(caller)) {
        real_dispatch(controller,a2,a3,a4,a5,a6,input);return;
    }
    const std::array<std::uint64_t,7> args{reinterpret_cast<std::uintptr_t>(controller),a2,a3,a4,a5,a6,reinterpret_cast<std::uintptr_t>(input)};
    const auto before=dispatch_snapshot(controller,a2,a5,a6,input);
    try {real_dispatch(controller,a2,a3,a4,a5,a6,input);}
    catch(...) {record("dispatch",caller,args,-1,before,{},"native_exception");throw;}
    record("dispatch",caller,args,-1,before,dispatch_snapshot(controller,a2,a5,a6,input));
}
FILE* create_output(const std::filesystem::path& path) noexcept {
    const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return nullptr;
    const int descriptor=_open_osfhandle(reinterpret_cast<std::intptr_t>(handle),_O_BINARY|_O_WRONLY);
    if(descriptor==-1){CloseHandle(handle);return nullptr;}
    auto stream=_fdopen(descriptor,"wb");if(!stream)_close(descriptor);return stream;
}
void write_snapshot_header(FILE* stream,const char* prefix) {
    constexpr std::array<const char*,43> names{
        "relation","owner","owner_vtable","simulation","simulation_vtable","active","controller","queue","application_vtable",
        "benchmark","pause_ui","pause_vtable","pause_pane","ui_input","ui_captured","ui_active","ui_hover","mouse_route",
        "photo","paused","pause_menu","dead","sleeping","observed_blockers","right_action_active",
        "component","component_vtable","component_definition","component_button_type",
        "held_right","held_right_vtable","held_right_definition","held_right_button_type",
        "held_left","held_left_vtable","held_left_definition","held_left_button_type",
        "input","input_vtable","input_known_type","input_action","input_phase","input_mode"
    };
    for(const auto name:names)std::fprintf(stream,"\t%s_%s",prefix,name);
}
}
NativeInputProbeStatus install_native_input_probe(std::uintptr_t base,bool sha_matched,
    const std::filesystem::path& trace_path) noexcept {
    wchar_t option[8]{};
    if(GetEnvironmentVariableW(L"SWVR_INPUT_PROBE",option,8)!=1 || option[0]!=L'1')return NativeInputProbeStatus::disabled;
    if(installed)return NativeInputProbeStatus::already_installed;
    if(!sha_matched || !base)return NativeInputProbeStatus::unsupported_profile;
    FILE* stream{};std::size_t created{};
    const std::array<void*,2> targets{reinterpret_cast<void*>(base+pop_rva),reinterpret_cast<void*>(base+dispatch_rva)};
    const auto rollback=[&] {
        while(created)MH_RemoveHook(targets[--created]);
        real_pop=nullptr;real_dispatch=nullptr;
        if(stream){std::fclose(stream);stream=nullptr;}
    };
    try {
        if(std::memcmp(targets[0],pop_prefix.data(),pop_prefix.size()) || std::memcmp(targets[1],dispatch_prefix.data(),dispatch_prefix.size()))
            return NativeInputProbeStatus::signature_mismatch;
        for(const auto& call:call_sites)if(std::memcmp(reinterpret_cast<const void*>(base+call.returned-5),call.bytes.data(),5))
            return NativeInputProbeStatus::signature_mismatch;
        if(!trace_path.is_absolute())return NativeInputProbeStatus::io_failure;
        stream=create_output(trace_path.wstring()+L".input-native.tsv");if(!stream)return NativeInputProbeStatus::io_failure;
        const std::array<void*,2> detours{reinterpret_cast<void*>(pop),reinterpret_cast<void*>(dispatch)};
        const std::array<void**,2> originals{reinterpret_cast<void**>(&real_pop),reinterpret_cast<void**>(&real_dispatch)};
        for(;created<targets.size();++created)if(MH_CreateHook(targets[created],detours[created],originals[created])!=MH_OK)break;
        if(created!=targets.size()){std::fputs("#state\thook_creation_failed\n",stream);rollback();return NativeInputProbeStatus::hook_failure;}
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        std::fprintf(stream,"#profile\tc38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d\n"
            "#exe_base\t%llx\n#qpc_frequency\t%lld\n#row_limit_per_epoch\t%llu\n#unknown_int\t%d\n"
            "#policy\tread_only_observation_no_input_allowed_verdict_no_event_emission\n"
            "#blockers\t0=photo,1=paused,2=pause_menu,3=dead,4=sleeping,5=benchmark,6=pause_ui_nonzero_or_unknown,"
            "7=ui_input,8=ui_captured,9=ui_active,10=ui_hover,11=mouse_route_zero\n"
            "#epoch\t0\tinstall\t0\n#state\thooks_created_not_enabled_by_probe\n",
            static_cast<unsigned long long>(base),static_cast<long long>(frequency.QuadPart),static_cast<unsigned long long>(row_limit),unknown);
        std::fputs("seq\tqpc\tthread\tepoch\tepoch_row\tkind\tcaller_rva\tfirst_pop\tpop_return\tcompletion"
            "\targ1\targ2\targ3\targ4\targ5\targ6\targ7",stream);
        write_snapshot_header(stream,"before");write_snapshot_header(stream,"after");std::fputc('\n',stream);
        if(std::ferror(stream)||std::fflush(stream)!=0){rollback();return NativeInputProbeStatus::io_failure;}
        image_base=base;output=stream;stream=nullptr;rows=sequence=epoch=0;io_failed=false;installed=true;
        observing.store(true,std::memory_order_release);return NativeInputProbeStatus::installed;
    } catch(...) {rollback();return NativeInputProbeStatus::io_failure;}
}
const char* native_input_probe_status_name(NativeInputProbeStatus status) noexcept {
    switch(status) {
    case NativeInputProbeStatus::disabled:return "disabled";
    case NativeInputProbeStatus::installed:return "hooks_created";
    case NativeInputProbeStatus::already_installed:return "already_installed";
    case NativeInputProbeStatus::unsupported_profile:return "unsupported_profile";
    case NativeInputProbeStatus::signature_mismatch:return "signature_mismatch";
    case NativeInputProbeStatus::io_failure:return "io_failure";
    case NativeInputProbeStatus::hook_failure:return "hook_failure";
    }
    return "unknown";
}
void flush_native_input_probe() noexcept {
    try {std::lock_guard lock(output_mutex);if(output && std::fflush(output)!=0){observing=false;io_failed=true;}}catch(...){}
}
bool rearm_native_input_probe_log() noexcept {
    try {
        std::lock_guard lock(output_mutex);
        if(!output || io_failed || std::ferror(output))return false;
        if(std::fprintf(output,"#epoch\t%llu\trearm\t%llu\n",static_cast<unsigned long long>(epoch+1),
            static_cast<unsigned long long>(sequence))<0 || std::fflush(output)!=0){observing=false;io_failed=true;return false;}
        ++epoch;rows=0;observing=true;return true;
    } catch(...) {return false;}
}
}
