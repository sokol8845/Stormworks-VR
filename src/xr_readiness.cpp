#include "swvr/xr_readiness.hpp"

namespace swvr {
const char* xr_readiness_name(XrReadiness reason) noexcept {
    switch(reason) {
    case XrReadiness::ready:return "ready";
    case XrReadiness::session:return "session";
    case XrReadiness::focus:return "focus";
    case XrReadiness::tracking:return "tracking";
    case XrReadiness::render:return "render";
    }
    return "unknown";
}
std::optional<XrReadinessNotice> XrReadinessMonitor::observe(const XrRuntimeFrame& frame,Clock::time_point now) {
    // A caller's out-of-order timestamp must not move either deadline back.
    if(reason_ && now<last_observation_)return {};
    last_observation_=now;
    const auto reason=!frame.running?XrReadiness::session:
        !frame.focused?XrReadiness::focus:
        !(frame.position_valid&&frame.orientation_valid&&frame.position_tracked&&frame.orientation_tracked)?XrReadiness::tracking:
        !frame.id||!frame.should_render?XrReadiness::render:XrReadiness::ready;
    if(reason==XrReadiness::ready) {
        const auto waited=std::chrono::duration_cast<std::chrono::milliseconds>(now-since_);
        reason_=reason;warned_=false;
        if(reported_interruption_) {
            reported_interruption_=false;return XrReadinessNotice{reason,waited};
        }
        return {};
    }
    if(reason_!=reason) {reason_=reason;since_=now;warned_=false;}
    const auto waited=std::chrono::duration_cast<std::chrono::milliseconds>(now-since_);
    if(waited<std::chrono::seconds(5) || (warned_ && now-last_notice_<std::chrono::seconds(30)))return {};
    warned_=reported_interruption_=true;last_notice_=now;
    return XrReadinessNotice{reason,waited};
}
}
