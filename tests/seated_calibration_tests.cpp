#include "swvr/tracking.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        constexpr float radians=0.017453292519943295f;
        const float roll=7*radians;
        const swvr::Pose tilted{{0,0,std::sin(roll/2),std::cos(roll/2)},{.4f,1.6f,-.2f}};
        const swvr::Pose level{{},{.4f,1.6f,-.2f}};
        const std::array<swvr::Pose,2> offsets{swvr::Pose{{},{-.032f,0,0}},swvr::Pose{{},{.032f,0,0}}};
        const std::array<swvr::Pose,2> initial{swvr::compose(tilted,offsets[0]),swvr::compose(tilted,offsets[1])};
        const std::array<swvr::Pose,2> now{swvr::compose(level,offsets[0]),swvr::compose(level,offsets[1])};
        const auto origin=swvr::gravity_aligned_origin(initial);
        const auto relative=swvr::cockpit_views({},origin,now);
        const auto head=swvr::stereo_center(relative);
        const auto transform=swvr::view_matrix(swvr::inverse(head));
        const auto retained_roll=std::atan2(transform[1],transform[5])/radians;
        if(std::abs(retained_roll)>.001f) {
            std::cerr<<"FAIL: a level head retains "<<retained_roll<<" degrees of initial calibration roll\n";
            return 1;
        }
        if(std::hypot(head.position.x,head.position.y,head.position.z)>1e-5f)
            throw std::runtime_error("Calibration moved the center of the head");
        const swvr::Pose yaw{{0,std::sin(35*radians/2),0,std::cos(35*radians/2)},level.position};
        const swvr::Pose pitch{{std::sin(-20*radians/2),0,0,std::cos(-20*radians/2)},{}};
        const auto pitched=swvr::compose(yaw,pitch);
        const std::array<swvr::Pose,2> pitched_eyes{swvr::compose(pitched,offsets[0]),swvr::compose(pitched,offsets[1])};
        const auto yaw_origin=swvr::gravity_aligned_origin(pitched_eyes);
        const auto retained=swvr::stereo_center(swvr::cockpit_views({},yaw_origin,pitched_eyes));
        const auto retained_matrix=swvr::view_matrix(swvr::inverse(retained));
        if(std::abs(std::atan2(-retained_matrix[9],retained_matrix[10])/radians+20)>.001f)
            throw std::runtime_error("Calibration erased actual head pitch");
        const std::array<swvr::Pose,2> straight{swvr::compose(yaw,offsets[0]),swvr::compose(yaw,offsets[1])};
        const auto leveled=swvr::stereo_center(swvr::cockpit_views({},yaw_origin,straight));
        const auto leveled_matrix=swvr::view_matrix(swvr::inverse(leveled));
        for(int i=0;i<12;++i) {
            const float expected=(i==0 || i==5 || i==10)?1.f:0.f;
            if(std::abs(leveled_matrix[i]-expected)>1e-5f)
                throw std::runtime_error("Neutral head did not return to calibrated yaw without pitch/roll");
        }
        auto moved=straight;
        for(auto& eye:moved) eye.position.y+=.15f;
        const auto moved_head=swvr::stereo_center(swvr::cockpit_views({},yaw_origin,moved));
        if(std::abs(moved_head.position.y-.15f)>1e-5f)
            throw std::runtime_error("Vertical leaning is no longer physical LOCAL up");
        std::cout<<"PASS: a neutral head stays level after initially tilted calibration\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
