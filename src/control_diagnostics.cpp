#include "swvr/control_diagnostics.hpp"
#include "swvr/hand_closure.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>
namespace swvr {
namespace {
std::string number(float value,unsigned precision=2){std::ostringstream out;out.imbue(std::locale::classic());if(!std::isfinite(value))return "?";out<<std::fixed<<std::setprecision(precision)<<value;return out.str();}
}
std::vector<std::string> control_diagnostic_lines(const XrRuntimeFrame& f,const GestureSettings& s,const ControlDiagnostics& d){
    const char* reason=!f.running||!f.should_render?"BRAK AKTYWNEJ KLATKI":!f.focused?"BRAK FOKUSU VR":f.system_input_suppressed?"GEST SYSTEMOWY QUEST":
        !f.position_valid||!f.orientation_valid||!f.position_tracked||!f.orientation_tracked?"BRAK SLEDZENIA GLOWY":!d.camera_current?"BRAK NOWEJ KAMERY GRY":d.settings_pending?"ZMIANA USTAWIEN":d.calibration?"KALIBRACJA":d.keypad?"KLAWIATURA VR":d.menu?"MENU VR":d.blocked?"BLAD PRZYCISKOW":
        d.own_hold?"TRZYMANY PRZYCISK":d.pending?"ZDARZENIE W KOLEJCE":!d.pointing.valid_ray?"BRAK PROMIENIA DO GRY":"PROMIEN / CEL NIEPOTWIERDZONY";
    const auto hand_name=[](bool present){return present?"TAK":"NIE";};
    const auto fist=[&](unsigned h){auto v=hand_closed(f.hands[h],false,s.arm_swing);return !v?"BRAK":*v?"PIESC":"OTWARTA";};
    const auto owner=[&](unsigned h){return d.menu||d.keypad?"PANEL":d.steering.hands[h]?"KIER":d.movement_hands[h]?"RUCH":d.turning_hand&&h!=static_cast<unsigned>(s.input.joystick_hand)?"OBROT":d.pointing.valid_ray&&h==static_cast<unsigned>(d.pointing.hand)?"CEL":"WOLNA";};
    std::vector<std::string> lines{
        std::string("TRYB: ")+(s.input.movement_fists?"PIESCI":"JOYSTICK")+(!s.input.movement_fists&&s.joystick.forward_only?" / TYLKO PRZOD":""),
        std::string("L: ")+fist(0)+" / "+owner(0)+" | P: "+fist(1)+" / "+owner(1),
        "RUCH PRZOD "+number(d.movement.forward)+" BOK "+number(d.movement.sideways),
        "OBROT "+number(d.turn_degrees_per_second,0)+" DEG/S / SPRINT "+hand_name(d.movement.sprint),
        "KIER "+number(d.steering.axis)+" / KAT "+number(d.steering.angle_radians*57.2957795f,0)+" DEG",
        std::string("POJAZD ")+hand_name(d.seated)+" / KLIK: "+reason,
        "KLIK OK "+std::to_string(d.accepted)+" / ODMOWY "+std::to_string(d.denied),
        "OST. ODMOWA: "+d.last_denial.substr(0,26)
    };
    for(auto& line:lines){if(line.size()>48)line.resize(48);for(auto& c:line){if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');if(c=='_'||c=='|')c='/';}}
    return lines;
}
}
