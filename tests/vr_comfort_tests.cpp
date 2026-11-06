#include "swvr/comfort.hpp"
#include "swvr/control_recording.hpp"
#include "swvr/hand_locomotion.hpp"
#include "swvr/hand_turn.hpp"
#include "swvr/body_yaw.hpp"
#include "swvr/performance.hpp"
#include "swvr/vr_settings_menu.hpp"
#include <iostream>
#include <sstream>
using namespace swvr;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
XrRuntimeFrame fixture(){XrRuntimeFrame f;f.id=1;f.predicted_display_time_ns=1'000'000'000;f.space_revision=1;f.running=f.focused=f.should_render=f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
    f.eyes[0].pose.position={-.032f,1.6f,0};f.eyes[1].pose.position={.032f,1.6f,0};
    for(unsigned h=0;h<2;++h){auto& hand=f.hands[h];hand.active=true;for(auto& j:hand.joints)j.position_valid=j.position_tracked=true;const Vec3 w{h?.25f:-.25f,1.3f,-.35f};hand.joints[1].pose.position=w;
        unsigned finger{};for(unsigned p:{7u,12u,17u,22u}){const float x=-.03f+.022f*static_cast<float>(finger++);hand.joints[p].pose.position={w.x+x,w.y,w.z-.075f};hand.joints[p+3].pose.position={w.x+x,w.y-.025f,w.z-.05f};}}
    return f;
}
void tick(XrRuntimeFrame& f){++f.id;f.predicted_display_time_ns+=20'000'000;}
void shift_hand(XrRuntimeFrame& f,unsigned hand,float x){for(auto& j:f.hands[hand].joints)j.pose.position.x+=x;tick(f);}
}
int main(int argc,char** argv){try{
    check(argc==2||argc==3,"temporary directory required, optional synthetic report output");const auto temp=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(temp);
    GestureSettings settings;check(!settings.turn.snap&&!settings.comfort.head_fade&&!settings.comfort.vignette_strength&&settings.input.dwell_ms==800,"legacy defaults changed");
    settings.turn.snap=true;settings.turn.snap_degrees=45;settings.comfort={true,true,.75f,35};settings.input.dwell_ms=500;settings.input.menu_scale=1.4f;settings.input.click_sound=true;
    check(parse_gesture_settings(format_gesture_settings_ini(settings))==settings,"new settings did not round trip");
    for(const auto invalid:{"[input]\ndwell_ms=0","[input]\nmenu_scale=2","[turn]\nsnap_degrees=180","[comfort]\nvignette_strength=nan","[comfort]\nhead_fade=yes"}){
        bool rejected{};try{parse_gesture_settings(invalid);}catch(const std::exception&){rejected=true;}check(rejected,"invalid comfort setting accepted");}
    for(int n=0;n<100;++n){adjust_vr_preferences(VrMenuAction::dwell_less,settings);adjust_vr_preferences(VrMenuAction::scale_more,settings);}check(settings.input.dwell_ms==300&&settings.input.menu_scale==1.4f,"UI range clamping failed");
    for(const auto hand:{GestureHand::left,GestureHand::right})for(unsigned angle:{30u,45u}){
        settings={};settings.turn.snap=true;settings.turn.snap_degrees=angle;settings.input.joystick_hand=hand;
        auto f=fixture();HandTurn turn;const unsigned h=1-static_cast<unsigned>(hand);check(turn.update(f,settings,true)==0&&turn.using_hand(),"snap did not anchor");
        shift_hand(f,h,.12f);const auto expected=angle*.017453292519943295;check(std::abs(turn.update(f,settings,true)-expected)<1e-8,"wrong snap angle");
        for(int n=0;n<20;++n){tick(f);check(turn.update(f,settings,true)==0,"held snap repeated");}
        shift_hand(f,h,-.24f);check(turn.update(f,settings,true)==0,"snap reversed without returning to neutral");
        shift_hand(f,h,.12f);check(turn.update(f,settings,true)==0,"neutral caused a snap");shift_hand(f,h,-.12f);check(std::abs(turn.update(f,settings,true)+expected)<1e-8,"neutral did not rearm opposite snap");
        f.focused=false;check(turn.update(f,settings,true)==0&&!turn.using_hand(),"dashboard retained snap grip");f.focused=true;tick(f);check(turn.update(f,settings,true)==0,"focus recovery snapped immediately");
    }
    // The native body and XR play space must both accept a 45-degree snap.
    LookYawCalibration yaw;Matrix4d camera{};camera[0]=camera[5]=camera[10]=camera[15]=1;camera=rotate_game_yaw(camera,.6);
    for(int n=0;n<16;++n)yaw.observe(.6f,camera);check(yaw.locked(),"yaw fixture failed");
    const auto snap=follow_headset_yaw_turning(.6f,.6f,yaw,camera,camera,.0175,.78539816339);check(std::abs(snap.turn-.78539816339)<1e-6&&std::abs(snap.field-1.38539816339)<1e-6,"native yaw swallowed snap");
    // Physical head translation is untouched in fade mode, while legacy limiting stays available.
    auto f=fixture();const auto origin=gravity_aligned_origin({f.eyes[0].pose,f.eyes[1].pose});const auto neck=neck_position(origin,{f.eyes[0].pose,f.eyes[1].pose});
    auto lean=std::array{f.eyes[0].pose,f.eyes[1].pose};for(auto& eye:lean)eye.position.x+=.4f;
    const auto fade=head_comfort_view(origin,neck,lean,{},true,true),limit=head_comfort_view(origin,neck,lean,{},true,false);
    check(fade.fade==1&&fade.origin.position.x==origin.position.x&&limit.origin.position.x!=origin.position.x,"head fade modified physical motion or legacy limit disappeared");
    check(head_comfort_view(origin,neck,{f.eyes[0].pose,f.eyes[1].pose},{},true,true).fade==0,"centre remained faded");
    check(head_comfort_view(origin,neck,lean,{},false,true).fade==0,"NoHeadLimit did not disable head fade");
    ComfortMotion motion;ComfortSettings comfort;comfort.vignette_strength=.75f;
    motion.update(f,comfort,true,0,0);tick(f);const auto partial=motion.update(f,comfort,true,0,0);check(partial.vignette>0&&partial.vignette<.75f,"vignette did not ease in");
    for(int n=0;n<20;++n){tick(f);motion.update(f,comfort,true,0,0);}tick(f);check(motion.update(f,comfort,true,0,0).vignette==.75f,"vignette never reached selected intensity");
    for(int n=0;n<20;++n){tick(f);motion.update(f,comfort,false,0,0);}tick(f);check(motion.update(f,comfort,false,0,0).vignette==0,"stopped movement retained vignette");
    f.focused=false;check(motion.update(f,comfort,true,1,1).fade==0&&motion.update(f,comfort,true,1,1).vignette==0,"comfort effect covered system dashboard");
    // A system overlay can retain live tracking; all application input must still release.
    HandLocomotion movement;movement.set_mode(LocomotionMode::arm_swing);f=fixture();movement.publish(f,true);check(movement.inspect().sprint,"fist fixture did not run");
    f.focused=false;tick(f);movement.publish(f,true);check(!movement.inspect().active,"unfocused live hands kept running");f.focused=true;tick(f);movement.publish(f,true);check(movement.inspect().sprint,"fists recovery stopped following user preference");
    // A larger target is hittable outside the old bounds; dwell is configured and feedback lasts 150 ms.
    const auto path=temp/("comfort-controls-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".tsv");ControlRecording recorder;recorder.open(path);PointingPanel panel;panel.record_to(&recorder,0);
    const std::array<PanelButton,1> buttons{{{7,"TARGET",0,0,.2f,.08f,true}}};PanelOptions options;options.scale=1.4f;options.dwell_ms=400;
    check(panel.open(f,buttons,"TEST",options),"scaled panel failed");const auto pose=panel.snapshot().panel;f.hands[0].active=false;
    f.hands[1].joints[7].pose.position=compose(pose,Pose{{},{.12f,0,.35f}}).position;f.hands[1].joints[10].pose.position=compose(pose,Pose{{},{.12f,0,.25f}}).position;
    unsigned actions{};for(int n=0;n<=20;++n){tick(f);const auto action=panel.update(f);if(n<20)check(!action,"dwell fired before configured delay");actions+=action.has_value();}
    check(actions==1&&panel.snapshot().pressed==0&&!panel.snapshot().direct_touch,"scaled dwell target or mode missing");
    for(int n=0;n<7;++n){tick(f);check(!panel.update(f)&&panel.snapshot().pressed==0,"press feedback vanished too early or repeated action");}tick(f);panel.update(f);check(panel.snapshot().pressed==-1,"feedback never expired");
    f.focused=false;tick(f);panel.update(f);check(!panel.snapshot().visible,"unfocused panel retained click target");
    HandTurn recorded_turn;recorded_turn.record_to(&recorder);f=fixture();settings={};settings.turn.snap=true;settings.turn.snap_degrees=45;
    recorded_turn.update(f,settings,true);shift_hand(f,1,.12f);check(recorded_turn.update(f,settings,true)>.7,"recorded turn did not snap");tick(f);recorded_turn.update(f,settings,true);recorded_turn.reset();
    recorder.close();std::ifstream source(path);std::ostringstream output;const auto replay=replay_controls(source,output);check(!replay.mismatches&&replay.panel_checks==30&&replay.turn_checks==3,"scaled menu or snap replay diverged");source.close();std::filesystem::remove(path);
    std::istringstream old("SWVR_CONTROLS 1\nEND\n");std::ostringstream old_output;check(!replay_controls(old,old_output).mismatches,"legacy controls schema rejected");
    PerformanceReport performance;performance.cpu_sample(8,10,1);performance.cpu_sample(12,10,2);performance.gpu_sample(6,10,1);performance.gpu_sample(11,10,2);performance.gpu_sample(9,0,3);
    check(performance.cpu_over_budget==1&&performance.gpu_over_budget==1&&performance.gpu_budget_samples==2&&performance.runtime_period.mean()==10,"runtime budget accounting wrong");
    if(argc==3){performance.gpu_available=true;performance.save(std::filesystem::absolute(argv[2]),9,1,0,0);}
    std::cout<<"PASS VR comfort: focus lifecycle, snap turning, 1:1 head, vignette, settings, scaled UI, feedback, replay and frame budgets\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
