#include "swvr/hand_locomotion.hpp"
#include "swvr/hand_turn.hpp"
#include "swvr/vr_settings_menu.hpp"
#include "swvr/hand_input_guard.hpp"
#include <iostream>
#include <stdexcept>
using namespace swvr;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
XrRuntimeFrame hands(bool left_closed,bool right_closed){
    XrRuntimeFrame f;f.id=1;f.space_revision=1;f.predicted_display_time_ns=1'000'000'000;
    f.running=f.focused=f.should_render=f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
    f.eyes[0].pose.position={-.032f,1.65f,0};f.eyes[1].pose.position={.032f,1.65f,0};
    for(unsigned h=0;h<2;++h){auto& hand=f.hands[h];hand.active=true;for(auto& j:hand.joints)j.position_valid=j.position_tracked=true;
        const Vec3 w{h?.25f:-.25f,1.35f,-.35f};hand.joints[1].pose.position=w;unsigned finger{};
        for(unsigned p:{7u,12u,17u,22u}){const float x=-.03f+.022f*static_cast<float>(finger++);const bool closed=h?right_closed:left_closed;
            hand.joints[p].pose.position={w.x+x,w.y,w.z-.075f};hand.joints[p+3].pose.position={w.x+x,w.y+(closed?-.025f:0),w.z+(closed?-.05f:-.165f)};}
        // Thumb stays far from both index and middle: no pinch is performed.
        hand.joints[5].pose.position={w.x+.14f,w.y,w.z-.10f};
    }return f;
}
int main(){unsigned failed{};const auto test=[&](const char* name,auto fn){try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    test("closed grip is not a system palm",[]{auto f=hands(true,true);for(auto& hand:f.hands){auto& palm=hand.joints[0];palm.pose.position=hand.joints[1].pose.position;palm.orientation_valid=palm.orientation_tracked=true;palm.pose.orientation={-.70710678f,0,0,.70710678f};}
        check(!suppress_system_hand_input(f)&&f.hands[0].active&&f.hands[1].active,"system fallback suppressed closed fists");});
    test("two stationary fists",[]{HandLocomotion m;m.set_mode(LocomotionMode::arm_swing);auto f=hands(true,true);auto now=HandLocomotion::Clock::time_point{};
        m.publish(f,true,now);check(m.inspect(now).sprint,"two closed fists did not start immediately");
        for(unsigned i=0;i<60;++i){++f.id;f.predicted_display_time_ns+=20'000'000;now+=std::chrono::milliseconds(20);m.publish(f,true,now);check(m.inspect(now).sprint,"stationary fists stopped the active run");}
        f.hands[1]=hands(true,false).hands[1];++f.id;f.predicted_display_time_ns+=20'000'000;m.publish(f,true,now);check(!m.inspect(now).active,"opening a hand did not stop");});
    test("fist joystick without pinch",[]{HandLocomotion m;auto f=hands(false,false);auto now=HandLocomotion::Clock::time_point{};m.publish(f,true,now);
        f=hands(true,false);f.id=2;f.predicted_display_time_ns+=20'000'000;now+=std::chrono::milliseconds(20);m.publish(f,true,now);check(m.inspect(now).active,"closed joystick hand required a pinch");
        ++f.id;f.predicted_display_time_ns+=20'000'000;for(auto& j:f.hands[0].joints)j.pose.position.z-=.14f;now+=std::chrono::milliseconds(20);m.publish(f,true,now);check(m.inspect(now).sprint,"fist deflection did not sprint");});
    test("inferred fingers and immediate recovery",[]{HandLocomotion m;m.set_mode(LocomotionMode::arm_swing);auto f=hands(true,true);auto now=HandLocomotion::Clock::time_point{};
        for(auto& h:f.hands){for(unsigned j:{10u,15u,20u,25u})h.joints[j].position_tracked=false;h.joints[25].position_valid=false;}
        m.publish(f,true,now);check(m.inspect(now).sprint,"occluded valid finger estimates rejected a tracked fist");
        f.focused=false;++f.id;f.predicted_display_time_ns+=20'000'000;m.publish(f,true,now);check(!m.inspect(now).active,"focus loss retained sprint");
        f.focused=true;++f.id;f.predicted_display_time_ns+=20'000'000;m.publish(f,true,now);check(m.inspect(now).sprint,"recovery demanded a preliminary open-hand gesture");
        f.hands[0].joints[20].position_valid=false;++f.id;f.predicted_display_time_ns+=20'000'000;m.publish(f,true,now);check(!m.inspect(now).active,"too few valid fingers retained sprint");});
    test("stale XR pose cannot renew a fist run",[]{HandLocomotion m;m.set_mode(LocomotionMode::arm_swing);auto f=hands(true,true);auto now=HandLocomotion::Clock::time_point{};m.publish(f,true,now);check(m.inspect(now).sprint,"setup failed");
        for(int i=0;i<4;++i){++f.id;now+=std::chrono::milliseconds(20);m.publish(f,true,now);check(!m.inspect(now).active,"repeated XR timestamp renewed a closed-fist command");}
        f.predicted_display_time_ns+=20'000'000;++f.id;m.publish(f,true,now);check(m.inspect(now).sprint,"fresh fists did not recover");});
    test("joystick selected hand and neutral regrip",[]{for(auto selected:{GestureHand::left,GestureHand::right}){HandLocomotion m;GestureSettings settings;settings.input.joystick_hand=selected;m.configure(settings);auto f=hands(true,true);auto now=HandLocomotion::Clock::time_point{};
        m.publish(f,true,now);check(locomotion_buttons(m.inspect(now))==0,"joystick activated forward movement at its neutral grip");
        const auto h=static_cast<unsigned>(selected);for(auto& j:f.hands[h].joints)j.pose.position.z-=.14f;++f.id;f.predicted_display_time_ns+=20'000'000;m.publish(f,true,now);check(m.inspect(now).sprint,"selected grip did not steer");
        f.hands[h]=hands(false,false).hands[h];++f.id;f.predicted_display_time_ns+=20'000'000;m.publish(f,true,now);check(!m.inspect(now).active,"open grip retained movement");
        f.hands[h]=hands(true,true).hands[h];++f.id;f.predicted_display_time_ns+=20'000'000;m.publish(f,true,now);check(locomotion_buttons(m.inspect(now))==0,"regrip reused the old neutral point");}});
    test("menu distance",[]{VrSettingsMenu menu;auto f=hands(false,false);check(menu.open(f,{true,true,true},{},false),"cannot open");check(-menu.snapshot().panel.position.z>=.9f,"panel is still too close");});
    test("menu distance configuration",[]{VrSettingsMenu menu;auto f=hands(false,false);GestureSettings settings;settings.input.menu_distance_m=.8f;menu.open(f,{true,true,true},settings,false);check(std::abs(menu.snapshot().panel.position.z+.8f)<.0001f,"configured menu distance ignored");});
    test("menu dwell without pinch",[]{VrSettingsMenu menu;auto f=hands(false,false);menu.open(f,{true,true,true},{},false);const auto panel=menu.snapshot().panel;
        const auto button=menu.snapshot().buttons[0];for(auto& hand:f.hands)hand.active=false;auto& hand=f.hands[1];hand.active=true;
        hand.joints[5].position_tracked=hand.joints[15].position_tracked=false;
        hand.joints[7].pose.position=compose(panel,Pose{{},{button.x,button.y,.35f}}).position;hand.joints[10].pose.position=compose(panel,Pose{{},{button.x,button.y,.25f}}).position;
        std::optional<VrMenuAction> action;for(unsigned i=0;i<50;++i){++f.id;f.predicted_display_time_ns+=20'000'000;if(const auto result=menu.update(f))action=result;}
        check(action==VrMenuAction::joystick,"point and hold required a finger pinch");});
    test("dwell once, rearm and interrupted tracking",[]{PointingPanel panel;auto f=hands(false,false);const std::array<PanelButton,1> button{{{7,"TEST",0,0,.2f,.08f,true}}};panel.open(f,button,"");
        auto& hand=f.hands[1];f.hands[0].active=false;const auto pose=panel.snapshot().panel;
        hand.joints[7].pose.position=compose(pose,Pose{{},{0,0,.35f}}).position;hand.joints[10].pose.position=compose(pose,Pose{{},{0,0,.25f}}).position;
        const auto tick=[&](){++f.id;f.predicted_display_time_ns+=20'000'000;return panel.update(f);};unsigned actions{};
        for(int i=0;i<120;++i)actions+=tick().has_value();check(actions==1,"stationary pointing repeated an action");
        hand.active=false;tick();hand.active=true;for(int i=0;i<20;++i)check(!tick(),"reacquisition reused prior dwell time");
        hand.active=false;tick();hand.active=true;for(int i=0;i<42;++i)actions+=tick().has_value();check(actions==2,"new continuous dwell did not rearm after interruption");});
    test("panel stays upright under head roll",[]{PointingPanel panel;auto f=hands(false,false);for(auto& e:f.eyes)e.pose.orientation={0,0,.258819f,.965926f};
        const std::array<PanelButton,1> buttons{{{7,"TEST",0,0,.2f,.08f,true}}};check(panel.open(f,buttons,""),"open failed");
        const auto p=panel.snapshot().panel;const auto up=compose(p,Pose{{},{0,1,0}}).position;
        check(std::abs(up.x-p.position.x)<.0001f&&std::abs(up.z-p.position.z)<.0001f,"menu inherited head roll");});
    test("small ray edge jitter preserves dwell",[]{PointingPanel panel;auto f=hands(false,false);const std::array<PanelButton,1> buttons{{{7,"TEST",0,0,.2f,.08f,true}}};panel.open(f,buttons,"");
        f.hands[0].active=false;const auto pose=panel.snapshot().panel;auto& hand=f.hands[1];unsigned actions{};
        for(int i=0;i<65;++i){const float x=i%10==9?.102f:.095f;
            hand.joints[7].pose.position=compose(pose,Pose{{},{x,0,.35f}}).position;hand.joints[10].pose.position=compose(pose,Pose{{},{x,0,.25f}}).position;
            ++f.id;f.predicted_display_time_ns+=20'000'000;actions+=panel.update(f).has_value();}
        check(actions==1,"2 mm edge jitter prevented a VR menu click");});
    test("independent second hand smooth turning",[]{for(auto selected:{GestureHand::left,GestureHand::right}){HandTurn turn;HandLocomotion movement;GestureSettings settings;settings.input.joystick_hand=selected;movement.configure(settings);
        auto f=hands(true,true);const auto h=1u-static_cast<unsigned>(selected);check(turn.update(f,settings,true)==0&&turn.using_hand(),"grip did not anchor");
        for(auto& j:f.hands[h].joints)j.pose.position.x+=.12f;++f.id;f.predicted_display_time_ns+=20'000'000;
        const double step=turn.update(f,settings,true);check(step>.020&&step<.022,"right deflection did not turn 60 degrees/s");
        check(turn.update(f,settings,true)==0,"same XR frame turned twice");
        movement.publish(f,true);check(locomotion_buttons(movement.diagnostic_command())==0,"turn grip generated walking");
        for(auto& j:f.hands[h].joints)j.pose.position.x-=.24f;++f.id;f.predicted_display_time_ns+=20'000'000;
        check(turn.update(f,settings,true)<-.020,"left deflection did not reverse turn");
        f.hands[h]=hands(false,false).hands[h];++f.id;f.predicted_display_time_ns+=20'000'000;
        check(turn.update(f,settings,true)==0&&!turn.using_hand(),"opening turn hand did not stop");}});
    test("turn gates, translation and reanchoring",[]{HandTurn turn;GestureSettings settings;auto f=hands(false,true);turn.update(f,settings,true);
        const auto tick=[&](bool allowed=true){++f.id;f.predicted_display_time_ns+=20'000'000;return turn.update(f,settings,allowed);};
        for(auto& e:f.eyes)e.pose.position.x+=.1f;for(auto& h:f.hands)for(auto& j:h.joints)j.pose.position.x+=.1f;
        check(tick()==0,"ordinary room translation caused turning");
        for(auto& j:f.hands[1].joints)j.pose.position.x+=.12f;check(tick()>.02,"deflection setup failed");
        check(tick(false)==0&&!turn.using_hand(),"UI block retained turning");check(tick()==0,"resuming did not use a fresh neutral");
        for(auto& j:f.hands[1].joints)j.pose.position.x+=.12f;check(tick()>.02,"second deflection failed");
        f.focused=false;check(tick()==0&&!turn.using_hand(),"focus loss kept turning");f.focused=true;check(tick()==0,"focus recovery jumped");
        f.predicted_display_time_ns+=1'000'000'000;check(tick()==0,"tracking pause caught up old turning");
        settings.input.joystick_hand=GestureHand::right;f.hands[0]=hands(true,true).hands[0];check(tick()==0,"hand swap reused neutral");
        f.hands[0].joints[1].position_tracked=false;check(tick()==0&&!turn.using_hand(),"untracked wrist kept turning");});
    test("head calibration resets position and preserves heading",[]{auto f=hands(false,false);const Pose old{{0,.342020f,0,.939693f},{2,1.4f,3}};
        const auto centered=recenter_head_origin(old,{f.eyes[0].pose,f.eyes[1].pose});const auto eyes=cockpit_views({},centered,{f.eyes[0].pose,f.eyes[1].pose});
        const auto center=stereo_center(eyes);check(std::hypot(center.position.x,center.position.y,center.position.z)<.0001f,"accepted calibration retained old head offset");
        check(centered.orientation.y==old.orientation.y&&centered.orientation.w==old.orientation.w,"head recenter rotated the world");
        const auto neck=neck_position(centered,{f.eyes[0].pose,f.eyes[1].pose});check(!limit_head(centered,neck,{f.eyes[0].pose,f.eyes[1].pose},{}).limited,"neck was not recentered with the head");});
    test("fast fingertip crosses the menu plane",[]{PointingPanel panel;auto f=hands(false,false);const std::array<PanelButton,1> buttons{{{7,"TEST",0,0,.2f,.08f,true}}};panel.open(f,buttons,"");
        f.hands[0].active=false;auto& hand=f.hands[1];hand.joints[7].position_tracked=false;const auto pose=panel.snapshot().panel;
        const auto tick=[&](float depth){hand.joints[10].pose.position=compose(pose,Pose{{},{0,0,depth}}).position;++f.id;f.predicted_display_time_ns+=20'000'000;return panel.update(f);};
        check(!tick(.05f),"approaching finger clicked early");check(tick(-.05f)==7,"finger passed through button between frames without clicking");
        check(!tick(-.06f),"finger behind panel repeated the click");});
    test("only the displayed menu dwell may activate",[]{PointingPanel panel;auto f=hands(false,false);const std::array<PanelButton,2> buttons{{{7,"LEFT",-.11f,0,.18f,.08f,true},{8,"RIGHT",.11f,0,.18f,.08f,true}}};panel.open(f,buttons,"");
        const auto pose=panel.snapshot().panel;const auto aim=[&](unsigned h,float x){f.hands[h].joints[7].pose.position=compose(pose,Pose{{},{x,0,.35f}}).position;f.hands[h].joints[10].pose.position=compose(pose,Pose{{},{x,0,.25f}}).position;};
        aim(0,-.11f);aim(1,.11f);f.hands[1].active=false;
        for(int i=0;i<55;++i){if(i==30)f.hands[1].active=true;++f.id;f.predicted_display_time_ns+=20'000'000;
            check(!panel.update(f),"background hand activated a different button than the displayed dwell");}
        check(panel.snapshot().hover==1,"preferred pointer not displayed");});
    test("duplicate pose with lost turn tracking releases ownership",[]{HandTurn turn;GestureSettings settings;auto f=hands(false,true);turn.update(f,settings,true);
        f.hands[1].active=false;check(turn.update(f,settings,true)==0&&!turn.using_hand(),"tracking loss in a repeated sample retained turn ownership");});
    return failed?1:0;
}
