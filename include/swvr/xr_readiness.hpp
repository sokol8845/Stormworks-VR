#pragma once
#include "swvr/xr_runtime.hpp"
#include <chrono>
#include <optional>

namespace swvr {
enum class XrReadiness { ready, session, focus, tracking, render };
const char* xr_readiness_name(XrReadiness) noexcept;
struct XrReadinessNotice {
    XrReadiness reason{};
    std::chrono::milliseconds waited{};
};
// Diagnostic only. Does not change frame submission, session state or focus.
// Warn after five seconds of a continuous cause, then at most every 30 s.
// A ready notice is emitted once when a reported interruption has recovered.
class XrReadinessMonitor {
public:
    using Clock=std::chrono::steady_clock;
    std::optional<XrReadinessNotice> observe(const XrRuntimeFrame&,Clock::time_point now);
private:
    std::optional<XrReadiness> reason_;
    Clock::time_point since_{},last_notice_{},last_observation_{};
    bool warned_{},reported_interruption_{};
};
}
