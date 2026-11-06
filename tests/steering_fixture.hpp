#pragma once
#include "swvr/hand_steering.hpp"
#include <cmath>
namespace swvr::steering_fixture {
inline XrRuntimeFrame hands(float angle,bool left=true,bool right=true){
    XrRuntimeFrame f;f.id=1;f.space_revision=1;f.predicted_display_time_ns=1'000'000'000;
    f.running=f.focused=f.should_render=f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
    f.eyes[0].pose.position={-.032f,1.65f,0};f.eyes[1].pose.position={.032f,1.65f,0};
    for(unsigned h=0;h<2;++h){auto& hand=f.hands[h];hand.active=true;for(auto& j:hand.joints)j.position_valid=j.position_tracked=true;
        const float x=h?.25f:-.25f;const Vec3 w{x*std::cos(angle),1.25f-x*std::sin(angle),-.4f};hand.joints[1].pose.position=w;unsigned finger{};
        for(unsigned p:{7u,12u,17u,22u}){const float dx=-.03f+.022f*static_cast<float>(finger++);const bool closed=h?right:left;
            hand.joints[p].pose.position={w.x+dx,w.y,w.z-.075f};hand.joints[p+3].pose.position={w.x+dx,w.y+(closed?-.025f:0),w.z+(closed?-.05f:-.165f)};}
        hand.joints[5].pose.position={w.x+.14f,w.y,w.z-.1f};
    }return f;
}
}
