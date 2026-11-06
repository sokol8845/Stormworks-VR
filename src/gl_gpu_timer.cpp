#include "swvr/gl_gpu_timer.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <array>
#include <stdexcept>
namespace swvr {
namespace {template<class F>F get(const char* name){const auto p=wglGetProcAddress(name);return !p||p==reinterpret_cast<PROC>(1)||p==reinterpret_cast<PROC>(2)||p==reinterpret_cast<PROC>(3)||p==reinterpret_cast<PROC>(-1)?nullptr:reinterpret_cast<F>(p);}}
struct GlGpuTimer::Impl {
    struct Pair {std::array<GLuint,2> query{};std::uint64_t frame{};double budget{};bool pending{};};
    std::array<Pair,16> pairs{};int active{-1};unsigned cursor{};std::uint64_t skipped{};
    HGLRC context{};DWORD thread{};bool checked{},supported{};
    void(APIENTRY* gen)(GLsizei,GLuint*){};void(APIENTRY* remove)(GLsizei,const GLuint*){};
    void(APIENTRY* timestamp)(GLuint,GLenum){};void(APIENTRY* available)(GLuint,GLenum,GLint*){};
    void(APIENTRY* result)(GLuint,GLenum,unsigned long long*){};
    bool initialize(){
        if(checked){if(context&&(context!=wglGetCurrentContext()||thread!=GetCurrentThreadId()))throw std::logic_error("GPU timer context changed");return supported;}
        if(!wglGetCurrentContext())return false;checked=true;context=wglGetCurrentContext();thread=GetCurrentThreadId();
        gen=get<decltype(gen)>("glGenQueries");remove=get<decltype(remove)>("glDeleteQueries");timestamp=get<decltype(timestamp)>("glQueryCounter");available=get<decltype(available)>("glGetQueryObjectiv");result=get<decltype(result)>("glGetQueryObjectui64v");
        supported=gen&&remove&&timestamp&&available&&result;return supported;
    }
    ~Impl(){if(remove&&context==wglGetCurrentContext()&&thread==GetCurrentThreadId())for(auto& p:pairs)if(p.query[0])remove(2,p.query.data());}
};
GlGpuTimer::GlGpuTimer():impl_(std::make_unique<Impl>()){}GlGpuTimer::~GlGpuTimer()=default;
void GlGpuTimer::begin(std::uint64_t frame,double budget){auto& s=*impl_;if(!s.initialize())return;if(s.active>=0)throw std::logic_error("GPU timer scope already open");
    for(unsigned n=0;n<s.pairs.size();++n){const unsigned index=(s.cursor+n)%static_cast<unsigned>(s.pairs.size());auto& p=s.pairs[index];if(p.pending)continue;
        if(!p.query[0])s.gen(2,p.query.data());if(!p.query[0]||!p.query[1]){++s.skipped;return;}
        p.frame=frame;p.budget=budget;s.timestamp(p.query[0],0x8E28);s.active=static_cast<int>(index);s.cursor=(index+1)%static_cast<unsigned>(s.pairs.size());return;}++s.skipped;
}
void GlGpuTimer::end(){auto& s=*impl_;if(s.active<0)return;s.initialize();auto& p=s.pairs[static_cast<unsigned>(s.active)];s.timestamp(p.query[1],0x8E28);p.pending=true;s.active=-1;}
void GlGpuTimer::cancel()noexcept{if(impl_->active>=0){impl_->active=-1;++impl_->skipped;}}
std::optional<GpuTiming> GlGpuTimer::poll(){auto& s=*impl_;if(!s.checked||!s.initialize())return {};for(auto& p:s.pairs)if(p.pending){GLint ready[2]{};for(unsigned i=0;i<2;++i)s.available(p.query[i],0x8867,&ready[i]);if(!ready[0]||!ready[1])continue;
        unsigned long long begin{},end{};s.result(p.query[0],0x8866,&begin);s.result(p.query[1],0x8866,&end);p.pending=false;if(end>=begin)return GpuTiming{p.frame,static_cast<double>(end-begin)/1e6,p.budget};++s.skipped;}return {};}
bool GlGpuTimer::supported()const noexcept{return impl_->supported;}std::uint64_t GlGpuTimer::skipped()const noexcept{return impl_->skipped;}
unsigned GlGpuTimer::pending()const noexcept{unsigned count{};for(const auto& p:impl_->pairs)count+=p.pending;return count;}
}
