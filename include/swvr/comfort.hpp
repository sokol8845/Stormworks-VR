#pragma once
#include "swvr/gesture_settings.hpp"
#include "swvr/xr_runtime.hpp"
#include <algorithm>
#include <cmath>
namespace swvr {
struct ComfortVisual {float fade{},vignette{},radius_degrees{40};};
struct HeadComfortView {Pose origin;float fade{},extent{};bool limited{};};
inline HeadComfortView head_comfort_view(const Pose& origin,Vec3 neck,const std::array<Pose,2>& eyes,const HeadLimit& limits,bool enabled,bool fade_mode){
    if(!enabled)return {origin};const auto limit=limit_head(origin,neck,eyes,limits);
    if(!fade_mode)return {limit.origin,0,limit.extent,limit.limited};
    const float t=std::clamp((limit.extent-limits.knee)/(1-limits.knee),0.f,1.f);
    return {origin,t*t*(3-2*t),limit.extent,limit.limited}; // Physical pose remains 1:1.
}
class ComfortMotion {
public:
    void reset()noexcept{time_=0;revision_=0;value_=0;snap_until_=0;}
    ComfortVisual update(const XrRuntimeFrame& f,const ComfortSettings& settings,bool moving,double turn_delta,float head_fade)noexcept{
        if(!f.focused||!f.running||!f.should_render||!f.position_tracked||!f.orientation_tracked||f.terminal){reset();return {};}
        if(f.space_revision!=revision_||f.predicted_display_time_ns<time_||f.predicted_display_time_ns-time_>300'000'000){reset();revision_=f.space_revision;}
        const float dt=time_?std::clamp(static_cast<float>(f.predicted_display_time_ns-time_)/1e9f,0.f,.1f):0.f;
        time_=f.predicted_display_time_ns;if(std::abs(turn_delta)>.2)snap_until_=time_+150'000'000;
        const float target=(moving||std::abs(turn_delta)>1e-7||time_<snap_until_)?settings.vignette_strength:0;
        value_+=std::clamp(target-value_,-dt*4,dt*6);
        if(settings.vignette_strength==0)value_=0;
        return {std::clamp(head_fade,0.f,1.f),value_,settings.vignette_radius_degrees};
    }
private:
    std::int64_t time_{},snap_until_{};std::uint64_t revision_{};float value_{};
};
}
