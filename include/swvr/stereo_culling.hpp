#pragma once
#include "swvr/camera_bridge.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace swvr {
// Layout consumed by the measured native AABB/plane test (186AB0).
// The interior satisfies dot(normal, point) >= distance.
struct NativeCullPlane {
    double distance{};
    std::array<float,3> normal{};
    std::uint32_t padding{};
};
static_assert(std::is_standard_layout_v<NativeCullPlane>);
static_assert(sizeof(NativeCullPlane)==24);
static_assert(offsetof(NativeCullPlane,normal)==8);
static_assert(offsetof(NativeCullPlane,padding)==20);
using StereoCullPlanes=std::array<NativeCullPlane,6>;

// Build six conservative half-spaces covering both finite eye frusta. The base
// is the native proper, rigid, column-major camera transform (double world or
// floating-origin coordinates). Eyes are relative OpenXR poses, in metres.
// World points use base * S * eye * corner, S=diag(1,1,-1,1).
// Order: near, far, left, right, bottom, top. There is no shared-apex assumption.
// Does not modify any camera, scene, or native memory.
StereoCullPlanes make_stereo_cull_planes(const Matrix4d& base,
    const std::array<Pose,2>& eyes,const std::array<Fov,2>& fovs,
    double near_distance,double far_distance);
}
