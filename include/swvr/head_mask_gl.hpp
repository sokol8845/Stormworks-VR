#pragma once
#include <cstdint>
#include <memory>

namespace swvr {
using HeadMaskGlResolver=void*(*)(const char* name);
struct GlHeadMaskStats {
    std::uint64_t calls{},patched_program_calls{},candidate_calls{},masked_calls{};
    std::uint64_t uniform_writes{},program_cache_misses{};
};
// Activates the injected fragment uniform only for the measured native head
// signature. Owns no GL objects and never binds a different program/context.
class GlHeadMask {
public:
    GlHeadMask();
    ~GlHeadMask();
    GlHeadMask(const GlHeadMask&)=delete;
    GlHeadMask& operator=(const GlHeadMask&)=delete;
    // Must return ORIGINAL GL entry points, especially glUniform1i and
    // glGetUniformLocation, to avoid re-entering the caller's instrumentation.
    void configure(HeadMaskGlResolver resolver);
    bool before_draw(bool vr_active);
    // Call after every link attempt. Conservatively invalidates this name in
    // all cached contexts, including contexts that may share the program.
    void program_linked(unsigned program);
    // Call when an HGLRC is deleted, so reused handle/name pairs cannot be stale.
    void context_destroyed(void* native_context);
    GlHeadMaskStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
