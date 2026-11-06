#include "swvr/driver_wheel_gl.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace swvr {
struct GlDriverWheel::Impl {
    struct Program {
        int world{-1},active{-1},count{-1},points{-1},to_seat{-1},to_model{-1},angle{-1},old_angle{-1};
        bool uploaded{},has_world{};Matrix4d model{};
    };
    struct Context {
        int(APIENTRY* location)(unsigned,const char*){};
        void(APIENTRY* matrix)(int,int,unsigned char,const float*){};
        void(APIENTRY* integer)(int,int){};void(APIENTRY* scalar)(int,float){};void(APIENTRY* vectors)(int,int,const float*){};
        std::map<unsigned,Program> programs;
    };
    mutable std::recursive_mutex mutex;void*(*resolver)(const char*){};
    std::map<HGLRC,Context> contexts;std::vector<Vec3> points;DriverWheelGlStats counters;
    DriverSeatSnapshot seat;Matrix4d expected{};bool active{},rebased{};
    HGLRC eye_context{};std::thread::id eye_thread{};std::chrono::steady_clock::time_point eye_time;
    std::uint64_t frame{};std::uintptr_t previous_seat{};float angle{},old_angle{};
    Context* restoring_context{};Program* restoring_program{};unsigned restoring_name{};
    template<class T>T resolve(const char* name){auto p=resolver(name);if(!p||reinterpret_cast<std::uintptr_t>(p)<=3||p==reinterpret_cast<void*>(-1))throw std::runtime_error("Driver wheel GL function unavailable");return reinterpret_cast<T>(p);}
    Context& context(HGLRC handle){
        const auto found=contexts.find(handle);if(found!=contexts.end())return found->second;
        if(!resolver)throw std::logic_error("Driver wheel GL not configured");
        Context c;c.location=resolve<decltype(c.location)>("glGetUniformLocation");c.matrix=resolve<decltype(c.matrix)>("glUniformMatrix4fv");
        c.integer=resolve<decltype(c.integer)>("glUniform1i");c.scalar=resolve<decltype(c.scalar)>("glUniform1f");c.vectors=resolve<decltype(c.vectors)>("glUniform3fv");
        return contexts.emplace(handle,std::move(c)).first->second;
    }
    Program* program(Context& c,unsigned name){
        const auto found=c.programs.find(name);if(found!=c.programs.end())return &found->second;
        if(c.programs.size()>=512)return nullptr;
        Program p;p.active=c.location(name,"swvr_wheel_active");
        if(p.active>=0){
            p.world=c.location(name,"mat_world");p.count=c.location(name,"swvr_wheel_count");p.points=c.location(name,"swvr_wheel_points[0]");
            p.to_seat=c.location(name,"swvr_wheel_to_seat");p.to_model=c.location(name,"swvr_wheel_to_model");
            p.angle=c.location(name,"swvr_wheel_angle");p.old_angle=c.location(name,"swvr_wheel_old_angle");
            if(p.world<0||p.count<0||p.points<0||p.to_seat<0||p.to_model<0||p.angle<0)p.active=-1;
        }
        return &c.programs.emplace(name,p).first->second;
    }
};
GlDriverWheel::GlDriverWheel():impl_(std::make_unique<Impl>()){}
GlDriverWheel::~GlDriverWheel()=default;
void GlDriverWheel::configure(void*(*resolver)(const char*),std::vector<Vec3> points){
    if(!resolver||points.empty()||points.size()>driver_wheel_point_limit)throw std::invalid_argument("Invalid Driver wheel GL configuration");
    std::lock_guard lock(impl_->mutex);restore();impl_->resolver=resolver;impl_->points=std::move(points);impl_->contexts.clear();impl_->active=false;
}
void GlDriverWheel::matrix_uploaded(int location,int count,bool transpose,const float* values){
    std::lock_guard lock(impl_->mutex);auto handle=wglGetCurrentContext();if(!handle)return;
    GLint name{};glGetIntegerv(0x8B8D,&name);if(name<=0)return;
    auto& c=impl_->context(handle);auto* p=impl_->program(c,static_cast<unsigned>(name));
    if(!p||p->active<0||location!=p->world)return;
    p->has_world=count==1&&!transpose&&values;if(p->has_world)std::copy_n(values,16,p->model.begin());
}
void GlDriverWheel::program_linked(unsigned name){
    std::lock_guard lock(impl_->mutex);restore();
    // Shared GL objects may have been linked from a different context.
    for(auto& [handle,c]:impl_->contexts){static_cast<void>(handle);c.programs.erase(name);}
}
void GlDriverWheel::context_destroyed(void* handle){
    std::lock_guard lock(impl_->mutex);if(impl_->eye_context==handle){impl_->active=false;impl_->restoring_context=nullptr;impl_->restoring_program=nullptr;}
    impl_->contexts.erase(static_cast<HGLRC>(handle));
}
void GlDriverWheel::begin_eye(DriverSeatSnapshot seat,SteeringCommand command,Matrix4d expected,std::uint64_t frame){
    std::lock_guard lock(impl_->mutex);restore();auto& s=*impl_;
    s.eye_context=wglGetCurrentContext();s.eye_thread=std::this_thread::get_id();s.eye_time=std::chrono::steady_clock::now();s.expected=expected;s.seat=seat;s.rebased=false;
    const bool fresh=seat.valid&&seat.seat&&seat.observed<=s.eye_time&&s.eye_time-seat.observed<=std::chrono::milliseconds(100)&&driver_wheel_rigid(seat.world)&&driver_wheel_rigid(expected);
    if(frame!=s.frame||seat.seat!=s.previous_seat){
        s.old_angle=fresh&&seat.seat==s.previous_seat?s.angle:0;
        s.angle=fresh&&command.active&&command.seat==seat.seat&&std::isfinite(command.angle_radians)?std::clamp(command.angle_radians,-3.14159265f,3.14159265f):0;
        s.frame=frame;s.previous_seat=fresh?seat.seat:0;
    }
    s.active=fresh&&(s.angle!=0||s.old_angle!=0);
    ++s.counters.eyes;s.counters.identified_eyes+=fresh;
}
void GlDriverWheel::camera_rebased(const Matrix4d& actual){
    std::lock_guard lock(impl_->mutex);auto& s=*impl_;
    if(!s.active||s.rebased||s.eye_context!=wglGetCurrentContext()||s.eye_thread!=std::this_thread::get_id()||!driver_wheel_rigid(actual))return;
    for(int i=0;i<3;++i)s.seat.world[12+i]+=actual[12+i]-s.expected[12+i];s.rebased=true;
}
GlDriverWheel::Draw GlDriverWheel::before_draw(){
    std::lock_guard lock(impl_->mutex);auto& s=*impl_;
    if(!s.active||!s.rebased||s.eye_context!=wglGetCurrentContext()||s.eye_thread!=std::this_thread::get_id()||s.restoring_program||std::chrono::steady_clock::now()-s.eye_time>std::chrono::milliseconds(100))return {};
    GLint name{};glGetIntegerv(0x8B8D,&name);if(name<=0)return {};
    auto& c=s.context(s.eye_context);auto* p=s.program(c,static_cast<unsigned>(name));
    if(!p||p->active<0||!p->has_world||!driver_wheel_rigid(p->model))return {};
    const auto to_seat=driver_wheel_product(driver_wheel_inverse(s.seat.world),p->model);
    const auto to_model=driver_wheel_inverse(to_seat);
    Matrix4 a{},b{};for(int i=0;i<16;++i){a[i]=static_cast<float>(to_seat[i]);b[i]=static_cast<float>(to_model[i]);}
    if(!p->uploaded){static_assert(sizeof(Vec3)==3*sizeof(float));c.vectors(p->points,static_cast<int>(s.points.size()),&s.points.front().x);c.integer(p->count,static_cast<int>(s.points.size()));p->uploaded=true;}
    c.matrix(p->to_seat,1,GL_FALSE,a.data());c.matrix(p->to_model,1,GL_FALSE,b.data());c.scalar(p->angle,s.angle);
    if(p->old_angle>=0)c.scalar(p->old_angle,s.old_angle);
    s.restoring_context=&c;s.restoring_program=p;s.restoring_name=static_cast<unsigned>(name);c.integer(p->active,1);
    ++s.counters.draws;s.counters.depth_draws+=p->old_angle<0;return Draw(this);
}
void GlDriverWheel::restore() noexcept {
    try{std::lock_guard lock(impl_->mutex);auto& s=*impl_;auto* c=std::exchange(s.restoring_context,nullptr);auto* p=std::exchange(s.restoring_program,nullptr);
        if(!c||!p||wglGetCurrentContext()!=s.eye_context)return;
        GLint name{};glGetIntegerv(0x8B8D,&name);if(static_cast<unsigned>(name)==s.restoring_name)c->integer(p->active,0);
    }catch(...){}
}
void GlDriverWheel::end_eye() noexcept {try{std::lock_guard lock(impl_->mutex);restore();impl_->active=false;}catch(...){}}
GlDriverWheel::Draw::Draw(Draw&& other) noexcept:owner_(std::exchange(other.owner_,nullptr)){}
GlDriverWheel::Draw::~Draw(){if(owner_)owner_->restore();}
DriverWheelGlStats GlDriverWheel::stats()const{std::lock_guard lock(impl_->mutex);return impl_->counters;}
}
