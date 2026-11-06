#include "swvr/hand_steering.hpp"
#include "swvr/hand_closure.hpp"
#include <algorithm>
#include <cmath>
namespace swvr {
namespace {constexpr float pi=3.14159265358979323846f;
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}
void HandSteering::clear(bool geometry){command_={};armed_={};closed_={};angle_=0;if(geometry)wheel_.reset();}
void HandSteering::reset(){std::lock_guard lock(mutex_);clear(true);seat_=0;time_.reset();frame_=0;}
SteeringCommand HandSteering::inspect(Clock::time_point now){std::lock_guard lock(mutex_);if(now==Clock::time_point::max())now=Clock::now();if(now<published_||now-published_>std::chrono::milliseconds(100))clear(false);return command_;}
void HandSteering::publish(const XrRuntimeFrame& f,const GestureSettings& settings,std::uintptr_t seat,bool allowed,Clock::time_point now){
    std::lock_guard lock(mutex_);
    if(now<published_){clear(true);return;}
    const bool valid=allowed&&seat&&f.running&&f.focused&&f.should_render&&!f.terminal&&f.id&&f.predicted_display_time_ns>=0&&
        f.position_valid&&f.orientation_valid&&f.position_tracked&&f.orientation_tracked;
    if(!valid){clear(false);return;}
    if(seat!=seat_||f.space_revision!=revision_){clear(true);seat_=seat;revision_=f.space_revision;time_.reset();}
    std::array<std::optional<bool>,2> closed;
    for(unsigned h=0;h<2;++h){closed[h]=hand_closed(f.hands[h],closed_[h],settings.arm_swing);if(!closed[h]||!*closed[h]){command_.hands[h]=closed_[h]=false;armed_[h]=closed[h].has_value();}}
    if(time_&&(f.id<=frame_||f.predicted_display_time_ns<=*time_)){
        if(f.id!=frame_||f.predicted_display_time_ns!=*time_)clear(false);
        if(!command_.hands[0]&&!command_.hands[1]){command_={};angle_=0;}return;
    }
    const auto dt=time_?f.predicted_display_time_ns-*time_:0;
    if(dt>100'000'000)clear(false);
    time_=f.predicted_display_time_ns;frame_=f.id;published_=now;
    try {
        const auto head=gravity_aligned_origin({f.eyes[0].pose,f.eyes[1].pose});
        std::array<Vec3,2> p{f.hands[0].joints[1].pose.position,f.hands[1].joints[1].pose.position};
        if(!wheel_){
            if(!closed[0]||!closed[1]||!*closed[0]||!*closed[1]||!armed_[0]||!armed_[1]||!finite(p[0])||!finite(p[1]))return;
            const auto a=compose(inverse(head),Pose{{},p[0]}).position,b=compose(inverse(head),Pose{{},p[1]}).position;
            const float span=std::hypot(b.x-a.x,b.y-a.y);
            if(span<.16f||span>.65f||std::abs(a.z-b.z)>.15f||b.x-a.x<.12f||a.z>-.10f||b.z>-.10f||a.z<-.85f||b.z<-.85f||a.y>.05f||b.y>.05f||a.y<-.8f||b.y<-.8f)return;
            wheel_=Pose{head.orientation,{(p[0].x+p[1].x)/2,(p[0].y+p[1].y)/2,(p[0].z+p[1].z)/2}};radius_=span/2;
        }
        float sum{};unsigned contributors{};
        for(unsigned h=0;h<2;++h){
            if(!closed[h]||!*closed[h]||!armed_[h]||!finite(p[h])){command_.hands[h]=closed_[h]=false;continue;}
            const auto local=compose(inverse(*wheel_),Pose{{},p[h]}).position;
            const float radius=std::hypot(local.x,local.y),angle=std::atan2(local.y,local.x);
            if(std::abs(radius-radius_)>.10f||std::abs(local.z)>.14f){command_.hands[h]=closed_[h]=armed_[h]=false;continue;}
            if(command_.hands[h]){
                const float delta=std::remainder(angle-last_angle_[h],2*pi);
                if(std::abs(delta)>.08f+8.f*static_cast<float>(dt)*1e-9f){command_.hands[h]=closed_[h]=armed_[h]=false;continue;}
                sum-=delta;++contributors;
            }
            last_angle_[h]=angle;command_.hands[h]=closed_[h]=true;
        }
        command_.active=command_.hands[0]||command_.hands[1];
        if(!command_.active){angle_=0;command_.axis=0;return;}
        if(contributors)angle_=std::clamp(angle_+sum/static_cast<float>(contributors),-pi/2,pi/2);
        const float target=std::copysign(std::clamp((std::abs(angle_)-.025f)/(pi/2-.025f),0.f,1.f),angle_);
        const float blend=1.f-std::exp(-static_cast<float>(dt)*1e-9f/.04f);
        command_.axis+=blend*(target-command_.axis);
    }catch(...){clear(true);}
}
}
