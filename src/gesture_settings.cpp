#include "swvr/gesture_settings.hpp"
#include <charconv>
#include <atomic>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <cmath>
#include <fstream>
#include <iterator>
#include <locale>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace swvr {
namespace {
std::string_view trim(std::string_view v) {
    const auto first=v.find_first_not_of(" \t\r\n");
    return first==v.npos?std::string_view{}:v.substr(first,v.find_last_not_of(" \t\r\n")-first+1);
}
void require(bool good,const char* message) {if(!good)throw std::invalid_argument(message);}
void range(float value,float lo,float hi,const char* name) {
    if(!std::isfinite(value)||value<lo||value>hi)throw std::invalid_argument(std::string("Out of range: ")+name);
}
}
void validate_gesture_settings(const GestureSettings& s) {
    for(const auto hand:{s.input.joystick_hand,s.input.pointing_priority})require(hand==GestureHand::left||hand==GestureHand::right,"Invalid hand preference");
    const auto& j=s.joystick;const auto& a=s.arm_swing;
    require(j.activation==JoystickActivation::whole_hand||j.activation==JoystickActivation::middle_pinch,"Invalid joystick activation");
    require(a.activation==RunActivation::two_fists||a.activation==RunActivation::alternating,"Invalid run activation");
    range(s.input.menu_distance_m,.6f,1.2f,"menu_distance_m");
    range(s.input.menu_scale,1,1.4f,"menu_scale");require(s.input.dwell_ms>=300&&s.input.dwell_ms<=2000,"dwell_ms must be 300..2000");
    require(s.turn.snap_degrees==30||s.turn.snap_degrees==45,"snap_degrees must be 30 or 45");
    range(s.comfort.vignette_strength,0,1,"vignette_strength");range(s.comfort.vignette_radius_degrees,25,60,"vignette_radius_degrees");
    range(s.turn.speed_degrees,15,120,"turn.speed_degrees");range(s.turn.dead_zone_m,.005f,.1f,"turn.dead_zone_m");range(s.turn.full_deflection_m,.02f,.4f,"turn.full_deflection_m");
    require(s.turn.full_deflection_m>s.turn.dead_zone_m,"Turn threshold order");
    range(j.dead_zone_m,.005f,.1f,"dead_zone_m");range(j.full_deflection_m,.02f,.4f,"full_deflection_m");
    range(j.sprint_on_m,.02f,.4f,"sprint_on_m");range(j.sprint_off_m,.01f,.4f,"sprint_off_m");
    range(j.pinch_press_m,.005f,.045f,"pinch_press_m");range(j.pinch_release_m,.01f,.08f,"pinch_release_m");
    require(j.full_deflection_m>j.dead_zone_m&&j.sprint_on_m>j.sprint_off_m&&j.sprint_off_m>j.dead_zone_m,"Joystick threshold order");
    require(j.pinch_release_m>j.pinch_press_m,"Pinch release must exceed press");
    range(a.stroke_m,.02f,.25f,"stroke_m");range(a.minimum_speed_mps,.05f,2.f,"minimum_speed_mps");
    range(a.maximum_speed_mps,.5f,8.f,"maximum_speed_mps");
    require(a.maximum_speed_mps>a.minimum_speed_mps,"Speed threshold order");
    require(a.start_half_strokes>=2&&a.start_half_strokes<=8,"start_half_strokes must be 2..8");
    require(a.minimum_half_stroke_ms>=40&&a.minimum_half_stroke_ms<=500,"minimum_half_stroke_ms must be 40..500");
    require(a.maximum_half_stroke_ms>=200&&a.maximum_half_stroke_ms<=2000&&a.maximum_half_stroke_ms>a.minimum_half_stroke_ms,"maximum_half_stroke_ms invalid");
    require(a.stop_delay_ms>=100&&a.stop_delay_ms<=600&&a.stop_delay_ms<=a.maximum_half_stroke_ms,"stop_delay_ms invalid");
    range(a.fist_close_wrist_ratio,.7f,1.6f,"fist_close_wrist_ratio");range(a.fist_open_wrist_ratio,.8f,1.9f,"fist_open_wrist_ratio");
    range(a.fist_close_knuckle_ratio,.4f,1.2f,"fist_close_knuckle_ratio");range(a.fist_open_knuckle_ratio,.5f,1.5f,"fist_open_knuckle_ratio");
    require(a.fist_close_wrist_ratio<a.fist_open_wrist_ratio&&a.fist_close_knuckle_ratio<a.fist_open_knuckle_ratio,"Fist hysteresis threshold order");
}
GestureSettings parse_gesture_settings(std::string_view text,GestureSettings s) {
    auto& j=s.joystick;auto& a=s.arm_swing;
    const std::map<std::string,float*> floats{
        {"input.menu_scale",&s.input.menu_scale},{"comfort.vignette_strength",&s.comfort.vignette_strength},{"comfort.vignette_radius_degrees",&s.comfort.vignette_radius_degrees},
        {"turn.speed_degrees",&s.turn.speed_degrees},{"turn.dead_zone_m",&s.turn.dead_zone_m},{"turn.full_deflection_m",&s.turn.full_deflection_m},
        {"joystick.dead_zone_m",&j.dead_zone_m},{"joystick.full_deflection_m",&j.full_deflection_m},
        {"joystick.sprint_on_m",&j.sprint_on_m},{"joystick.sprint_off_m",&j.sprint_off_m},
        {"joystick.pinch_press_m",&j.pinch_press_m},{"joystick.pinch_release_m",&j.pinch_release_m},{"input.menu_distance_m",&s.input.menu_distance_m},
        {"arm_swing.stroke_m",&a.stroke_m},{"arm_swing.minimum_speed_mps",&a.minimum_speed_mps},{"arm_swing.maximum_speed_mps",&a.maximum_speed_mps},
        {"arm_swing.fist_close_wrist_ratio",&a.fist_close_wrist_ratio},{"arm_swing.fist_open_wrist_ratio",&a.fist_open_wrist_ratio},
        {"arm_swing.fist_close_knuckle_ratio",&a.fist_close_knuckle_ratio},{"arm_swing.fist_open_knuckle_ratio",&a.fist_open_knuckle_ratio}};
    const std::map<std::string,unsigned*> ints{{"input.dwell_ms",&s.input.dwell_ms},{"turn.snap_degrees",&s.turn.snap_degrees},{"arm_swing.start_half_strokes",&a.start_half_strokes},
        {"arm_swing.minimum_half_stroke_ms",&a.minimum_half_stroke_ms},{"arm_swing.maximum_half_stroke_ms",&a.maximum_half_stroke_ms},
        {"arm_swing.stop_delay_ms",&a.stop_delay_ms}};
    std::set<std::string> seen;std::string section;unsigned line{};
    while(!text.empty()) {
        ++line;const auto end=text.find('\n');auto value=trim(text.substr(0,end));text=end==text.npos?std::string_view{}:text.substr(end+1);
        if(value.empty()||value.front()=='#'||value.front()==';')continue;
        try {
            if(value.front()=='['&&value.back()==']') {
                section=trim(value.substr(1,value.size()-2));require(section=="joystick"||section=="arm_swing"||section=="input"||section=="turn"||section=="comfort","Unknown section");continue;
            }
            const auto eq=value.find('=');require(eq!=value.npos,"Expected key=value");
            const auto key=section+"."+std::string(trim(value.substr(0,eq)));value=trim(value.substr(eq+1));
            require(seen.insert(key).second,"Duplicate setting");
            if(key=="input.click_sound"||key=="turn.snap"||key=="comfort.head_fade"||key=="comfort.seated"){
                require(value=="true"||value=="false","Expected true or false");
                if(key=="input.click_sound")s.input.click_sound=value=="true";else if(key=="turn.snap")s.turn.snap=value=="true";else if(key=="comfort.head_fade")s.comfort.head_fade=value=="true";else s.comfort.seated=value=="true";
            }else if(key=="input.movement_mode"){
                require(value=="fists"||value=="joystick","Unknown movement mode");s.input.movement_fists=value=="fists";
            }else if(key=="joystick.activation"){
                require(value=="whole_hand"||value=="middle_pinch","Unknown joystick activation");j.activation=value=="whole_hand"?JoystickActivation::whole_hand:JoystickActivation::middle_pinch;
            }else if(key=="arm_swing.activation"){
                require(value=="two_fists"||value=="alternating","Unknown run activation");a.activation=value=="two_fists"?RunActivation::two_fists:RunActivation::alternating;
            }else if(key=="input.joystick_hand"||key=="input.pointing_priority"){
                require(value=="left"||value=="right","Hand must be left or right");
                (key=="input.joystick_hand"?s.input.joystick_hand:s.input.pointing_priority)=value=="left"?GestureHand::left:GestureHand::right;
            }else if(const auto it=floats.find(key);it!=floats.end()) {
                float number{};const auto result=std::from_chars(value.data(),value.data()+value.size(),number);
                require(result.ec==std::errc{}&&result.ptr==value.data()+value.size()&&std::isfinite(number),"Invalid finite number");*it->second=number;
            }else if(const auto integer=ints.find(key);integer!=ints.end()) {
                unsigned number{};const auto result=std::from_chars(value.data(),value.data()+value.size(),number);
                require(result.ec==std::errc{}&&result.ptr==value.data()+value.size(),"Invalid integer");*integer->second=number;
            }else throw std::invalid_argument("Unknown setting: "+key);
        }catch(const std::exception& e){throw std::invalid_argument("Line "+std::to_string(line)+": "+e.what());}
    }
    validate_gesture_settings(s);return s;
}
GestureSettings load_gesture_settings(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);if(!input)throw std::invalid_argument("Cannot open gesture configuration");
    std::string text(16385,'\0');input.read(text.data(),static_cast<std::streamsize>(text.size()));text.resize(static_cast<std::size_t>(input.gcount()));
    require(!input.bad(),"Cannot read gesture configuration");require(text.size()<=16384,"Gesture configuration too large");
    if(text.starts_with("\xef\xbb\xbf"))text.erase(0,3);
    return parse_gesture_settings(text);
}
std::string describe_gesture_settings(const GestureSettings& s) {
    std::ostringstream out;out.imbue(std::locale::classic());out.precision(std::numeric_limits<float>::max_digits10);const auto& j=s.joystick;const auto& a=s.arm_swing;
    out<<"joystick_activation="<<(j.activation==JoystickActivation::whole_hand?"whole_hand":"middle_pinch")<<" dead_zone_m="<<j.dead_zone_m<<" full_deflection_m="<<j.full_deflection_m<<" sprint_on_m="<<j.sprint_on_m<<" sprint_off_m="<<j.sprint_off_m
       <<" pinch_press_m="<<j.pinch_press_m<<" pinch_release_m="<<j.pinch_release_m<<" run_activation="<<(a.activation==RunActivation::two_fists?"two_fists":"alternating")<<" stroke_m="<<a.stroke_m<<" minimum_speed_mps="<<a.minimum_speed_mps
       <<" maximum_speed_mps="<<a.maximum_speed_mps<<" start_half_strokes="<<a.start_half_strokes<<" minimum_half_stroke_ms="<<a.minimum_half_stroke_ms
       <<" maximum_half_stroke_ms="<<a.maximum_half_stroke_ms<<" stop_delay_ms="<<a.stop_delay_ms<<" fist_close_wrist_ratio="<<a.fist_close_wrist_ratio
       <<" fist_open_wrist_ratio="<<a.fist_open_wrist_ratio<<" fist_close_knuckle_ratio="<<a.fist_close_knuckle_ratio<<" fist_open_knuckle_ratio="<<a.fist_open_knuckle_ratio
       <<" joystick_hand="<<(s.input.joystick_hand==GestureHand::left?"left":"right")<<" pointing_priority="<<(s.input.pointing_priority==GestureHand::left?"left":"right")<<" menu_distance_m="<<s.input.menu_distance_m
       <<" movement_mode="<<(s.input.movement_fists?"fists":"joystick")<<" dwell_ms="<<s.input.dwell_ms<<" menu_scale="<<s.input.menu_scale<<" click_sound="<<(s.input.click_sound?"true":"false")
       <<" turn_speed_degrees="<<s.turn.speed_degrees<<" turn_dead_zone_m="<<s.turn.dead_zone_m<<" turn_full_deflection_m="<<s.turn.full_deflection_m<<" turn_snap="<<(s.turn.snap?"true":"false")<<" turn_snap_degrees="<<s.turn.snap_degrees
       <<" comfort_head_fade="<<(s.comfort.head_fade?"true":"false")<<" comfort_seated="<<(s.comfort.seated?"true":"false")<<" comfort_vignette_strength="<<s.comfort.vignette_strength<<" comfort_vignette_radius_degrees="<<s.comfort.vignette_radius_degrees;
    return out.str();
}
std::string format_gesture_settings_ini(const GestureSettings& s){
    validate_gesture_settings(s);std::istringstream fields(describe_gesture_settings(s));std::ostringstream out;out<<"[joystick]\n";std::string field;
    while(fields>>field){if(field.starts_with("run_activation=")){out<<"[arm_swing]\nactivation="<<field.substr(15)<<'\n';continue;}
        if(field.starts_with("turn_speed_degrees="))out<<"[turn]\n";
        if(field.starts_with("turn_")){out<<field.substr(5)<<'\n';continue;}
        if(field.starts_with("comfort_head_fade="))out<<"[comfort]\n";
        if(field.starts_with("comfort_")){out<<field.substr(8)<<'\n';continue;}
        if(field.starts_with("joystick_activation=")){out<<"activation="<<field.substr(20)<<'\n';continue;}
        if(field.starts_with("joystick_hand="))out<<"[input]\n";out<<field<<'\n';}return out.str();
}
void save_gesture_settings(const std::filesystem::path& path,const GestureSettings& settings){
    const auto text=format_gesture_settings_ini(settings);require(path.is_absolute(),"Settings path must be absolute");
    static std::atomic<unsigned long long> serial{};
    auto temporary=path;temporary+=L".tmp-"+std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count())+L"-"+std::to_wstring(++serial);
    require(!std::filesystem::exists(temporary),"Settings staging collision");
    try{{std::ofstream out(temporary,std::ios::binary);out<<text;out.flush();require(static_cast<bool>(out),"Cannot write settings");out.close();require(!out.fail(),"Cannot close settings");}
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace settings");
#else
        std::filesystem::rename(temporary,path);
#endif
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
}
