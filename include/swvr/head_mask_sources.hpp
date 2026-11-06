#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include "swvr/head_mask_shader.hpp"

namespace swvr {
// GL entry points used by the injector (x64: one calling convention).
struct HeadMaskShaderApi {
    void(*source)(unsigned shader,int count,const char* const* strings,const int* lengths){};
    void(*compile)(unsigned shader){};
    void(*get_shader)(unsigned shader,unsigned pname,int* value){}; // May be null.
};
using TraceNote=void(*)(const char* key,const char* message) noexcept;
// In-memory injection for glShaderSource/glCompileShader hooks; defaults to the
// head mask, also reused for Driver wheel vertex shaders. Keeps the ORIGINAL
// source of each patched shader (per context and shader) so a
// failed compile can be retried with the unmodified game shader.
class HeadMaskShaderSources {
public:
    using Patch=HeadMaskShaderPatch(*)(std::string_view);
    explicit HeadMaskShaderSources(unsigned type=0x8B30,Patch patch=patch_head_mask_fragment,const char* label="head_mask_shader")
        :type_(type),patch_(patch),label_(label){}
    // True when the patched source has been submitted; the hook must then NOT
    // submit the original. False leaves submission to the caller and drops any
    // older backup for this shader.
    bool submit(std::uintptr_t context,unsigned shader,int count,const char* const* strings,
                const int* lengths,const HeadMaskShaderApi&,TraceNote);
    // After the original glCompileShader: restore and recompile on failure.
    void compiled(std::uintptr_t context,unsigned shader,const HeadMaskShaderApi&,TraceNote);
    void forget(std::uintptr_t context,unsigned shader);
    void context_destroyed(std::uintptr_t context);
    unsigned patched() const noexcept;
    unsigned fallbacks() const noexcept;
    std::size_t backups() const;
private:
    unsigned type_;Patch patch_;const char* label_;
    using Key=std::pair<std::uintptr_t,unsigned>;
    mutable std::mutex mutex_;
    std::map<Key,std::string> backups_;
    std::size_t bytes_{};
    std::atomic<unsigned> patched_{},fallbacks_{};
};
}
