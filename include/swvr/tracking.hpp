#pragma once
#include <array>
#include <cstdint>

namespace swvr {
// OpenXR convention: metres, right-handed, +Y up, -Z forward.
struct Vec3 { float x{}, y{}, z{}; };
struct Quaternion { float x{}, y{}, z{}, w{1}; };
struct Pose { Quaternion orientation{}; Vec3 position{}; };
using Matrix4 = std::array<float,16>; // Column-major OpenGL layout.
struct Fov { float left{}, right{}, up{}, down{}; }; // Angles in radians from xrLocateViews.
Matrix4 projection(Fov fov, float near_z, float far_z);
Matrix4 view_matrix(const Pose& eye);
Pose compose(const Pose& parent, const Pose& child);
Pose inverse(const Pose& pose);
// Midpoint of the two eyes, with the shortest-arc mean orientation. Suitable
// for an initial LOCAL calibration even when view orientations are canted.
Pose stereo_center(const std::array<Pose,2>& eyes);
// Seated calibration: keep the stereo midpoint and yaw heading, while retaining
// LOCAL gravity instead of baking the initial pitch/roll into the world frame.
Pose gravity_aligned_origin(const std::array<Pose,2>& eyes);
// Recenter translation after an accepted body calibration; keep world heading.
Pose recenter_head_origin(const Pose& origin,const std::array<Pose,2>& eyes);
// Both eyes come from xrLocateViews at the SAME predicted display time.
// origin is the calibrated pose in LOCAL space; seat is in game/world space.
std::array<Pose, 2> cockpit_views(const Pose& seat, const Pose& origin,
                                const std::array<Pose, 2>& located_eyes);

// Seated head limit. Leaning is measured at an estimated neck pivot so that
// turning or pitching the head never counts as leaving the body. Offsets are in
// the calibrated origin frame (+X right, +Y up, -Z seat forward), in metres.
// Inside knee*limit nothing changes; beyond, the offset is compressed smoothly
// (tanh) toward the ellipsoid surface and never exceeds it.
inline constexpr Vec3 head_neck_offset{0,-.075f,.08f}; // Neck pivot in the head frame.
struct HeadLimit {
    float knee{.6f};
    float forward{.30f},back{.12f},left{.22f},right{.22f},up{.10f},down{.30f};
};
struct HeadLimitResult {
    Pose origin;       // Effective origin: use it for views, hands and avatar targets.
    Vec3 requested{};  // Neck offset from calibration, before limiting.
    Vec3 applied{};    // Neck offset after limiting.
    float extent{};    // Normalized ellipsoid radius of requested (1 = on the limit).
    bool limited{};
};
Vec3 neck_position(const Pose& origin,const std::array<Pose,2>& eyes);
HeadLimitResult limit_head(const Pose& origin,Vec3 calibrated_neck,
                           const std::array<Pose,2>& eyes,const HeadLimit& limit);

enum class PinchEvent { none, began, held, released, cancelled };
struct HandSample {
    bool active{};
    bool position_valid{};
    bool position_tracked{};
    bool focused{};
    std::int64_t time_ns{}; // Monotonic capture/sample clock, not wall time.
    Vec3 thumb_tip{};
    Vec3 index_tip{};
};

// Require an open hand before arming, including after tracking/focus loss.
// Cancel is NOT release: callers must not commit a click on cancellation.
class PinchDetector {
public:
    PinchDetector(float press_metres = 0.025f, float release_metres = 0.040f,
                  std::int64_t max_age_ns = 100'000'000);
    PinchEvent update(const HandSample& sample, std::int64_t now_ns);
    PinchEvent reset();
private:
    float press_, release_;
    std::int64_t max_age_, last_sample_{-1};
    bool armed_{}, pinched_{};
};
}
