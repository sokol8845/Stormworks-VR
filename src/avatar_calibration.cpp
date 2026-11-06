#include "swvr/avatar_calibration.hpp"
#include <algorithm>
#include <string_view>
#include <cmath>
#include <vector>

namespace swvr {
bool AvatarCalibrationWindow::observe(std::optional<AvatarBodyCalibration> body,Vec3 head,Clock::time_point now){
    if(last_seen_&&now<=*last_seen_)return false;
    last_seen_=now;
    if(!samples_.empty()&&now-samples_.back().time>std::chrono::milliseconds(300))samples_.clear();
    if(!body||!std::isfinite(head.x)||!std::isfinite(head.y)||!std::isfinite(head.z))return false;
    if(!body->actor||!std::isfinite(body->wrist_span)||body->wrist_span<.8||body->wrist_span>2.4)return false;
    for(const auto value:body->shoulder_offset)if(!std::isfinite(value))return false;
    if(!samples_.empty()){const auto& old=samples_.back().body;
        if(old.actor!=body->actor||old.space_revision!=body->space_revision||old.variant!=body->variant)samples_.clear();}
    if(samples_.size()>=5){double span{};std::array<double,3> offset{};Vec3 centre{};
        for(const auto& s:samples_){span+=s.body.wrist_span;for(unsigned i=0;i<3;++i)offset[i]+=s.body.shoulder_offset[i];centre.x+=s.head.x;centre.y+=s.head.y;centre.z+=s.head.z;}
        const double n=static_cast<double>(samples_.size());
        if(std::abs(body->wrist_span-span/n)>.12)return false;
        for(unsigned i=0;i<3;++i)if(std::abs(body->shoulder_offset[i]-offset[i]/n)>.07)return false;
        if(std::hypot(head.x-centre.x/n,head.y-centre.y/n,head.z-centre.z/n)>.10)return false;
    }
    samples_.push_back({*body,head,now});
    while(samples_.size()>256||now-samples_.front().time>std::chrono::milliseconds(1800))samples_.pop_front();
    return true;
}
std::optional<StableAvatarCalibration> AvatarCalibrationWindow::result(std::uint64_t generation)const{
    if(!generation||samples_.size()<10||samples_.back().time-samples_.front().time<std::chrono::milliseconds(1200))return {};
    StableAvatarCalibration out{samples_.back().body,{},samples_.size()};out.body.generation=generation;
    const auto average=[&](auto field,double maximum_spread)->std::optional<double>{std::vector<double> values;values.reserve(samples_.size());
        for(const auto& s:samples_)values.push_back(field(s));std::sort(values.begin(),values.end());const auto trim=values.size()/5;
        if(values[values.size()-trim-1]-values[trim]>maximum_spread)return {};
        double sum{};for(auto i=trim;i<values.size()-trim;++i)sum+=values[i];return sum/static_cast<double>(values.size()-2*trim);};
    const auto span=average([](const Sample& s){return s.body.wrist_span;},.08);if(!span)return {};out.body.wrist_span=*span;
    for(unsigned i=0;i<3;++i){const auto value=average([&](const Sample& s){return s.body.shoulder_offset[i];},.05);if(!value)return {};out.body.shoulder_offset[i]=*value;}
    const auto x=average([](const Sample& s){return s.head.x;},.05),y=average([](const Sample& s){return s.head.y;},.05),z=average([](const Sample& s){return s.head.z;},.05);
    if(!x||!y||!z)return {};out.head_position={static_cast<float>(*x),static_cast<float>(*y),static_cast<float>(*z)};return out;
}
void AvatarCalibrationHold::reset() noexcept {
    context_.reset();since_.reset();last_valid_.reset();last_observation_.reset();latched_=false;
}
AvatarCalibrationHoldResult AvatarCalibrationHold::update(bool valid,AvatarCalibrationContext context,Clock::time_point now) {
    if(last_observation_&&now<*last_observation_)return {};
    if(context_!=context || (last_valid_&&now-*last_valid_>std::chrono::milliseconds(300)))reset();
    context_=context;last_observation_=now;
    if(valid&&context.actor) {
        if(!since_){since_=now;++holds_;}
        last_valid_=now;
        longest_=std::max(longest_,std::chrono::duration_cast<std::chrono::milliseconds>(now-*since_));
    }
    const auto progress=since_&&last_valid_?std::clamp(std::chrono::duration<float>(*last_valid_-*since_).count()/1.5f,0.f,1.f):0.f;
    return {since_.has_value()&&!latched_,valid&&context.actor&&since_&&!latched_&&now-*since_>=std::chrono::milliseconds(1500),progress};
}
const char* avatar_calibration_hint(const AvatarTargets& targets,const char* issue) noexcept {
    if(!issue)return "TRZYMAJ T-POSE";
    const std::string_view code=issue;
    if(code=="tracking_not_ready")return "KALIBRACJA CZEKA NA SLEDZENIE";
    if(code=="hand_not_tracked")return !targets.hands[0]?"POKAZ LEWA DLON":"POKAZ PRAWA DLON";
    if(code=="arms_not_spread")return "ROZLOZ RECE SZERZEJ";
    if(code=="hands_too_far_apart")return "ZBLIZ DLONIE";
    if(code=="hands_uneven_height")return "WYROWNAJ WYSOKOSC DLONI";
    if(code=="hands_uneven_forward")return "DLONIE NA JEDNEJ GLEBOKOSCI";
    if(code=="arms_not_sideways")return "ROZLOZ RECE NA BOKI";
    if(code=="hands_too_high")return "OPUSC DLONIE";
    if(code=="hands_too_low")return "UNIES DLONIE";
    if(code=="hands_off_centre")return "WYROWNAJ RECE WZGLEDEM GLOWY";
    if(code=="hands_too_far_forward")return "COFNIJ RECE BLIZEJ CIALA";
    return "KALIBRACJA - BRAK POPRAWNEJ POZY";
}
}
