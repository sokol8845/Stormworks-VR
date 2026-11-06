#include "swvr/interaction_probe.hpp"
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <mutex>

namespace swvr {
namespace {
constexpr std::uintptr_t selection_rva=0x74f260,origin_rva=0x4e33a0,action_rva=0x78c7a0;
constexpr std::uintptr_t selection_origin_return=0x74f38d;
constexpr std::array<unsigned char,24> selection_prefix{
    0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,
    0x56,0x41,0x57,0x48,0x8d,0xa8,0x58,0xf9,0xff,0xff,0x48,0x81};
constexpr std::array<unsigned char,24> origin_prefix{
    0x40,0x55,0x53,0x57,0x48,0x8d,0xac,0x24,0x20,0xfe,0xff,0xff,
    0x48,0x81,0xec,0xe0,0x02,0x00,0x00,0x48,0x8b,0x05,0x56,0x4c};
constexpr std::array<unsigned char,24> action_prefix{
    0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x4c,0x89,0x40,0x18,0x48,
    0x89,0x50,0x10,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41};

// All arguments are in integer/pointer slots. Opaque 64-bit contexts avoid
// inventing C++ pointee types. The two phase/ID parameters are 32-bit integers.
using Selection=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,const int*);
using Action=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,int,int);
// Unlike the initial decompiler declaration, native epilogue 4E36C4 moves the
// output pointer to RAX. Preserve that return value for ALL original callers.
using Origin=void*(*)(void*,double*,std::uint8_t,std::uint8_t);
Selection real_selection{};
Action real_action{};
Origin real_origin{};
std::uintptr_t image_base{};
FILE* output{};
std::mutex output_mutex;
std::atomic<bool> observing{};
bool installed{};
std::uint64_t rows{};
constexpr std::uint64_t row_limit=10000;

template<class T>T read(const void* pointer,std::size_t offset) noexcept {
    T result{};
    std::memcpy(&result,static_cast<const unsigned char*>(pointer)+offset,sizeof(result));
    return result;
}
struct Snapshot {
    std::uintptr_t component{},world{},held_right{},held_left{};
    std::array<float,3> hit{},forward{};
    unsigned flags{};
};
Snapshot snapshot(const void* controller) noexcept {
    Snapshot value;
    if(!controller) return value;
    // Read only fields used by these version-guarded native functions. Do not
    // dereference the picked or held objects, which may have separate lifetimes.
    value.component=read<std::uintptr_t>(controller,0x520);
    value.world=read<std::uintptr_t>(controller,0x538);
    value.held_right=read<std::uintptr_t>(controller,0x558);
    value.held_left=read<std::uintptr_t>(controller,0x560);
    value.hit=read<std::array<float,3>>(controller,0x528);
    value.forward=read<std::array<float,3>>(controller,0xaa0);
    value.flags=(read<unsigned char>(controller,0x494)?1u:0u)|
                (read<unsigned char>(controller,0x4bc)?2u:0u)|
                (read<unsigned char>(controller,0x4bd)?4u:0u);
    return value;
}
struct Ray {
    std::array<double,3> origin{};
    std::array<float,3> forward{};
    unsigned calls{};
};
struct SelectionScope {
    void* controller{};
    Ray ray{};
    SelectionScope* previous{};
};
thread_local SelectionScope* active_selection{};
struct RestoreScope {
    SelectionScope* previous;
    ~RestoreScope(){active_selection=previous;}
};

void record(const char* event,void* controller,std::uintptr_t caller,
            const std::array<std::uint64_t,5>& contexts,int action,int phase,
            const Ray& ray,const Snapshot& before,const Snapshot& after) noexcept {
    if(!observing.load(std::memory_order_relaxed)) return;
    try {
        LARGE_INTEGER ticks{};QueryPerformanceCounter(&ticks);
        const auto thread=GetCurrentThreadId();
        std::lock_guard lock(output_mutex);
        if(!output || rows>=row_limit) return;
        const auto caller_offset=caller>=image_base?caller-image_base:0;
        ++rows;
        const int written=std::fprintf(output,
            "%llu\t%lld\t%lu\t%s\t%llx\t%llx\t%llx\t%llx\t%llx\t%llx\t%llx\t%d\t%d\t%u"
            "\t%.17g\t%.17g\t%.17g\t%.9g\t%.9g\t%.9g"
            "\t%llx\t%llx\t%llx\t%llx\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g"
            "\t%llx\t%llx\t%llx\t%llx\t%u\t%u\n",
            static_cast<unsigned long long>(rows),static_cast<long long>(ticks.QuadPart),thread,event,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(controller)),
            static_cast<unsigned long long>(caller_offset),
            static_cast<unsigned long long>(contexts[0]),static_cast<unsigned long long>(contexts[1]),
            static_cast<unsigned long long>(contexts[2]),static_cast<unsigned long long>(contexts[3]),
            static_cast<unsigned long long>(contexts[4]),action,phase,ray.calls,
            ray.origin[0],ray.origin[1],ray.origin[2],ray.forward[0],ray.forward[1],ray.forward[2],
            static_cast<unsigned long long>(before.component),static_cast<unsigned long long>(after.component),
            static_cast<unsigned long long>(before.world),static_cast<unsigned long long>(after.world),
            before.hit[0],before.hit[1],before.hit[2],after.hit[0],after.hit[1],after.hit[2],
            static_cast<unsigned long long>(before.held_right),static_cast<unsigned long long>(after.held_right),
            static_cast<unsigned long long>(before.held_left),static_cast<unsigned long long>(after.held_left),
            before.flags,after.flags);
        if(written<0) {observing=false;return;}
        if(rows==row_limit) {
            std::fprintf(output,"#state\tlimit_reached\n");
            observing=false;
        }
        if(rows==row_limit || action>=0 || rows%30==0) std::fflush(output);
    } catch(...) {} // Diagnostics never unwind through game frames.
}

void selection(void* controller,std::uint64_t a2,std::uint64_t a3,std::uint64_t a4,
               std::uint64_t a5,const int* a6) {
    if(!observing.load(std::memory_order_relaxed)) {real_selection(controller,a2,a3,a4,a5,a6);return;}
    const auto before=snapshot(controller);
    SelectionScope scope{controller,{},active_selection};
    RestoreScope restore{scope.previous};
    active_selection=&scope;
    real_selection(controller,a2,a3,a4,a5,a6);
    active_selection=scope.previous;
    record("selection",controller,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
           {a2,a3,a4,a5,reinterpret_cast<std::uintptr_t>(a6)},-1,-1,scope.ray,before,snapshot(controller));
}
void* origin(void* character,double* destination,std::uint8_t mode,std::uint8_t option) {
    const auto result=real_origin(character,destination,mode,option);
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(observing.load(std::memory_order_relaxed) && active_selection && destination &&
       caller==image_base+selection_origin_return) {
        auto& scope=*active_selection;
        std::memcpy(scope.ray.origin.data(),destination,sizeof(scope.ray.origin));
        scope.ray.forward=read<std::array<float,3>>(scope.controller,0xaa0);
        ++scope.ray.calls;
    }
    return result;
}
void action(void* controller,std::uint64_t a2,std::uint64_t a3,std::uint64_t a4,
            std::uint64_t a5,int id,int phase) {
    const bool sample=observing.load(std::memory_order_relaxed) && (id==0x13 || id==0x14);
    Snapshot before;
    if(sample) before=snapshot(controller);
    real_action(controller,a2,a3,a4,a5,id,phase);
    if(sample) {
        Ray ray;ray.forward=before.forward;
        record("action",controller,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
               {a2,a3,a4,a5,0},id,phase,ray,before,snapshot(controller));
    }
}

FILE* create_output(const std::filesystem::path& path) noexcept {
    const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE) return nullptr;
    const int descriptor=_open_osfhandle(reinterpret_cast<std::intptr_t>(handle),_O_BINARY|_O_WRONLY);
    if(descriptor==-1) {CloseHandle(handle);return nullptr;}
    auto stream=_fdopen(descriptor,"wb");
    if(!stream) _close(descriptor); // Also closes the owned Win32 handle.
    return stream;
}
}

InteractionProbeStatus install_interaction_probe(std::uintptr_t base,bool sha_matched,
                                                 const std::filesystem::path& trace_path) noexcept {
    wchar_t option[8]{};
    if(GetEnvironmentVariableW(L"SWVR_INTERACTION_PROBE",option,8)==0 || std::wcscmp(option,L"1")!=0)
        return InteractionProbeStatus::disabled;
    if(installed) return InteractionProbeStatus::already_installed;
    if(!sha_matched || !base) return InteractionProbeStatus::unsupported_profile;
    try {
        const auto matches=[&](std::uintptr_t rva,const auto& prefix) {
            return std::memcmp(reinterpret_cast<const void*>(base+rva),prefix.data(),prefix.size())==0;
        };
        if(!matches(selection_rva,selection_prefix) || !matches(origin_rva,origin_prefix) || !matches(action_rva,action_prefix))
            return InteractionProbeStatus::signature_mismatch;
        if(!trace_path.is_absolute()) return InteractionProbeStatus::io_failure;
        auto stream=create_output(trace_path.wstring()+L".interaction.tsv");
        if(!stream) return InteractionProbeStatus::io_failure;
        const std::array<void*,3> targets{reinterpret_cast<void*>(base+selection_rva),
            reinterpret_cast<void*>(base+origin_rva),reinterpret_cast<void*>(base+action_rva)};
        const std::array<void*,3> detours{reinterpret_cast<void*>(selection),reinterpret_cast<void*>(origin),reinterpret_cast<void*>(action)};
        const std::array<void**,3> originals{reinterpret_cast<void**>(&real_selection),
            reinterpret_cast<void**>(&real_origin),reinterpret_cast<void**>(&real_action)};
        std::size_t created{};
        for(;created<targets.size();++created) if(MH_CreateHook(targets[created],detours[created],originals[created])!=MH_OK) break;
        if(created!=targets.size()) {
            // Contract: no hooks enabled yet. Remove only our successfully
            // created hooks; never initialize/enable/uninitialize MinHook here.
            while(created) MH_RemoveHook(targets[--created]);
            std::fprintf(stream,"#state\thook_creation_failed\n");std::fclose(stream);
            return InteractionProbeStatus::hook_failure;
        }
        image_base=base;output=stream;rows=0;
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        std::fprintf(output,"#profile\tc38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d\n"
            "#exe_base\t%llx\n#row_limit\t%llu\n#qpc_frequency\t%lld\n#flags\tbit0=494,bit1=4BC,bit2=4BD\n"
            "#state\thooks_created_not_enabled_by_probe\n",
            static_cast<unsigned long long>(base),static_cast<unsigned long long>(row_limit),
            static_cast<long long>(frequency.QuadPart));
        std::fputs("seq\tqpc\tthread\tevent\tcontroller\tcaller_rva\tcontext2\tcontext3\tcontext4\tcontext5\tcontext6"
            "\taction\tphase\torigin_calls\torigin_x\torigin_y\torigin_z\tforward_x\tforward_y\tforward_z"
            "\tcomponent_before\tcomponent_after\tworld_before\tworld_after"
            "\thit_before_x\thit_before_y\thit_before_z\thit_after_x\thit_after_y\thit_after_z"
            "\theld_right_before\theld_right_after\theld_left_before\theld_left_after\tflags_before\tflags_after\n",output);
        std::fflush(output);installed=true;observing=true;
        return InteractionProbeStatus::installed;
    } catch(...) {return InteractionProbeStatus::io_failure;}
}
const char* interaction_probe_status_name(InteractionProbeStatus status) noexcept {
    switch(status) {
    case InteractionProbeStatus::disabled:return "disabled";
    case InteractionProbeStatus::installed:return "hooks_created";
    case InteractionProbeStatus::already_installed:return "already_installed";
    case InteractionProbeStatus::unsupported_profile:return "unsupported_profile";
    case InteractionProbeStatus::signature_mismatch:return "signature_mismatch";
    case InteractionProbeStatus::io_failure:return "io_failure";
    case InteractionProbeStatus::hook_failure:return "hook_failure";
    }
    return "unknown";
}
void flush_interaction_probe() noexcept {
    try {std::lock_guard lock(output_mutex);if(output)std::fflush(output);} catch(...) {}
}
}
