#pragma once
#include <cstdint>
#include <memory>
#include <optional>
namespace swvr {
struct GpuTiming {std::uint64_t frame{};double milliseconds{},budget_ms{};};
// Timestamp pairs never bind a query target or wait for a result. Unavailable
// pairs stay in a bounded ring; a full ring skips instrumentation, not rendering.
class GlGpuTimer {
public:
    GlGpuTimer();~GlGpuTimer();
    GlGpuTimer(const GlGpuTimer&)=delete;GlGpuTimer& operator=(const GlGpuTimer&)=delete;
    void begin(std::uint64_t frame,double budget_ms);
    void end();
    void cancel()noexcept;
    std::optional<GpuTiming> poll();
    bool supported()const noexcept;
    std::uint64_t skipped()const noexcept;
    unsigned pending()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
