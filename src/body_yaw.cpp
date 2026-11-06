#include "swvr/body_yaw.hpp"
#include <array>
#include <cmath>

namespace swvr {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr double match_tolerance=.0175; // ~1 degree
}
double game_camera_yaw(const Matrix4d& camera) noexcept {return std::atan2(camera[8],camera[10]);}
double wrap_pi(double radians) noexcept {
    if(!std::isfinite(radians))return 0;
    radians=std::fmod(radians+pi,2*pi);
    if(radians<0)radians+=2*pi;
    return radians-pi;
}
void LookYawCalibration::observe(float field_radians,const Matrix4d& look) noexcept {
    if(locked_)return;
    const double field=field_radians;
    // Near multiples of pi/2 two of the four mappings coincide; wait for an unambiguous angle.
    if(!std::isfinite(field)||std::abs(std::sin(field))<.2||std::abs(std::cos(field))<.2)return;
    const double horizontal=std::hypot(look[8],look[10]);
    if(!std::isfinite(horizontal)||horizontal<.2){agree_=0;return;}
    const double measured=game_camera_yaw(look);
    int matches{},sign{};double offset{};
    for(int s:{1,-1})for(double o:{0.,pi})
        if(std::abs(wrap_pi(measured-(s*field+o)))<match_tolerance){++matches;sign=s;offset=o;}
    if(matches!=1){agree_=0;return;}
    if(agree_&&sign==candidate_sign_&&offset==candidate_offset_)++agree_;
    else {candidate_sign_=sign;candidate_offset_=offset;agree_=1;}
    if(agree_>=needed_){locked_=true;sign_=candidate_sign_;offset_=candidate_offset_;}
}
Matrix4d rotate_game_yaw(const Matrix4d& camera,double radians) noexcept {
    const double c=std::cos(radians),s=std::sin(radians);auto out=camera;
    for(int column=0;column<3;++column) {
        const double x=camera[column*4],z=camera[column*4+2];
        out[column*4]=c*x+s*z;out[column*4+2]=-s*x+c*z;
    }
    return out;
}
float follow_headset_yaw(float field_radians,const LookYawCalibration& calibration,
                         const Matrix4d& headset_camera,const Matrix4d& game_camera,double dead_zone) noexcept {
    if(!calibration.locked()||!std::isfinite(field_radians))return field_radians;
    const double delta=wrap_pi(game_camera_yaw(headset_camera)-game_camera_yaw(game_camera));
    if(!std::isfinite(delta)||std::abs(delta)<dead_zone)return field_radians;
    return static_cast<float>(wrap_pi(field_radians+calibration.sign()*delta));
}
HeadsetYawStep follow_headset_yaw_turning(float field_radians,std::optional<float> settled,
                         const LookYawCalibration& calibration,const Matrix4d& headset_camera,
                         const Matrix4d& game_camera,double dead_zone,double joystick_turn) noexcept {
    if(!calibration.locked()||!std::isfinite(field_radians))return {field_radians,0};
    if(!std::isfinite(joystick_turn)||std::abs(joystick_turn)>.79)joystick_turn=0; // Smooth <=120 deg/s or one snap <=45 degrees.
    if(!settled||!std::isfinite(*settled)){
        // Headset dead zone must not swallow a deliberate joystick delta.
        const auto followed=follow_headset_yaw(field_radians,calibration,headset_camera,game_camera,dead_zone);
        return {joystick_turn==0?followed:static_cast<float>(wrap_pi(followed+calibration.sign()*joystick_turn)),joystick_turn};
    }
    if(joystick_turn!=0)field_radians=static_cast<float>(wrap_pi(field_radians+calibration.sign()*joystick_turn));
    // game_camera still shows the settled yaw; the headset turns with the play space.
    const double turn=calibration.sign()*wrap_pi(static_cast<double>(field_radians)-*settled);
    const auto follow=follow_headset_yaw(*settled,calibration,rotate_game_yaw(headset_camera,turn),game_camera,dead_zone);
    // Inside the dead zone keep the field exactly as the game left it.
    if(follow==*settled)return {field_radians,turn};
    return {follow,turn};
}
}
