#pragma once
#include "swvr/camera_bridge.hpp"
#include <cstdint>
#include <optional>

namespace swvr {
// Body yaw follows the headset outside seats. The native look yaw (camera
// owner C+0x2A0, radians wrapped to [-pi, pi]) turns the character and the
// walking direction; the XR view does not depend on it because the head anchor
// removes the look rotation. Both yaws below are measured with the same
// formula on game camera transforms: atan2(m[8], m[10]).
double game_camera_yaw(const Matrix4d& camera) noexcept;
double wrap_pi(double radians) noexcept;

// Identifies how the native yaw field maps to the look matrix yaw:
// look_yaw == sign * field + offset (offset 0 or pi). Locks after `needed`
// consecutive agreeing frames with a well-conditioned angle; never guesses.
class LookYawCalibration {
public:
    explicit LookYawCalibration(unsigned needed=10) noexcept : needed_(needed) {}
    void observe(float field_radians,const Matrix4d& look) noexcept;
    bool locked() const noexcept {return locked_;}
    int sign() const noexcept {return sign_;}
    double offset() const noexcept {return offset_;}
    void reset() noexcept {*this=LookYawCalibration(needed_);}
private:
    unsigned needed_{},agree_{};
    int sign_{},candidate_sign_{};
    double offset_{},candidate_offset_{};
    bool locked_{};
};

// Rotates a game camera transform about the game's vertical axis so that
// game_camera_yaw increases by `radians`; position and pitch are preserved.
Matrix4d rotate_game_yaw(const Matrix4d& camera,double radians) noexcept;

// New native yaw field value so that the game camera faces the headset yaw.
// Returns the current field unchanged inside the dead zone.
float follow_headset_yaw(float field_radians,const LookYawCalibration&,
                         const Matrix4d& headset_camera,const Matrix4d& game_camera,
                         double dead_zone_radians=0.0175) noexcept;

// The game applies mouse/key turning to the yaw field between camera updates.
// `settled` is the field value left by the previous native update (nullopt
// when unknown). Any change since then is a deliberate turn: it is returned
// as `turn` (game-yaw radians, to rotate the play space by) and kept in
// `field`, so the headset follow does not pull the body back. An optional
// joystick delta joins that same path, rotating body and XR space together.
struct HeadsetYawStep {float field{};double turn{};};
HeadsetYawStep follow_headset_yaw_turning(float field_radians,std::optional<float> settled,
                         const LookYawCalibration&,const Matrix4d& headset_camera,
                         const Matrix4d& game_camera,double dead_zone_radians=0.0175,
                         double joystick_turn_radians=0) noexcept;
}
