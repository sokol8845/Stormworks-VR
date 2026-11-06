#include "swvr/avatar_calibration.hpp"
#include "swvr/control_recording.hpp"
#include "swvr/hand_turn.hpp"
#include "swvr/performance.hpp"
#include "swvr/vr_settings_menu.hpp"
#include <iostream>
#include <limits>
#include <sstream>
using namespace swvr;
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void rejects(F fn){bool rejected{};try{fn();}catch(const std::exception&){rejected=true;}check(rejected,"invalid input accepted");}
XrRuntimeFrame hands(){XrRuntimeFrame f;f.id=1;f.predicted_display_time_ns=1'000'000'000;f.space_revision=1;f.running=f.focused=f.should_render=f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
 f.eyes[0].pose.position={-.032f,1.6f,0};f.eyes[1].pose.position={.032f,1.6f,0};
 for(unsigned h=0;h<2;++h){auto& hand=f.hands[h];hand.active=true;for(auto& j:hand.joints)j.position_valid=j.position_tracked=true;const Vec3 w{h?.25f:-.25f,1.3f,-.35f};hand.joints[1].pose.position=w;unsigned finger{};
  for(unsigned p:{7u,12u,17u,22u}){const float x=-.03f+.022f*static_cast<float>(finger++);hand.joints[p].pose.position={w.x+x,w.y,w.z-.075f};hand.joints[p+3].pose.position={w.x+x,w.y-.025f,w.z-.05f};}}
 return f;}
void tick(XrRuntimeFrame& f){++f.id;f.predicted_display_time_ns+=20'000'000;}
}
int main(int argc,char** argv){try{check(argc==2,"temporary directory required");const auto temp=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(temp);
 const auto suffix=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 // Calibrate from a stable interval, not the last (noisy) pose.
 AvatarCalibrationWindow window;const auto epoch=AvatarCalibrationWindow::Clock::time_point{};
 AvatarBodyCalibration body{42,3,1,AvatarVariant::male,1.4,{0,-.3,.1}};
 for(int i=0;i<=80;++i){auto noisy=body;const float noise=i%2?.005f:-.005f;noisy.wrist_span+=noise;check(window.observe(noisy,{noise,1.6f+noise,noise},epoch+std::chrono::milliseconds(i*20)),"ordinary jitter rejected");}
 auto spike=body;spike.wrist_span=1.9;check(!window.observe(spike,{.5f,2,0},epoch+std::chrono::milliseconds(1620)),"single tracking spike accepted");
 const auto stable=window.result(2);check(stable&&std::abs(stable->body.wrist_span-1.4)<.001&&std::abs(stable->head_position.x)<.001&&stable->body.generation==2,"calibration used last sample instead of robust average");
 check(!window.observe(body,{},epoch+std::chrono::milliseconds(1600)),"backwards calibration time accepted");
 window.observe({}, {},epoch+std::chrono::milliseconds(2000));check(!window.result(3),"long gap retained stale average");
 AvatarCalibrationWindow moving;for(int i=0;i<=80;++i){auto noisy=body;noisy.wrist_span+=i%2?.055:-.055;moving.observe(noisy,{0,1.6f,0},epoch+std::chrono::milliseconds(i*20));}check(!moving.result(1),"unstable pose passed the calibration window");
 for(int i=0;i<=80;++i)window.observe(body,{0,1.6f,0},epoch+std::chrono::milliseconds(2100+i*20));++body.actor;window.observe(body,{0,1.6f,0},epoch+std::chrono::milliseconds(3740));check(!window.result(4),"actor change retained calibration");
 // Settings survive a restart, including mode and independent turn thresholds.
 GestureSettings settings;settings.turn={90,.04f,.15f};settings.input.movement_fists=false;settings.input.joystick_hand=GestureHand::right;settings.input.menu_distance_m=.8f;
 const auto ini=temp/("settings-"+suffix+".ini");save_gesture_settings(ini,settings);check(load_gesture_settings(ini)==settings,"persistent preferences lost");
 auto bad=settings;bad.turn.speed_degrees=999;rejects([&]{save_gesture_settings(ini,bad);});check(load_gesture_settings(ini)==settings,"invalid save damaged previous settings");
 auto normal=GestureSettings{};check(parse_gesture_settings("[joystick]\ndead_zone_m=.06\n").turn==normal.turn,"legacy joystick thresholds changed turning");
 for(int i=0;i<100;++i){adjust_vr_preferences(VrMenuAction::turn_dead_more,settings);adjust_vr_preferences(VrMenuAction::turn_range_less,settings);validate_gesture_settings(settings);}
 adjust_vr_preferences(VrMenuAction::defaults,settings);check(settings==GestureSettings{},"restore defaults incomplete");save_gesture_settings(ini,settings);check(load_gesture_settings(ini)==settings,"default save failed");std::filesystem::remove(ini);
 // Speed and dead zone can change independently from the walking stick.
 auto f=hands();HandTurn turn;settings.turn={90,.05f,.15f};turn.update(f,settings,true);for(auto& j:f.hands[1].joints)j.pose.position.x+=.04f;tick(f);check(turn.update(f,settings,true)==0,"turn dead zone reused walk threshold");
 for(auto& j:f.hands[1].joints)j.pose.position.x+=.11f;tick(f);check(std::abs(turn.update(f,settings,true)-.0314159265)<1e-6,"turn speed ignored");
 // Record real module operations and replay actions, progress, cancellation and turning.
 const auto path=temp/("controls-"+suffix+".tsv");ControlRecording recorder;recorder.open(path);turn.reset();turn.record_to(&recorder);f=hands();turn.update(f,settings,true);
 for(auto& j:f.hands[1].joints)j.pose.position.x+=.15f;tick(f);turn.update(f,settings,true);turn.update(f,settings,true);f.hands[1].active=false;turn.update(f,settings,true);turn.reset();
 PointingPanel panel;panel.record_to(&recorder,0);f=hands();const std::array<PanelButton,1> buttons{{{7,"TEST",0,0,.2f,.08f,true}}};panel.set_preferred_hand(1);check(panel.open(f,buttons,"TEST"),"panel open failed");
 const auto pose=panel.snapshot().panel;f.hands[0].active=false;auto& hand=f.hands[1];hand.joints[7].pose.position=compose(pose,Pose{{},{0,0,.35f}}).position;hand.joints[10].pose.position=compose(pose,Pose{{},{0,0,.25f}}).position;
 unsigned actions{};for(int i=0;i<45;++i){tick(f);actions+=panel.update(f).has_value();}check(actions==1,"fixture dwell missing");
 hand.joints[10].pose.position=compose(pose,Pose{{},{0,0,.05f}}).position;tick(f);panel.update(f);hand.joints[10].pose.position=compose(pose,Pose{{},{0,0,-.05f}}).position;tick(f);check(panel.update(f)==7,"fixture swept touch missing");
 f.focused=false;tick(f);panel.update(f);recorder.close();check(!recorder.failed(),"control recording failed");
 std::ifstream input(path,std::ios::binary);std::ostringstream output;const auto replay=replay_controls(input,output);check(replay.turn_checks==4&&replay.panel_checks==48&&!replay.mismatches,"control replay diverged");input.close();
 std::ifstream raw(path,std::ios::binary);std::string text{std::istreambuf_iterator<char>(raw),{}};raw.close();const auto at=text.find("T 1 0 1");check(at!=std::string::npos,"fixture turn missing");text.replace(at,5,"T 1 0.5");
 std::istringstream corrupt(text);std::ostringstream diagnostics;check(replay_controls(corrupt,diagnostics).mismatches>0,"replay ignored changed expected turn");
 text.resize(text.size()-4);rejects([&]{std::istringstream truncated(text);std::ostringstream report;replay_controls(truncated,report);});std::filesystem::remove(path);
 const auto limited_path=temp/("controls-limit-"+suffix+".tsv");ControlRecording limited;limited.open(limited_path,1024);limited.turn(f,settings,true,0,false);limited.close();check(limited.limited()&&std::filesystem::file_size(limited_path)<=1024,"controls cap failed");std::ifstream prefix(limited_path);std::ostringstream prefix_out;const auto partial=replay_controls(prefix,prefix_out);check(partial.turn_checks==0&&partial.limited&&prefix_out.str().find("#prefix")!=std::string::npos,"limit produced partial operation or reported completeness");prefix.close();std::filesystem::remove(limited_path);
 // Bounded timing histograms and useful tail percentiles, including long stalls.
 TimingDistribution timing;for(unsigned i=1;i<=100;++i)timing.observe(i,i);timing.observe(std::numeric_limits<double>::quiet_NaN(),0);check(timing.count()==100&&timing.mean()==50.5&&timing.percentile(.95)==95&&timing.percentile(.99)==99,"percentiles wrong");
 timing.observe(500,101);check(timing.percentile(1)==500,"overflow histogram hid a stall");
 PerformanceReport performance;performance.interval=timing;const auto report=temp/("performance-"+suffix+".json");performance.save(report,90,10,2,1);check(std::filesystem::file_size(report)>100,"performance output empty");std::filesystem::remove(report);
 std::cout<<"PASS stable calibration, persistent settings, independent turning, control replay and performance tails\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
