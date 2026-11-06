#pragma once
#include "swvr/tracking.hpp"

namespace swvr {
using Matrix4d = std::array<double,16>;
// Offset the game's double-precision camera transform before its own update.
// Coordinates remain in the input's world/floating-origin space.
Matrix4d offset_game_camera(const Matrix4d& transform, const Pose& eye_from_camera);
struct EyeCamera {
    Matrix4 view{}, projection{}, view_projection{}, inverse_view_projection{};
    Vec3 render_position{}; // Same floating-origin coordinates as the input game view.
};
// eye_from_camera is xrLocateViews' eye pose relative to the calibrated LOCAL
// origin (including IPD). Preserve the game's reflected coordinate basis and
// depth coefficients; do not turn its view matrix into a quaternion.
// This computes a candidate camera only. The caller must also update culling,
// double matrices, world offsets and per-eye render targets in the game.
EyeCamera make_eye_camera(const Matrix4& game_view, const Matrix4& game_projection,
                          const Pose& eye_from_camera, Fov eye_fov);
}
