#include "swvr/vr_settings_menu.hpp"
#include <iostream>
#include <stdexcept>
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
