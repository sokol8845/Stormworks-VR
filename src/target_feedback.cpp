#include "swvr/target_feedback.hpp"
#include <cmath>
#include <string_view>
namespace swvr {
TargetFeedback current_target_feedback(TargetFeedback value,const HandInteractionSnapshot& current,HandInteractionClock::time_point now)noexcept{
    const auto& old=value.sample;const auto fresh=[&](auto time){return now>=time&&now-time<=std::chrono::milliseconds(100);};
    if(!current.valid_ray||!old.valid_ray||!old.sequence||old.sequence>current.sequence||old.hand!=current.hand||old.space_revision!=current.space_revision||
        old.cancellation_generation!=current.cancellation_generation||!fresh(value.observed)||!fresh(old.published_at)||!fresh(current.published_at))return {};
    double distance{},dot{},length{},old_length{};
    for(unsigned i=0;i<3;++i){const auto d=current.ray.origin[i]-old.ray.origin[i];distance+=d*d;dot+=current.ray.direction[i]*old.ray.direction[i];
        length+=current.ray.direction[i]*current.ray.direction[i];old_length+=old.ray.direction[i]*old.ray.direction[i];}
    // The angle test below is only meaningful for unit directions (the bridge's contract).
    if(!std::isfinite(distance)||!std::isfinite(dot)||!(std::abs(length-1)<=.02)||!(std::abs(old_length-1)<=.02)||distance>.0001||dot<.999390827)return {};
    return value;
}
const char* target_feedback_hint(const TargetFeedback& feedback)noexcept{
    if(feedback.state==TargetFeedbackState::ready)return "CEL GRY - PRZYCISK GOTOWY";
    if(feedback.state==TargetFeedbackState::held)return "PRZYCISK TRZYMANY - OTWORZ DLON";
    if(feedback.state==TargetFeedbackState::unknown)return "BRAK POTWIERDZONEGO CELU";
    const std::string_view why=feedback.reason?feedback.reason:"";
    if(why=="target_mismatch")return "BRAK PRZYCISKU POD PROMIENIEM";
    if(why=="unsupported_button_type")return "TEN TYP ELEMENTU NIE JEST OBSLUGIWANY";
    if(why=="ui_capture"||why=="pause_ui")return "INTERFEJS GRY BLOKUJE KLIK";
    if(why=="reopen_hand")return "OTWORZ DLON PRZED KOLEJNYM KLIKIEM";
    if(why=="native_hold"||why=="native_hand_down")return "GRA MA JUZ WCISNIETY PRZYCISK";
    if(why=="pending")return "CZEKAM NA OBSLUGE KLIKNIECIA";
    return "KLIK ZABLOKOWANY - DIAGNOSTYKA";
}
}
