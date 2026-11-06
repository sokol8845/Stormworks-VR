#pragma once
#include "swvr/camera_bridge.hpp"
#include "swvr/xr_runtime.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace swvr {
inline constexpr std::size_t avatar_bone_count=19;
enum class AvatarVariant { female, male };
// Identity tokens only. depth_only marks a producer-owned shadow copy
// (0x6734e0): it may authorize single-palette depth draws, never colour draws.
// proximity marks a 19-bone colour record WITHOUT the local head selector (a
// second, unselected copy of the seated local body). It is posed only when its
// native head lies within avatar_proximity_head_m of the eyes.
struct AvatarPaletteBinding {std::uintptr_t current{},previous{};bool depth_only{},proximity{};};
inline constexpr double avatar_proximity_head_m=.35;
struct AvatarRig {
    std::array<Matrix4d,avatar_bone_count> bind{},inverse_bind{};
    std::array<int,avatar_bone_count> parent{};
};
// Bounded reader for the named survivor hierarchy. No native pointers/callbacks.
AvatarRig read_avatar_rig(std::span<const unsigned char> anim);
struct AvatarBodyCalibration {
    std::uint64_t actor{},space_revision{},generation{};
    AvatarVariant variant{AvatarVariant::male};
    double wrist_span{};
    std::array<double,3> shoulder_offset{}; // Reference axes, relative to tracked eyes.
    // Body proportions do not depend on LOCAL space: a recenter keeps them;
    // only another actor or model variant invalidates the calibration.
};
struct AvatarTargets {
    std::uint64_t frame{},space_revision{},actor{};
    Matrix4d reference{},head{};
    std::array<std::optional<Matrix4d>,2> hands; // Left/right wrist, game-world axes.
    std::array<float,2> curl{};
    bool valid{};
    std::optional<AvatarBodyCalibration> body;
};
// Accept only a tracked, horizontal T-pose requested explicitly by the user.
// First failing T-pose condition (static string), or nullptr when acceptable.
// min_span: 0.8 m for an explicit request; automatic detection uses the stricter
// avatar_auto_t_pose_min_span so half-spread arms never trigger it.
inline constexpr double avatar_auto_t_pose_min_span=1.2;
const char* avatar_body_calibration_issue(const AvatarTargets&,double min_span=.8) noexcept;
std::optional<AvatarBodyCalibration> calibrate_avatar_body(const AvatarTargets&,std::uint64_t generation,AvatarVariant variant=AvatarVariant::male) noexcept;
// Same origin, predicted time and double-precision anchor as the eye cameras.
AvatarTargets make_avatar_targets(const XrRuntimeFrame&,const Pose& origin,const Matrix4d& anchor);
AvatarTargets rebase_avatar_targets(AvatarTargets,std::array<double,3> shift);
enum class AvatarPoseStatus { applied, inactive, invalid_palette, invalid_transform, head_anchor_mismatch };
struct AvatarPoseResult {
    AvatarPoseStatus status{AvatarPoseStatus::inactive};
    std::array<Matrix4,avatar_bone_count> current{},previous{};
    std::array<bool,2> hands{},reach_limited{};
    std::array<double,2> wrist_shift{}; // Posed minus native wrist position, world units (0 if untracked).
    double head_distance{},body_scale{1};
    bool body_calibrated{};
};
// Keeps the eye-to-head mount for one actor/reference-space/rig generation.
// Replaces only render palettes; animation/gameplay memory is never modified.
class AvatarRetargeter {
public:
    AvatarPoseResult pose(const AvatarRig&,AvatarVariant,const AvatarTargets&,
        const Matrix4& model_to_render,std::span<const Matrix4> current,
        std::span<const Matrix4> previous) noexcept;
    void reset() noexcept;
private:
    bool calibrated_{};
    std::uint64_t actor_{},revision_{};
    AvatarVariant variant_{};
    Matrix4d mount_{};
    std::array<double,3> eye_offset_{};
    double scale_{};
};
const char* avatar_pose_status_name(AvatarPoseStatus) noexcept;
}
