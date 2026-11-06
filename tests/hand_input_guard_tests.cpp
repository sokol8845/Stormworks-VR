#include "swvr/hand_input_guard.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace swvr;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    XrRuntimeFrame original;original.position_valid=true;original.eyes[0].pose.position={-.032f,1.6f,0};original.eyes[1].pose.position={.032f,1.6f,0};
    for(auto& hand:original.hands){hand.active=true;auto& palm=hand.joints[0];palm.position_valid=palm.position_tracked=palm.orientation_valid=palm.orientation_tracked=true;palm.pose.position={.2f,1.5f,-.35f};
        hand.joints[1].position_valid=true;hand.joints[1].pose.position=palm.pose.position;
        for(unsigned p:{12u,17u,22u}){hand.joints[p].position_valid=hand.joints[p+3].position_valid=true;hand.joints[p].pose.position={.2f,1.5f,-.41f};hand.joints[p+3].pose.position={.2f,1.5f,-.51f};}}
    auto frame=original;check(!suppress_system_hand_input(frame)&&frame.hands[0].active,"palm-down application input suppressed");
    for(unsigned h=0;h<2;++h){frame=original;frame.hands[h].joints[0].pose.orientation={-std::sqrt(.5f),0,0,std::sqrt(.5f)};
        check(suppress_system_hand_input(frame)&&!frame.hands[0].active&&!frame.hands[1].active,"palm facing headset did not suppress both application hands");
        frame=original;std::array<bool,2> signal{};signal[h]=true;
        check(suppress_system_hand_input(frame,signal)&&!frame.hands[0].active&&!frame.hands[1].active,"runtime system flag ignored");}
    frame=original;for(auto& hand:frame.hands)hand.joints[0].orientation_tracked=false;
    check(!suppress_system_hand_input(frame),"unknown orientation guessed to be a system pose");
    std::cout<<"PASS reserved system input: runtime flag, palm geometry, both hands and unknown orientation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
