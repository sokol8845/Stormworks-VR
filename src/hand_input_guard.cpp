#include "swvr/hand_input_guard.hpp"
#include <cmath>
namespace swvr {
bool suppress_system_hand_input(XrRuntimeFrame& frame,std::array<bool,2> reported){
    bool suppress=reported[0]||reported[1];
    if(!suppress&&frame.position_valid)try{
        const auto head=stereo_center({frame.eyes[0].pose,frame.eyes[1].pose}).position;
        for(const auto& hand:frame.hands){const auto& palm=hand.joints[0];
            if(!hand.active||!palm.position_valid||!palm.position_tracked||!palm.orientation_valid||!palm.orientation_tracked)continue;
            const auto valid=[](const XrTrackedJoint& j){const auto p=j.pose.position;return j.position_valid&&std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
            if(!valid(hand.joints[1]))continue;
            const auto distance_between=[](Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);};
            unsigned extended{};for(unsigned p:{12u,17u,22u})if(valid(hand.joints[p])&&valid(hand.joints[p+3])&&distance_between(hand.joints[p+3].pose.position,hand.joints[1].pose.position)>1.5f*distance_between(hand.joints[p].pose.position,hand.joints[1].pose.position))++extended;
            // A closed grip is application input, not the palm-up system pose.
            // Require open non-index fingers before applying the fallback.
            if(extended<2)continue;
            const Vec3 to_head{head.x-palm.pose.position.x,head.y-palm.pose.position.y,head.z-palm.pose.position.z};
            const auto distance=std::hypot(to_head.x,to_head.y,to_head.z);if(!std::isfinite(distance)||distance<.10f||distance>.90f)continue;
            // OpenXR joint +Y points to the back of the hand; -Y is the palm.
            const auto normal=compose(Pose{palm.pose.orientation,{}},Pose{{},{0,-1,0}}).position;
            if((normal.x*to_head.x+normal.y*to_head.y+normal.z*to_head.z)/distance>.65f)suppress=true;
        }
    }catch(...){suppress=true;}
    if(suppress)for(auto& hand:frame.hands)hand.active=false;
    return suppress;
}
}
