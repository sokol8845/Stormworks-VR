#include "swvr/native_hand_ray.hpp"
#include "swvr/hand_interaction.hpp"
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <mutex>

namespace swvr {
namespace {
constexpr std::uintptr_t selection_rva=0x74f260,origin_rva=0x4e33a0;
constexpr std::uintptr_t selection_caller=0x745a24,origin_caller=0x74f38d;
constexpr std::array<unsigned char,24> selection_prefix{
    0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,
    0x56,0x41,0x57,0x48,0x8d,0xa8,0x58,0xf9,0xff,0xff,0x48,0x81};
constexpr std::array<unsigned char,24> origin_prefix{
    0x40,0x55,0x53,0x57,0x48,0x8d,0xac,0x24,0x20,0xfe,0xff,0xff,
    0x48,0x81,0xec,0xe0,0x02,0x00,0x00,0x48,0x8b,0x05,0x56,0x4c};
using Selection=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,const int*);
using Origin=void*(*)(void*,double*,std::uint8_t,std::uint8_t);
Selection real_selection{};
Origin real_origin{};
std::uintptr_t image_base{};
HandInteractionBridge* hand_bridge{}; // Lifetime explicitly owned by installer caller.
NativeHandRayCallbacks hand_callbacks{};
std::atomic<bool> enabled{};
bool installed{};
FILE* log_file{};
std::mutex log_mutex;
// TLS isolates nesting, not concurrent access to the same controller bytes.
// Serialize our enabled Selection transactions; recursive native calls on this
// thread remain legal. This does not lock unrelated host/controller writers.
std::recursive_mutex selection_mutex;
std::uint64_t rows{},total_sequence{},epoch{};
bool logging{},log_failed{};
constexpr std::uint64_t row_limit=10000;
constexpr auto max_snapshot_age=std::chrono::milliseconds(100);

template<class T>T read(const void* object,std::size_t offset) noexcept {
    T value{};std::memcpy(&value,static_cast<const unsigned char*>(object)+offset,sizeof(value));return value;
}
unsigned blocked_flags(const void* controller) noexcept {
    if(!controller)return 0;
    return (read<unsigned char>(controller,0x494)?1u:0u)|
           (read<unsigned char>(controller,0x49c)?2u:0u)|
           (read<unsigned char>(controller,0x49f)?4u:0u)|
           (read<unsigned char>(controller,0x4bc)?8u:0u)|
           (read<unsigned char>(controller,0x4bd)?16u:0u);
}
const char* block_name(unsigned flags) noexcept {
    if(flags&1)return "photo_mode";
    if(flags&2)return "paused";
    if(flags&4)return "pause_menu";
    if(flags&8)return "dead";
    if(flags&16)return "sleeping";
    return "none";
}
const char* application_block(const void* application) noexcept {
    if(!application || read<std::uintptr_t>(application,0)!=image_base+0xb17cc0)return "application_profile";
    return read<unsigned char>(application,0x233)?"benchmark_mode":nullptr;
}
struct SelectionScope {
    void* controller{};
    const void* application{}; // Borrowed only for this known native Selection.
    SelectionScope* previous{};
    HandInteractionSnapshot sample{};
    std::uintptr_t caller{};
    const char* reason="native_no_origin";
    std::array<double,3> head_origin{},used_origin{};
    std::array<float,3> used_direction{};
    std::array<unsigned char,12> saved_forward{};
    unsigned flags{},origin_calls{};
    bool eligible{},saved{},modified{},used{},nested{},gaze{};
    void restore_forward() noexcept {
        if(modified) {
            std::memcpy(static_cast<unsigned char*>(controller)+0xaa0,saved_forward.data(),saved_forward.size());
            modified=false;
        }
    }
};
thread_local SelectionScope* active_selection{};
struct RestoreSelection {
    SelectionScope& scope;
    std::array<unsigned char,12> suspended_forward{};
    bool suspended{};
    explicit RestoreSelection(SelectionScope& current):scope(current) {
        // A nested Selection on the same controller starts from its native
        // bytes, even when the outer transaction already replaced forward.
        if(scope.previous && scope.previous->modified && scope.previous->controller==scope.controller) {
            std::memcpy(suspended_forward.data(),static_cast<unsigned char*>(scope.controller)+0xaa0,suspended_forward.size());
            scope.previous->restore_forward();suspended=true;
        }
    }
    ~RestoreSelection(){
        scope.restore_forward();
        if(suspended) {
            std::memcpy(static_cast<unsigned char*>(scope.controller)+0xaa0,suspended_forward.data(),suspended_forward.size());
            scope.previous->modified=true;
        }
        active_selection=scope.previous;
    }
};
void record(const SelectionScope& scope) noexcept {
    try {
        LARGE_INTEGER tick{};QueryPerformanceCounter(&tick);
        std::lock_guard lock(log_mutex);
        if(!logging || !log_file || rows>=row_limit)return;
        const auto component=scope.controller?read<std::uintptr_t>(scope.controller,0x520):0;
        const auto world=scope.controller?read<std::uintptr_t>(scope.controller,0x538):0;
        ++rows;++total_sequence;
        const auto& s=scope.sample;
        const int written=std::fprintf(log_file,
            "%llu\t%lld\t%lu\t%llx\t%llu\t%llu\t%u\t%s\t%s\t%u\t%u"
            "\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.9g\t%.9g\t%.9g\t%llx\t%llx\t%u\t%llu\t%llu\t%llu\t%u\n",
            static_cast<unsigned long long>(total_sequence),static_cast<long long>(tick.QuadPart),GetCurrentThreadId(),
            static_cast<unsigned long long>(scope.caller>=image_base?scope.caller-image_base:0),
            static_cast<unsigned long long>(s.sequence),static_cast<unsigned long long>(s.xr_frame_id),
            static_cast<unsigned>(s.hand),hand_interaction_status_name(s.status),scope.reason,scope.origin_calls,scope.used?1u:0u,
            scope.head_origin[0],scope.head_origin[1],scope.head_origin[2],scope.used_origin[0],scope.used_origin[1],scope.used_origin[2],
            scope.used_direction[0],scope.used_direction[1],scope.used_direction[2],
            static_cast<unsigned long long>(component),static_cast<unsigned long long>(world),scope.flags,
            static_cast<unsigned long long>(epoch),static_cast<unsigned long long>(rows),
            static_cast<unsigned long long>(s.pinch_gesture_id),s.pinch_down?1u:0u);
        if(written<0){logging=false;log_failed=true;return;}
        if(rows==row_limit){if(std::fputs("#state\tlimit_reached\n",log_file)<0)log_failed=true;logging=false;}
        if((rows==row_limit || rows%30==0) && std::fflush(log_file)!=0){logging=false;log_failed=true;}
    } catch(...) {} // Logging is independent of ray behavior and never unwinds.
}
void notify_selection(const SelectionScope& scope,std::uint64_t a2,std::uint64_t a3,
    std::uint64_t a4,std::uint64_t a5,const int* a6,bool completed) noexcept {
    if(scope.previous || scope.caller!=image_base+selection_caller || !hand_callbacks.selection_complete)return;
    NativeHandSelection selection;
    selection.controller_live=scope.controller;
    selection.a2=a2;selection.a3=a3;selection.a4=a4;selection.a5=a5;selection.a6_live=a6;
    selection.sample=scope.sample;
    // A native exception cannot certify a partly computed target. The live
    // arguments still identify the scope whose previous certificate is invalid.
    selection.target_token=completed && scope.controller?read<std::uintptr_t>(scope.controller,0x520):0;
    // Gaze selections never certify hand actions (pinch presses).
    selection.used_hand_ray=scope.used&&!scope.gaze;selection.completed=completed;selection.nested_selection=scope.nested;
    selection.origin_calls=scope.origin_calls;
    hand_callbacks.selection_complete(hand_callbacks.context,selection);
}
void selection(void* controller,std::uint64_t a2,std::uint64_t a3,std::uint64_t a4,
               std::uint64_t a5,const int* a6) {
    if(!enabled.load(std::memory_order_acquire)){real_selection(controller,a2,a3,a4,a5,a6);return;}
    std::lock_guard transaction_lock(selection_mutex);
    SelectionScope scope;scope.controller=controller;scope.previous=active_selection;
    if(scope.previous)scope.previous->nested=true;
    scope.caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    RestoreSelection restore{scope};active_selection=&scope;
    // A blocked/unrecognized nested Selection must shadow an outer valid scope.
    // Otherwise its Origin call could accidentally reuse the outer transaction.
    try {scope.sample=hand_bridge->inspect();}
    catch(...) {scope.reason="snapshot_exception";}
    scope.flags=blocked_flags(controller);
    if(scope.caller!=image_base+selection_caller)scope.reason="selection_caller";
    else if(!controller)scope.reason="null_controller";
    else if(scope.flags)scope.reason=block_name(scope.flags);
    else {
        // arg4 is c_application_data only at this measured caller. Do not even
        // interpret an opaque arg4 from an unrecognized call path.
        scope.application=reinterpret_cast<const void*>(a4);
        if(const auto reason=application_block(scope.application))scope.reason=reason;
        else if(scope.sample.valid_ray)scope.eligible=true;
        // No pointing hand: select along the head view (keyboard Q/E in VR).
        else if(scope.sample.valid_gaze){scope.eligible=true;scope.gaze=true;}
        else scope.reason=hand_interaction_status_name(scope.sample.status);
    }
    if(controller)scope.used_direction=read<std::array<float,3>>(controller,0xaa0);
    try {real_selection(controller,a2,a3,a4,a5,a6);}
    catch(...) {
        scope.reason="native_exception";scope.restore_forward();record(scope);
        notify_selection(scope,a2,a3,a4,a5,a6,false);throw;
    }
    scope.restore_forward();record(scope);notify_selection(scope,a2,a3,a4,a5,a6,true);
}
void* origin(void* character,double* output,std::uint8_t mode,std::uint8_t option) {
    const auto result=real_origin(character,output,mode,option);
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(!enabled.load(std::memory_order_acquire)||!active_selection||caller!=image_base+origin_caller)return result;
    auto& scope=*active_selection;++scope.origin_calls;
    if(!output){scope.reason="null_origin_output";scope.restore_forward();return result;}
    std::memcpy(scope.head_origin.data(),output,sizeof(scope.head_origin));scope.used_origin=scope.head_origin;
    if(!scope.eligible)return result;
    if(result!=output){scope.reason="origin_return_pointer";scope.restore_forward();return result;}
    const auto now=HandInteractionClock::now();
    if(now<scope.sample.published_at || now-scope.sample.published_at>max_snapshot_age) {
        scope.reason=now<scope.sample.published_at?"clock_reversal_at_use":"stale_at_use";
        scope.restore_forward();return result;
    }
    const auto current_flags=blocked_flags(scope.controller);scope.flags|=current_flags;
    if(current_flags){scope.reason=block_name(current_flags);scope.restore_forward();return result;}
    if(const auto reason=application_block(scope.application)) {
        scope.reason=reason;scope.restore_forward();return result;
    }
    // Bind the pose once per Selection, but revalidate cancellation/focus after
    // the original origin helper. Never replace it with a newer valid pose in
    // the middle of this native target-selection transaction.
    try {
        const auto latest=hand_bridge->inspect();
        if(scope.gaze?!latest.valid_gaze:!latest.valid_ray) {
            scope.reason=scope.gaze?"stale_gaze":hand_interaction_status_name(latest.status);scope.restore_forward();return result;
        }
        if(latest.space_revision!=scope.sample.space_revision) {scope.reason="space_changed";scope.restore_forward();return result;}
        if(latest.cancellation_generation!=scope.sample.cancellation_generation) {
            scope.reason="snapshot_cancelled";scope.restore_forward();return result;
        }
    } catch(...) {scope.reason="snapshot_recheck_exception";scope.restore_forward();return result;}
    const auto& ray=scope.gaze?scope.sample.gaze:scope.sample.ray;
    for(unsigned i=0;i<3;++i)if(!std::isfinite(scope.head_origin[i])||!std::isfinite(ray.origin[i])||!std::isfinite(ray.direction[i])) {
        scope.reason="nonfinite_ray";scope.restore_forward();return result;
    }
    const double length=std::hypot(ray.direction[0],ray.direction[1],ray.direction[2]);
    if(!std::isfinite(length)||std::abs(length-1)>1e-4) {
        scope.reason="nonunit_ray";scope.restore_forward();return result;
    }
    const double distance=std::hypot(ray.origin[0]-scope.head_origin[0],ray.origin[1]-scope.head_origin[1],ray.origin[2]-scope.head_origin[2]);
    if(!std::isfinite(distance)||distance>3) {
        scope.reason="hand_origin_outside_head_reach";scope.restore_forward();return result;
    }
    if(!scope.saved) {
        std::memcpy(scope.saved_forward.data(),static_cast<const unsigned char*>(scope.controller)+0xaa0,scope.saved_forward.size());
        scope.saved=true;
    }
    for(unsigned i=0;i<3;++i)scope.used_direction[i]=static_cast<float>(ray.direction[i]/length);
    scope.used_origin=ray.origin;
    // Mutation begins only after the original confirmed its output pointer and
    // the native Selection reached this exact ray-building call site.
    std::memcpy(output,scope.used_origin.data(),sizeof(scope.used_origin));
    std::memcpy(static_cast<unsigned char*>(scope.controller)+0xaa0,scope.used_direction.data(),sizeof(scope.used_direction));
    scope.modified=true;scope.used=true;scope.reason=scope.gaze?"gaze_ray":"hand_ray";
    return result; // Preserve the native ABI's returned pointer exactly.
}
FILE* create_output(const std::filesystem::path& path) noexcept {
    const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return nullptr;
    const int descriptor=_open_osfhandle(reinterpret_cast<std::intptr_t>(handle),_O_BINARY|_O_WRONLY);
    if(descriptor==-1){CloseHandle(handle);return nullptr;}
    auto stream=_fdopen(descriptor,"wb");if(!stream)_close(descriptor);return stream;
}
}
NativeHandRayStatus install_native_hand_ray(std::uintptr_t base,bool sha_matched,
    const std::filesystem::path& trace_path,HandInteractionBridge& bridge,NativeHandRayCallbacks callbacks) noexcept {
    wchar_t option[8]{};
    if(GetEnvironmentVariableW(L"SWVR_HAND_RAY",option,8)!=1 || option[0]!=L'1')return NativeHandRayStatus::disabled;
    if(installed)return NativeHandRayStatus::already_installed;
    if(!sha_matched||!base)return NativeHandRayStatus::unsupported_profile;
    FILE* stream{};std::size_t created{};
    const std::array<void*,2> targets{reinterpret_cast<void*>(base+selection_rva),reinterpret_cast<void*>(base+origin_rva)};
    const auto rollback=[&] {
        while(created)MH_RemoveHook(targets[--created]);
        real_selection=nullptr;real_origin=nullptr;
        if(stream){std::fclose(stream);stream=nullptr;}
    };
    try {
        if(std::memcmp(targets[0],selection_prefix.data(),selection_prefix.size()) ||
           std::memcmp(targets[1],origin_prefix.data(),origin_prefix.size()))return NativeHandRayStatus::signature_mismatch;
        if(!trace_path.is_absolute())return NativeHandRayStatus::io_failure;
        stream=create_output(trace_path.wstring()+L".hand-ray-native.tsv");
        if(!stream)return NativeHandRayStatus::io_failure;
        const std::array<void*,2> detours{reinterpret_cast<void*>(selection),reinterpret_cast<void*>(origin)};
        const std::array<void**,2> originals{reinterpret_cast<void**>(&real_selection),reinterpret_cast<void**>(&real_origin)};
        for(;created<targets.size();++created)if(MH_CreateHook(targets[created],detours[created],originals[created])!=MH_OK)break;
        if(created!=targets.size()) {
            std::fputs("#state\thook_creation_failed\n",stream);rollback();return NativeHandRayStatus::hook_failure;
        }
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        std::fprintf(stream,"#profile\tc38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d\n"
            "#exe_base\t%llx\n#row_limit\t%llu\n#qpc_frequency\t%lld\n"
            "#policy\tpointing_only_unit_direction_no_actions_native_distance_checks_unchanged\n"
            "#flags\tbit0=494,bit1=49C,bit2=49F,bit3=4BC,bit4=4BD\n"
            "#application_gate\targ4_vtable_B17BF0,benchmark_byte_233\n"
            "#epoch\t0\tinstall\t0\n#state\thooks_created_not_enabled_by_backend\n",
            static_cast<unsigned long long>(base),static_cast<unsigned long long>(row_limit),static_cast<long long>(frequency.QuadPart));
        std::fputs("seq\tqpc\tthread\tcaller_rva\thand_sequence\txr_frame_id\thand\thand_status\toutcome\torigin_calls\tused"
            "\thead_x\thead_y\thead_z\torigin_x\torigin_y\torigin_z\tforward_x\tforward_y\tforward_z"
            "\tcomponent_after\tworld_after\tflags\tepoch\tepoch_row\tpinch_gesture_id\tpinch_down\n",stream);
        if(std::ferror(stream)||std::fflush(stream)!=0){rollback();return NativeHandRayStatus::io_failure;}
        image_base=base;hand_bridge=&bridge;log_file=stream;stream=nullptr;
        hand_callbacks=callbacks;
        rows=total_sequence=epoch=0;logging=true;log_failed=false;installed=true;
        enabled.store(true,std::memory_order_release);return NativeHandRayStatus::installed;
    } catch(...) {rollback();return NativeHandRayStatus::io_failure;}
}
const char* native_hand_ray_status_name(NativeHandRayStatus status) noexcept {
    switch(status) {
    case NativeHandRayStatus::disabled:return "disabled";
    case NativeHandRayStatus::installed:return "hooks_created";
    case NativeHandRayStatus::already_installed:return "already_installed";
    case NativeHandRayStatus::unsupported_profile:return "unsupported_profile";
    case NativeHandRayStatus::signature_mismatch:return "signature_mismatch";
    case NativeHandRayStatus::io_failure:return "io_failure";
    case NativeHandRayStatus::hook_failure:return "hook_failure";
    }
    return "unknown";
}
void flush_native_hand_ray() noexcept {
    try {std::lock_guard lock(log_mutex);if(log_file && std::fflush(log_file)!=0){logging=false;log_failed=true;}}catch(...){}
}
bool rearm_native_hand_ray_log() noexcept {
    try {
        std::lock_guard lock(log_mutex);
        if(!log_file || log_failed || std::ferror(log_file))return false;
        if(std::fprintf(log_file,"#epoch\t%llu\trearm\t%llu\n",
            static_cast<unsigned long long>(epoch+1),static_cast<unsigned long long>(total_sequence))<0 || std::fflush(log_file)!=0) {
            logging=false;log_failed=true;return false;
        }
        ++epoch;rows=0;logging=true;return true;
    } catch(...) {return false;}
}
}
