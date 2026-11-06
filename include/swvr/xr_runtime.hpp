#pragma once
#include "swvr/tracking.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace swvr {
inline constexpr std::size_t xr_hand_joint_count=26;
inline constexpr std::size_t xr_thumb_tip=5, xr_index_proximal=7, xr_index_tip=10;
struct XrTrackedJoint {
    Pose pose{};
    float radius{};
    bool position_valid{}, orientation_valid{}, position_tracked{}, orientation_tracked{};
};
struct XrTrackedHand {
    bool active{};
    std::array<XrTrackedJoint,xr_hand_joint_count> joints{};
};
struct XrEyeFrame {
    Pose pose{}; // In the runtime's LOCAL space, at predicted_display_time_ns.
    Fov fov{};
    unsigned texture{}; // Borrowed GL_TEXTURE_2D, valid only until end/cancel_frame.
    int width{}, height{};
};
struct XrRuntimeFrame {
    std::uint64_t id{}; // Nonzero means xrBeginFrame succeeded: end/cancel it exactly once.
    std::int64_t predicted_display_time_ns{}; // OpenXR time domain, not wall/steady_clock.
    bool running{}, focused{}, terminal{}, should_render{};
    bool position_valid{}, orientation_valid{}, position_tracked{}, orientation_tracked{};
    // Changes when a pending LOCAL reference-space change reaches display time.
    // The caller must invalidate/re-establish its game-to-LOCAL calibration.
    std::uint64_t space_revision{};
    std::array<XrEyeFrame,2> eyes{};
    std::array<XrTrackedHand,2> hands{};
    std::int64_t predicted_display_period_ns{}; // Runtime scheduling period, not necessarily physical refresh.
    bool system_input_suppressed{}; // Diagnostic reason for the runtime/palm guard.
};
// Poses/FOV (LOCAL space) the submitted images were actually rendered with.
// Used to resubmit the previous images instead of an empty (black) frame; the
// compositor then reprojects them to the current head pose.
struct XrViewOverride {
    std::array<Pose,2> poses{};
    std::array<Fov,2> fov{};
};
struct XrRuntimeInfo {
    std::string runtime_name, system_name;
    bool hand_tracking_extension{}, hand_tracking_supported{}, srgb{},hand_system_gesture_extension{};
    std::int64_t color_format{};
};
struct XrRuntimeOptions {
    std::filesystem::path loader_path; // Absolute loader DLL path; no global runtime changes.
    void* native_dc{}; // HDC currently bound on this thread.
    void* native_gl_context{}; // HGLRC currently bound on this thread; never owned by this class.
};
class XrRuntime {
public:
    XrRuntime();
    ~XrRuntime();
    XrRuntime(const XrRuntime&)=delete;
    XrRuntime& operator=(const XrRuntime&)=delete;
    void initialize(const XrRuntimeOptions& options);
    const XrRuntimeInfo& info() const;
    // All calls, including shutdown/destruction, belong on the initialize thread
    // with its supplied GL context current. This class creates/binds no FBOs.
    XrRuntimeFrame begin_frame();
    // Only submit projection when BOTH acquired eye images were fully rendered.
    // {false,false} safely discards a begun frame (also when should_render=false).
    void end_frame(std::uint64_t frame_id, std::array<bool,2> rendered);
    void end_frame(std::uint64_t frame_id, std::array<bool,2> rendered, const XrViewOverride& views);
    void cancel_frame() noexcept;
    void request_exit();
    void shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
