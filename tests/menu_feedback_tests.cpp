#include "swvr/vr_settings_menu.hpp"
#include "swvr/control_recording.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <chrono>
using namespace swvr;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
XrRuntimeFrame frame(){XrRuntimeFrame f;f.id=1;f.predicted_display_time_ns=1'000'000'000;f.space_revision=1;f.running=f.focused=f.should_render=f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
    f.eyes[0].pose.position={-.032f,1.6f,0};f.eyes[1].pose.position={.032f,1.6f,0};return f;}
void tick(XrRuntimeFrame& f){++f.id;f.predicted_display_time_ns+=20'000'000;}
void aim(XrRuntimeFrame& f,const PanelSnapshot& p,float x,float y,unsigned hand=1){auto& h=f.hands[hand];h.active=true;
    for(unsigned j:{7u,10u})h.joints[j].position_valid=h.joints[j].position_tracked=true;
    h.joints[7].pose.position=compose(p.panel,Pose{{},{x,y,.35f}}).position;h.joints[10].pose.position=compose(p.panel,Pose{{},{x,y,.25f}}).position;tick(f);}
std::optional<VrMenuAction> click(VrSettingsMenu& m,XrRuntimeFrame& f,VrMenuAction action){const auto buttons=m.snapshot().buttons;const auto it=std::find_if(buttons.begin(),buttons.end(),[&](auto& b){return b.id==static_cast<int>(action);});check(it!=buttons.end(),"missing action");
    std::optional<VrMenuAction> result;const auto title=m.snapshot().title;const auto help=m.snapshot().help_mode;for(int n=0;n<110;++n){aim(f,m.snapshot(),it->x,it->y);if(const auto hit=m.update(f))result=hit;if(result||m.snapshot().title!=title||m.snapshot().help_mode!=help)break;}
    f.hands={};tick(f);m.update(f);return result;}
}
int main(int argc,char** argv){unsigned failed{};const auto test=[&](const char* name,auto fn){try{fn();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    test("distance changes in the open panel",[]{auto f=frame();VrSettingsMenu m;GestureSettings s;m.open(f,{true,true,true},s,false);click(m,f,VrMenuAction::controls);const auto before=m.snapshot().panel.position.z;
        const auto action=click(m,f,VrMenuAction::menu_farther);check(action&&adjust_vr_preferences(*action,s),"distance action absent");m.status(s,false);tick(f);m.update(f);check(m.snapshot().panel.position.z<before-.09f,"accepted distance leaves the panel in the same place");});
    test("scale changes in the open panel",[]{auto f=frame();VrSettingsMenu m;GestureSettings s;m.open(f,{true,true,true},s,false);click(m,f,VrMenuAction::controls);click(m,f,VrMenuAction::interface_page);const auto before=m.snapshot().buttons[0].width;
        const auto action=click(m,f,VrMenuAction::scale_more);check(action&&adjust_vr_preferences(*action,s),"scale action absent");m.status(s,false);tick(f);m.update(f);check(m.snapshot().scale>1&&m.snapshot().buttons[0].width>before,"accepted scale leaves the panel unchanged");});
    test("dwell change applies without reopening",[]{auto f=frame();VrSettingsMenu m;GestureSettings s;m.open(f,{true,true,true},s,false);click(m,f,VrMenuAction::controls);click(m,f,VrMenuAction::interface_page);
        s.input.dwell_ms=400;m.status(s,false);tick(f);m.update(f);const auto button=m.snapshot().buttons[0];unsigned actions{};for(int n=0;n<=21;++n){aim(f,m.snapshot(),button.x,button.y);actions+=m.update(f).has_value();}check(actions==1,"open panel still requires its old dwell time");});
    test("short pointing misses do not erase all dwell",[]{auto f=frame();PointingPanel p;const std::array<PanelButton,1> buttons{{{1,"TEST",0,0,.2f,.08f,true}}};p.open(f,buttons,"TEST");unsigned actions{};
        for(int n=0;n<70;++n){aim(f,p.snapshot(),n%12==10?.12f:0,0);const auto hit=p.update(f);if(n%12==10)check(!hit,"off-target pointer activated button");actions+=hit.has_value();}
        check(actions==1,"mostly steady visible pointing never completes after brief misses");});
    test("help only describes every option on every page",[]{for(unsigned page=0;page<8;++page){auto f=frame();VrSettingsMenu m;m.open(f,{page>=4,false,true},{},false);
        if(page>=5){click(m,f,VrMenuAction::tools_page);if(page>5)click(m,f,page==6?VrMenuAction::steering_page:VrMenuAction::profiles_page);}
        else if(page==4)click(m,f,VrMenuAction::movement_page);else {if(page)click(m,f,VrMenuAction::controls);if(page>1)click(m,f,page==2?VrMenuAction::interface_page:VrMenuAction::comfort_page);}
        click(m,f,VrMenuAction::help);check(m.snapshot().help_mode,"help did not enable");m.take_click();const auto title=m.snapshot().title;const auto buttons=m.snapshot().buttons;
        for(const auto& b:buttons){if(b.id==static_cast<int>(VrMenuAction::help))continue;
            for(int n=0;n<60;++n){aim(f,m.snapshot(),b.x,b.y);check(!m.update(f),"help executed an option");m.status({},false);}
            check(!m.take_click()&&m.snapshot().help_mode&&m.snapshot().title==title&&m.snapshot().visible,"help changed page, emitted click or closed menu");
            check(m.snapshot().description.find(vr_menu_help(static_cast<VrMenuAction>(b.id)))!=std::string::npos&&m.snapshot().dwell_progress==0,"help omitted explanation or started click progress");
            for(float z:{.06f,.006f}){f.hands[1].joints[10].pose.position=compose(m.snapshot().panel,Pose{{},{b.x,b.y,z}}).position;tick(f);check(!m.update(f),"touch in help executed an option");}
            check(!m.take_click(),"help touch produced click sound");
        }
        click(m,f,VrMenuAction::help);check(!m.snapshot().help_mode,"help did not disable");const auto back=page==0?VrMenuAction::close:page>=6?VrMenuAction::tools_page:(page==1||page==4||page==5)?VrMenuAction::back:VrMenuAction::controls;click(m,f,back);check(page==0?!m.snapshot().visible:m.snapshot().title!=title,"normal controls did not return after help");
    }});
    test("help preserves profile deletion confirmation",[]{auto f=frame();VrSettingsMenu m;m.open(f,{true,true,true},{},false);click(m,f,VrMenuAction::clear_profile);click(m,f,VrMenuAction::help);
        const auto b=m.snapshot().buttons[1];for(int n=0;n<60;++n){aim(f,m.snapshot(),b.x,b.y);check(!m.update(f),"help dismissed confirmation");}
        check(m.snapshot().description.find("BEZ USUWANIA")!=std::string::npos,"confirmation back help describes the wrong action");click(m,f,VrMenuAction::help);check(!click(m,f,VrMenuAction::close)&&m.snapshot().title=="USTAWIENIA VR","help bypassed confirmation return");});
    test("resizing held target does not manufacture another click",[]{auto f=frame();VrSettingsMenu m;GestureSettings s;m.open(f,{true,true,true},s,false);click(m,f,VrMenuAction::controls);click(m,f,VrMenuAction::interface_page);
        const auto b=m.snapshot().buttons[3];unsigned count{};for(int n=0;n<50;++n){aim(f,m.snapshot(),b.x,b.y);count+=m.update(f).has_value();}check(count==1&&m.snapshot().release_required,"completed click lacks release feedback");
        s.input.menu_scale=1.2f;m.status(s,false);const auto larger=m.snapshot().buttons[3];for(int n=0;n<60;++n){aim(f,m.snapshot(),larger.x,larger.y);check(!m.update(f),"resize repeated an action under held pointer");}check(m.snapshot().release_required,"resized held pointer looks actionable");
        aim(f,m.snapshot(),.5f,0);m.update(f);for(int n=0;n<50;++n){aim(f,m.snapshot(),larger.x,larger.y);count+=m.update(f).has_value();}check(count==2,"leaving target did not rearm action");});
    test("tracking loss and long misses still reset dwell",[]{for(bool tracking:{false,true}){auto f=frame();PointingPanel p;const std::array<PanelButton,1> b{{{1,"TEST",0,0,.2f,.08f,true}}};p.open(f,b,"TEST");
        for(int n=0;n<30;++n){aim(f,p.snapshot(),0,0);check(!p.update(f),"early click");}
        for(int n=0;n<(tracking?1:10);++n){aim(f,p.snapshot(),.3f,0);if(tracking)f.hands[1].active=false;check(!p.update(f),"lost target clicked");}
        for(int n=0;n<25;++n){aim(f,p.snapshot(),0,0);check(!p.update(f),"tracking loss or long miss retained progress");}
    }});
    test("live settings and help replay",[&]{check(argc==2,"temporary directory required");const auto path=std::filesystem::absolute(argv[1])/("menu-help-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".tsv");
        std::filesystem::create_directories(path.parent_path());
        ControlRecording recorder;recorder.open(path);auto f=frame();VrSettingsMenu m;m.record_to(&recorder);GestureSettings s;m.open(f,{true,true,true},s,false);click(m,f,VrMenuAction::controls);
        const auto action=click(m,f,VrMenuAction::menu_farther);check(action&&adjust_vr_preferences(*action,s),"recorded change missing");m.status(s,false);tick(f);m.update(f);
        click(m,f,VrMenuAction::help);const auto b=m.snapshot().buttons[0];for(int n=0;n<60;++n){aim(f,m.snapshot(),b.x,b.y);check(!m.update(f),"recorded help clicked");}click(m,f,VrMenuAction::help);m.cancel();recorder.close();
        std::ifstream input(path);std::ostringstream output;const auto replay=replay_controls(input,output);check(replay.panel_checks>100&&!replay.mismatches,"inspection or live geometry replay diverged");input.close();std::filesystem::remove(path);
    });
    return failed?1:0;
}
