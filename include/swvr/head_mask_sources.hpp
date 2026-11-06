#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace swvr {
// GL entry points used by the injector (x64: one calling convention).
struct HeadMaskShaderApi {
    void(*source)(unsigned shader,int count,const char* const* strings,const int* lengths){};
    void(*compile)(unsigned shader){};
    void(*get_shader)(unsigned shader,unsigned pname,int* value){}; // May be null.
};
using TraceNote=void(*)(const char* key,const char* message) noexcept;
// In-memory head-mask injection for glShaderSource/glCompileShader hooks. Keeps
// the ORIGINAL source of each patched fragment (per context and shader) so a
// failed compile can be retried with the unmodified game shader.
class HeadMaskShaderSources {
public:
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
    using Key=std::pair<std::uintptr_t,unsigned>;
    mutable std::mutex mutex_;
    std::map<Key,std::string> backups_;
    std::size_t bytes_{};
    std::atomic<unsigned> patched_{},fallbacks_{};
};
}
