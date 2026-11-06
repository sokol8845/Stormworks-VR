#include "swvr/hand_closure.hpp"
#include <cmath>
namespace swvr {
namespace {
bool valid(const XrTrackedJoint& j){const auto p=j.pose.position;return j.position_valid&&std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
float distance(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y,a.z-b.z);}
}
std::optional<bool> hand_closed(const XrTrackedHand& hand,bool was_closed,const ArmSwingSettings& s){
    if(!hand.active||!valid(hand.joints[1])||!hand.joints[1].position_tracked)return {};
    const auto wrist=hand.joints[1].pose.position;
    float width{};
    if(valid(hand.joints[7])&&valid(hand.joints[22]))width=distance(hand.joints[7].pose.position,hand.joints[22].pose.position);
    else if(valid(hand.joints[7])&&valid(hand.joints[17]))width=1.5f*distance(hand.joints[7].pose.position,hand.joints[17].pose.position);
    if(width<.025f||width>.16f)return {};
    unsigned available{},closed{};bool index_valid{},index_closed{};
    for(unsigned p:{7u,12u,17u,22u}){
        if(!valid(hand.joints[p])||!valid(hand.joints[p+3]))continue;
        const auto proximal=hand.joints[p].pose.position,tip=hand.joints[p+3].pose.position;const auto knuckle=distance(proximal,wrist);
        if(knuckle<.025f||knuckle>.18f)continue;++available;
        const bool folded=distance(tip,wrist)<=knuckle*(was_closed?s.fist_open_wrist_ratio:s.fist_close_wrist_ratio)&&distance(tip,proximal)<=width*(was_closed?s.fist_open_knuckle_ratio:s.fist_close_knuckle_ratio);
        closed+=folded;if(p==7){index_valid=true;index_closed=folded;}
    }
    if(!index_valid||available<3)return {};
    return index_closed&&closed>=3;
}
}
