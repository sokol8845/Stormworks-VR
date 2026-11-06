#include "swvr/gesture_settings.hpp"
#include <iostream>
#include <stdexcept>
using namespace swvr;
int main(){try{
    const auto hands=parse_gesture_settings("[input]\njoystick_hand=right\npointing_priority=left\n");
    const auto controls=parse_gesture_settings("[joystick]\nactivation=whole_hand\n[arm_swing]\nactivation=two_fists\n[input]\nmenu_distance_m=.95\n");
    const auto roundtrip=parse_gesture_settings(format_gesture_settings_ini(controls));
    if(roundtrip.joystick.activation!=JoystickActivation::whole_hand||roundtrip.arm_swing.activation!=RunActivation::two_fists||roundtrip.input.menu_distance_m!=.95f)throw std::runtime_error("activation/distance roundtrip lost");
    if(hands.input.joystick_hand!=GestureHand::right||hands.input.pointing_priority!=GestureHand::left||parse_gesture_settings(format_gesture_settings_ini(hands)).input.pointing_priority!=GestureHand::left)throw std::runtime_error("hand preferences lost");
    const auto s=parse_gesture_settings("[joystick]\r\ndead_zone_m=0.05\n[arm_swing]\nstart_half_strokes=4\n");
    if(s.joystick.dead_zone_m!=.05f||s.arm_swing.start_half_strokes!=4||s.arm_swing.stroke_m!=.06f)throw std::runtime_error("Settings/defaults");
    for(const char* text:{"[oops]\nx=1","[joystick]\ndeadzone_m=.02","[joystick]\ndead_zone_m=.02\ndead_zone_m=.03",
        "[joystick]\ndead_zone_m=nan","[joystick]\nsprint_off_m=.2","[joystick]\ndead_zone_m=.02garbage",
        "[arm_swing]\nstart_half_strokes=3.5","[arm_swing]\nstart_half_strokes=-1","[arm_swing]\nstop_delay_ms=999",
        "[arm_swing]\nfist_open_wrist_ratio=1.1","[arm_swing]\nminimum_speed_mps=2\nmaximum_speed_mps=1","[joystick]\nactivation=pinch","[arm_swing]\nactivation=magic","[input]\nmenu_distance_m=.2"}) {
        bool rejected{};try{parse_gesture_settings(text);}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)throw std::runtime_error(std::string("Accepted invalid settings: ")+text);
    }
    std::cout<<describe_gesture_settings(s)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
