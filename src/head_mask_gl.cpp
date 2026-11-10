#include "swvr/head_mask_gl.hpp"
#include "swvr/head_mask_shader.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace swvr {
struct GlHeadMask::Impl {
    struct Program {GLint mask{-1},bone{-1},weight{-1};};
    struct Context {
        GLint(APIENTRY* location)(GLuint,const char*){};
        void(APIENTRY* integer)(GLuint,GLint,GLint*){};
        void(APIENTRY* scalar)(GLuint,GLint,GLfloat*){};
        void(APIENTRY* set_integer)(GLint,GLint){};
        void(APIENTRY* program_integer)(GLuint,GLenum,GLint*){};
        std::unordered_map<GLuint,Program> programs;
    };
    mutable std::mutex mutex;
    HeadMaskGlResolver resolver{};
    std::unordered_map<HGLRC,Context> contexts;
    GlHeadMaskStats counters{};
    template<class T>T resolve(const char* name) const {
        const auto pointer=resolver(name);
        if(!pointer || pointer==reinterpret_cast<void*>(1) || pointer==reinterpret_cast<void*>(2) ||
           pointer==reinterpret_cast<void*>(3) || pointer==reinterpret_cast<void*>(-1))
            throw std::runtime_error(std::string("Head mask original GL entry point unavailable: ")+name);
        return reinterpret_cast<T>(pointer);
    }
    Context& current(HGLRC handle) {
        auto found=contexts.find(handle);if(found!=contexts.end())return found->second;
        if(!resolver)throw std::logic_error("Configure head mask with an original GL resolver before drawing");
        Context context;
        context.location=resolve<decltype(context.location)>("glGetUniformLocation");
        context.integer=resolve<decltype(context.integer)>("glGetUniformiv");
        context.scalar=resolve<decltype(context.scalar)>("glGetUniformfv");
        context.set_integer=resolve<decltype(context.set_integer)>("glUniform1i");
        context.program_integer=resolve<decltype(context.program_integer)>("glGetProgramiv");
        return contexts.emplace(handle,std::move(context)).first->second;
    }
};
GlHeadMask::GlHeadMask():impl_(std::make_unique<Impl>()) {}
GlHeadMask::~GlHeadMask()=default;
void GlHeadMask::configure(HeadMaskGlResolver resolver) {
    if(!resolver)throw std::invalid_argument("Head mask GL resolver must not be null");
    std::lock_guard lock(impl_->mutex);impl_->resolver=resolver;impl_->contexts.clear();
}
bool GlHeadMask::before_draw(bool vr_active) {
    std::lock_guard lock(impl_->mutex);++impl_->counters.calls;
    const auto handle=wglGetCurrentContext();if(!handle)return false;
    GLint active{};glGetIntegerv(0x8B8D,&active); // CURRENT_PROGRAM
    if(active<=0)return false;
    const auto program=static_cast<GLuint>(active);auto& api=impl_->current(handle);
    auto found=api.programs.find(program);
    if(found==api.programs.end()) {
        Impl::Program entry;GLint linked{};api.program_integer(program,0x8B82,&linked); // LINK_STATUS
        if(linked)entry.mask=api.location(program,head_mask_uniform_name.data());
        if(entry.mask>=0) {
            entry.bone=api.location(program,"bone_clip_index");
            entry.weight=api.location(program,"bone_clip_weight");
        }
        found=api.programs.emplace(program,entry).first;++impl_->counters.program_cache_misses;
    }
    const auto& entry=found->second;if(entry.mask<0)return false;
    ++impl_->counters.patched_program_calls;
    GLint index{-1};GLfloat weight{};
    if(entry.bone>=0)api.integer(program,entry.bone,&index);
    if(entry.weight>=0)api.scalar(program,entry.weight,&weight);
    // This exact build's local first-person branch supplies the head node and
    // 0.25 skin weight. Read each time: programs are shared by multiple actors.
    const bool candidate=entry.bone>=0 && entry.weight>=0 && index>=0 &&
                         std::isfinite(weight) && std::abs(weight-.25f)<=1e-6f;
    const bool masked=vr_active && candidate;
    if(candidate)++impl_->counters.candidate_calls;
    if(masked)++impl_->counters.masked_calls;
    // Always write the desired value, including 0. Never trust a previous draw's
    // state when the current program may now render another actor or mirror.
    api.set_integer(entry.mask,masked?1:0);++impl_->counters.uniform_writes;
    return masked;
}
GlProgramInterest GlHeadMask::interest(void* native_context,unsigned program) const {
    std::lock_guard lock(impl_->mutex);
    const auto api=impl_->contexts.find(static_cast<HGLRC>(native_context));
    if(api==impl_->contexts.end())return GlProgramInterest::unknown;
    const auto found=api->second.programs.find(program);
    if(found==api->second.programs.end())return GlProgramInterest::unknown;
    return found->second.mask>=0?GlProgramInterest::active:GlProgramInterest::none;
}
void GlHeadMask::program_linked(unsigned program) {
    std::lock_guard lock(impl_->mutex);
    for(auto& [handle,context]:impl_->contexts) {static_cast<void>(handle);context.programs.erase(program);}
}
void GlHeadMask::context_destroyed(void* native_context) {
    std::lock_guard lock(impl_->mutex);impl_->contexts.erase(static_cast<HGLRC>(native_context));
}
GlHeadMaskStats GlHeadMask::stats() const {std::lock_guard lock(impl_->mutex);return impl_->counters;}
}
