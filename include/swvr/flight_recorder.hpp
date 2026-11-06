#pragma once
#include "swvr/hand_recording.hpp"
#include <memory>
#include <vector>
namespace swvr {
struct FlightRecorderOptions {unsigned segments{6},incident_slots{3};std::uint64_t segment_bytes{8*1024*1024},segment_ms{30'000};};
struct FlightRecorderStats {std::uint64_t samples{},dropped{},rotations{},markers{},saved{};bool failed{};};
// Bounded asynchronous rolling observations. Each segment starts with settings;
// samples contain poses and observed controls, not a checkpoint of gesture FSMs.
// Exact deterministic replay remains in the separate hands/controls journals.
class FlightRecorder {
public:
    FlightRecorder();~FlightRecorder();
    FlightRecorder(const FlightRecorder&)=delete;FlightRecorder& operator=(const FlightRecorder&)=delete;
    void open(const std::filesystem::path& new_directory,FlightRecorderOptions={});
    void sample(const XrRuntimeFrame&,const GestureSettings&,std::uint64_t elapsed_ms,std::vector<std::string> diagnostics)noexcept;
    bool mark(std::uint64_t elapsed_ms,std::uint64_t frame)noexcept;
    FlightRecorderStats stats()const noexcept;
    void close()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
