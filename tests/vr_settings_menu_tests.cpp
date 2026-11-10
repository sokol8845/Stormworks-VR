#include "swvr/vr_settings_menu.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>
using namespace swvr;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct Fixture {
    XrRuntimeFrame frame;VrSettingsMenu menu;
    Fixture(){frame.id=1;frame.predicted_display_time_ns=1'000'000'000;frame.space_revision=1;frame.running=frame.focused=frame.should_render=frame.position_valid=frame.orientation_valid=frame.position_tracked=frame.orientation_tracked=true;frame.eyes[0].pose.position={-.032f,1.6f,0};frame.eyes[1].pose.position={.032f,1.6f,0};check(menu.open(frame,{true,true,true},{},false),"menu open failed");}
    std::optional<VrMenuAction> aim(VrMenuAction action,bool closed,unsigned hand=1){const auto& state=menu.snapshot();const PanelButton* key{};for(const auto& b:state.buttons)if(b.id==static_cast<int>(action))key=&b;check(key!=nullptr,"missing menu button");auto& h=frame.hands[hand];h.active=true;
        for(unsigned j:{5u,7u,10u,15u})h.joints[j].position_valid=h.joints[j].position_tracked=true;
        h.joints[7].pose.position=compose(state.panel,Pose{{},{key->x,key->y,.25f}}).position;h.joints[10].pose.position=compose(state.panel,Pose{{},{key->x,key->y,.15f}}).position;h.joints[15].pose.position=compose(state.panel,Pose{{},{key->x,key->y-.07f,.15f}}).position;h.joints[5].pose.position=compose(state.panel,Pose{{},{key->x+(closed?.01f:.06f),key->y-.07f,.15f}}).position;++frame.id;frame.predicted_display_time_ns+=20'000'000;return menu.update(frame);}
    std::optional<VrMenuAction> click(VrMenuAction key,unsigned hand=1){std::optional<VrMenuAction> result;const auto title=menu.snapshot().title;for(int i=0;i<42;++i){if(const auto value=aim(key,false,hand))result=value;if(!menu.snapshot().visible||menu.snapshot().title!=title)break;}frame.hands[hand].active=false;++frame.id;frame.predicted_display_time_ns+=20'000'000;menu.update(frame);return result;}
    void pose(bool high,bool open=true){for(unsigned h=0;h<2;++h){auto& hand=frame.hands[h];hand.active=true;for(auto& j:hand.joints)j.position_valid=j.position_tracked=true;const Vec3 w{h?.25f:-.25f,high?1.55f:1.1f,-.35f};hand.joints[1].pose.position=w;for(unsigned p:{7u,12u,17u,22u}){hand.joints[p].pose.position={w.x,w.y,w.z-.06f};hand.joints[p+3].pose.position={w.x,w.y,w.z-(open?.16f:.04f)};}}}
    bool summon(VrMenuSummon& detector){++frame.id;frame.predicted_display_time_ns+=20'000'000;return detector.update(frame);}
};
int main(){try{
    Fixture f;check(f.click(VrMenuAction::arm_swing)==VrMenuAction::arm_swing&&f.menu.snapshot().visible,"mode action closed menu or was not delivered");
    check(!f.click(VrMenuAction::movement_page)&&f.menu.snapshot().title=="CHODZENIE","movement page did not open");
    check(f.click(VrMenuAction::joystick_right,0)==VrMenuAction::joystick_right,"left hand could not operate settings");
    GestureSettings walking;const auto forward=f.click(VrMenuAction::forward_only_toggle);check(forward&&adjust_vr_preferences(*forward,walking)&&walking.joystick.forward_only,"forward-only switch missing");
    f.menu.status(walking,false);check(f.menu.snapshot().value.find("TYLKO PRZOD: WL")!=std::string::npos&&f.menu.snapshot().value.find("OBROT P")!=std::string::npos,"walking mode/turn hand not shown");
    {std::string label;for(const auto& b:f.menu.snapshot().buttons)if(b.id==static_cast<int>(VrMenuAction::forward_only_toggle))label=b.label;check(label=="TYLKO PRZOD: WL","forward-only switch does not name its state");}
    const auto full=f.click(VrMenuAction::forward_only_toggle);check(full&&adjust_vr_preferences(*full,walking)&&!walking.joystick.forward_only,"forward-only switch did not turn off");
    check(!f.click(VrMenuAction::back)&&f.menu.snapshot().title=="USTAWIENIA VR","movement back missing");
    check(!f.click(VrMenuAction::clear_profile)&&f.menu.snapshot().title=="USUNAC KALIBRACJE?","profile deletion skipped confirmation");
    check(f.click(VrMenuAction::clear_profile)==VrMenuAction::clear_profile&&!f.menu.snapshot().visible,"confirmed deletion not delivered once");
    Fixture preferences;check(!preferences.click(VrMenuAction::controls)&&preferences.menu.snapshot().title=="OBROT I MENU","controls page did not open");
    auto settings=GestureSettings{};const auto speed=preferences.click(VrMenuAction::turn_faster);check(speed&&adjust_vr_preferences(*speed,settings)&&settings.turn.speed_degrees==75,"speed action was not delivered");
    preferences.menu.status(settings,false);check(preferences.menu.snapshot().value.find("75 DEG/S")!=std::string::npos,"speed setting not shown");
    check(preferences.menu.take_click()&&!preferences.menu.take_click(),"menu click feedback duplicated or missing");
    check(!preferences.click(VrMenuAction::interface_page)&&preferences.menu.snapshot().title=="INTERFEJS","interface page missing");
    check(preferences.click(VrMenuAction::sound_toggle)==VrMenuAction::sound_toggle,"sound action missing");
    check(!preferences.click(VrMenuAction::controls)&&preferences.menu.snapshot().title=="OBROT I MENU","interface back failed");
    check(!preferences.click(VrMenuAction::comfort_page)&&preferences.menu.snapshot().title=="KOMFORT","comfort page missing");
    check(preferences.click(VrMenuAction::turn_toggle)==VrMenuAction::turn_toggle,"snap toggle missing");
    check(!preferences.click(VrMenuAction::controls),"comfort back emitted external action");
    check(!preferences.click(VrMenuAction::back)&&preferences.menu.snapshot().title=="USTAWIENIA VR","back did not restore main menu");
    Fixture tools;check(!tools.click(VrMenuAction::tools_page)&&tools.menu.snapshot().title=="NARZEDZIA","tools page missing");
    check(tools.click(VrMenuAction::diagnostics_toggle)==VrMenuAction::diagnostics_toggle&&tools.click(VrMenuAction::mark_problem)==VrMenuAction::mark_problem,"diagnostic/marker action missing");
    {const auto label=[&]{for(const auto& b:tools.menu.snapshot().buttons)if(b.id==static_cast<int>(VrMenuAction::diagnostics_toggle))return b.label;return std::string();};
     tools.menu.diagnostics_enabled(true);tools.menu.status({},false);check(label()=="DIAGNOSTYKA: WL","diagnostics switch does not name its on state");
     tools.menu.diagnostics_enabled(false);tools.menu.status({},false);check(label()=="DIAGNOSTYKA: WYL","diagnostics switch does not name its off state");}
    tools.click(VrMenuAction::steering_page);GestureSettings wheel;auto wheel_action=tools.click(VrMenuAction::wheel_lock_more);check(wheel_action&&adjust_vr_preferences(*wheel_action,wheel)&&wheel.steering.full_lock_degrees==105,"wheel settings not editable");
    for(unsigned n=0;n<100;++n){adjust_vr_preferences(VrMenuAction::wheel_lock_more,wheel);adjust_vr_preferences(VrMenuAction::wheel_dead_more,wheel);adjust_vr_preferences(VrMenuAction::wheel_smooth_more,wheel);}
    check(wheel.steering.full_lock_degrees==180&&wheel.steering.dead_zone_degrees==15&&wheel.steering.smoothing_ms==200,"wheel upper bounds");
    for(unsigned n=0;n<100;++n){adjust_vr_preferences(VrMenuAction::wheel_lock_less,wheel);adjust_vr_preferences(VrMenuAction::wheel_dead_less,wheel);adjust_vr_preferences(VrMenuAction::wheel_smooth_less,wheel);}
    check(wheel.steering.full_lock_degrees==30&&wheel.steering.dead_zone_degrees==0&&wheel.steering.smoothing_ms==0,"wheel lower bounds");
    tools.click(VrMenuAction::tools_page);tools.click(VrMenuAction::profiles_page);check(!tools.click(VrMenuAction::profile_driving)&&tools.menu.selected_profile()==ControlProfile::driving,"slot selection applied external action");
    check(tools.menu.snapshot().value.find("JAZDA")!=std::string::npos,"selected profile not shown");
    for(auto action:{VrMenuAction::profile_save,VrMenuAction::profile_load,VrMenuAction::profile_restore})check(tools.click(action)==action,"profile host action missing");
    {   // Walking adjustment page, preview switches, grip reset and practice.
        Fixture tuning;GestureSettings s;const auto label=[&](VrMenuAction action){for(const auto& b:tuning.menu.snapshot().buttons)if(b.id==static_cast<int>(action))return b.label;return std::string();};
        tuning.click(VrMenuAction::movement_page);tuning.menu.status(s,false);check(label(VrMenuAction::joystick_visual_toggle)=="PODGLAD JOYSTICKOW: WYL","joystick preview state not shown as off");
        const auto preview=tuning.click(VrMenuAction::joystick_visual_toggle);check(preview&&adjust_vr_preferences(*preview,s)&&s.input.joystick_visual&&tuning.menu.snapshot().visible,"joystick preview switch missing");
        tuning.menu.status(s,false);check(label(VrMenuAction::joystick_visual_toggle)=="PODGLAD JOYSTICKOW: WL"&&tuning.menu.snapshot().value.find("OBROT P")!=std::string::npos,"joystick preview state not shown as on");
        check(!tuning.click(VrMenuAction::movement_tuning_page)&&tuning.menu.snapshot().title=="REGULACJA CHODZENIA","walking adjustment page did not open");
        tuning.menu.status(s,false);check(tuning.menu.snapshot().value=="STREFA 30 / ZAKRES 120 / BIEG 120/90 MM","walking thresholds not shown in millimetres");
        const auto value=[&](unsigned field){const auto& j=s.joystick;return field==0?j.dead_zone_m:field==1?j.full_deflection_m:field==2?j.sprint_on_m:j.sprint_off_m;};
        const std::array pairs{std::pair{VrMenuAction::joy_dead_less,VrMenuAction::joy_dead_more},std::pair{VrMenuAction::joy_range_less,VrMenuAction::joy_range_more},std::pair{VrMenuAction::sprint_on_less,VrMenuAction::sprint_on_more},std::pair{VrMenuAction::sprint_off_less,VrMenuAction::sprint_off_more}};
        // Start from the middle so every button has room to move its own value.
        s.joystick.dead_zone_m=.03f;s.joystick.full_deflection_m=.15f;s.joystick.sprint_off_m=.08f;s.joystick.sprint_on_m=.15f;validate_gesture_settings(s);
        for(unsigned field=0;field<pairs.size();++field)for(const bool more:{false,true}){const auto before=s;const auto action=tuning.click(more?pairs[field].second:pairs[field].first);
            check(action&&adjust_vr_preferences(*action,s)&&tuning.menu.snapshot().title=="REGULACJA CHODZENIA","walking adjustment button missing or left the page");
            check(more?value(field)>(field==0?before.joystick.dead_zone_m:field==1?before.joystick.full_deflection_m:field==2?before.joystick.sprint_on_m:before.joystick.sprint_off_m):value(field)<(field==0?before.joystick.dead_zone_m:field==1?before.joystick.full_deflection_m:field==2?before.joystick.sprint_on_m:before.joystick.sprint_off_m),"walking adjustment moved the wrong way");
            for(unsigned other=0;other<pairs.size();++other)if(other!=field){const auto& j=before.joystick;check(value(other)==(other==0?j.dead_zone_m:other==1?j.full_deflection_m:other==2?j.sprint_on_m:j.sprint_off_m),"walking adjustment changed another value");}}
        tuning.menu.status(s,false);check(tuning.menu.snapshot().value=="STREFA 30 / ZAKRES 150 / BIEG 150/80 MM","adjusted walking thresholds not shown");
        // Limits keep the documented order: dead zone < sprint stop < sprint start, dead zone < range.
        for(unsigned n=0;n<200;++n)for(const auto& pair:pairs)adjust_vr_preferences(pair.second,s);
        check(s.joystick.dead_zone_m<s.joystick.sprint_off_m&&s.joystick.sprint_off_m<s.joystick.sprint_on_m&&s.joystick.dead_zone_m<s.joystick.full_deflection_m&&s.joystick.full_deflection_m<=.4f&&s.joystick.sprint_on_m<=.4f,"walking upper limits lost their order");
        tuning.menu.status(s,false);check(tuning.menu.snapshot().value.size()<=40&&tuning.menu.snapshot().value.ends_with("MM")&&tuning.menu.snapshot().value.find("ZAKRES 400")!=std::string::npos,"largest walking thresholds do not fit the value line");
        for(unsigned n=0;n<200;++n)for(const auto& pair:pairs)adjust_vr_preferences(pair.first,s);
        check(s.joystick.dead_zone_m>=.005f&&s.joystick.dead_zone_m<s.joystick.sprint_off_m&&s.joystick.sprint_off_m<s.joystick.sprint_on_m&&s.joystick.dead_zone_m<s.joystick.full_deflection_m,"walking lower limits lost their order");
        check(!tuning.click(VrMenuAction::movement_page)&&tuning.menu.snapshot().title=="CHODZENIE","walking adjustment back missing");
        tuning.click(VrMenuAction::back);tuning.click(VrMenuAction::tools_page);tuning.click(VrMenuAction::steering_page);tuning.menu.status(s,false);
        check(label(VrMenuAction::wheel_visual_toggle)=="POKAZ CHWYT: WYL","grip preview state not shown as off");
        const auto grip=tuning.click(VrMenuAction::wheel_visual_toggle);check(grip&&adjust_vr_preferences(*grip,s)&&s.steering.show_grip,"grip preview switch missing");
        tuning.menu.status(s,false);check(label(VrMenuAction::wheel_visual_toggle)=="POKAZ CHWYT: WL","grip preview state not shown as on");
        GestureSettings untouched=s;check(tuning.click(VrMenuAction::wheel_grip_reset)==VrMenuAction::wheel_grip_reset&&!adjust_vr_preferences(VrMenuAction::wheel_grip_reset,s)&&s==untouched,"grip reset is not a host-only command");
        tuning.click(VrMenuAction::tools_page);check(tuning.click(VrMenuAction::practice)==VrMenuAction::practice&&!adjust_vr_preferences(VrMenuAction::practice,s)&&s==untouched,"practice is not a host-only command");
    }
    Fixture calibration;check(calibration.click(VrMenuAction::calibrate)==VrMenuAction::calibrate&&!calibration.menu.snapshot().visible,"guided calibration did not dismiss panel");
    Fixture disabled;disabled.menu.open(disabled.frame,{false,false,true},{},false);check(!disabled.click(VrMenuAction::arm_swing)&&!disabled.click(VrMenuAction::calibrate)&&!disabled.click(VrMenuAction::movement_page),"disabled action accepted");
    Fixture gesture;VrMenuSummon summon;gesture.pose(true);for(int i=0;i<60;++i)check(!gesture.summon(summon),"raised palms opened the application menu");
    gesture.pose(false);gesture.summon(summon);gesture.pose(true);for(int i=0;i<70;++i)check(!gesture.summon(summon),"legacy near-face pose conflicts with Quest system input");
    gesture.pose(false);gesture.summon(summon);gesture.pose(true,false);for(int i=0;i<70;++i)check(!gesture.summon(summon),"fists summoned menu");
    gesture.frame.focused=false;gesture.summon(summon);gesture.frame.focused=true;gesture.pose(true);for(int i=0;i<60;++i)check(!gesture.summon(summon),"focus recovery triggered a menu action");
    check(summon.snapshot().visible&&summon.snapshot().compact&&summon.snapshot().buttons[0].label=="MENU","touch launcher is not visible");
    const auto head_center=stereo_center({gesture.frame.eyes[0].pose,gesture.frame.eyes[1].pose});
    check(summon.snapshot().panel.position.y>head_center.position.y+.1f,"MENU launcher still blocks chest-level controls");
    const auto poke=[&](float z){auto& h=gesture.frame.hands[1];h.active=true;h.joints[10].position_valid=h.joints[10].position_tracked=true;
        h.joints[10].pose.position=compose(summon.snapshot().panel,Pose{{},{0,0,z}}).position;return gesture.summon(summon);};
    check(!poke(.06f)&&poke(.006f),"MENU tile could not be touched with an index finger");
    for(int i=0;i<5;++i)check(!gesture.summon(summon),"held MENU tile reopened repeatedly");
    // Moving the head-locked tile into a stationary finger is not a poke.
    VrMenuSummon moving;Fixture head;moving.update(head.frame);auto& tip=head.frame.hands[1].joints[10];head.frame.hands[1].active=true;tip.position_valid=tip.position_tracked=true;
    tip.pose.position=compose(moving.snapshot().panel,Pose{{},{0,0,.06f}}).position;head.summon(moving);
    for(auto& eye:head.frame.eyes)eye.pose.position.z+=.06f;
    check(!head.summon(moving),"head motion manufactured a MENU press");
    std::cout<<"PASS menu: middle pinch, touch launcher, disabled actions, confirmation and system gesture separation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
