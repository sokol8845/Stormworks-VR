#include "swvr/avatar_pose.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using namespace swvr;using M=Matrix4d;using V=std::array<double,3>;
M identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix4 fm(M m){Matrix4 r{};for(int i=0;i<16;++i)r[i]=static_cast<float>(m[i]);return r;}
M mul(M a,M b){M r{};for(int c=0;c<4;++c)for(int i=0;i<4;++i)for(int k=0;k<4;++k)r[c*4+i]+=a[k*4+i]*b[c*4+k];return r;}
double distance(V a,V b){double d{};for(int i=0;i<3;++i)d+=(a[i]-b[i])*(a[i]-b[i]);return std::sqrt(d);}
void require(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
template<class T>void append(std::vector<unsigned char>& b,T v){auto p=reinterpret_cast<const unsigned char*>(&v);b.insert(b.end(),p,p+sizeof(v));}
std::vector<unsigned char> asset(){
 const std::array<std::string,19> names{"root","stomach","chest","head","right_shoulder","right_elbow","right_hand","right_fingers","right_fingers_2","left_shoulder","left_elbow","left_hand","left_fingers","right_hip","right_knee","right_foot","left_hip","left_knee","left_foot"};
 const std::array<int,19> parent{-1,0,1,2,2,4,5,6,7,2,9,10,11,0,13,14,0,16,17};
 const std::array<V,19> pos{{{0,.8,0},{0,.2,0},{0,.25,0},{0,.25,0},{-.2,.15,0},{-.28,0,0},{-.25,0,0},{-.08,0,0},{-.06,0,0},{.2,.15,0},{.28,0,0},{.25,0,0},{.08,0,0},{-.1,0,0},{0,-.4,0},{0,-.4,.05},{.1,0,0},{0,-.4,0},{0,-.4,.05}}};
 std::vector<unsigned char> b{'a','n','i','m',1,0,0,0,0,0,0,0};append<std::uint32_t>(b,19);
 for(unsigned i=0;i<19;++i){append<std::uint16_t>(b,static_cast<std::uint16_t>(names[i].size()));b.insert(b.end(),names[i].begin(),names[i].end());for(unsigned k=0;k<9;++k)append<float>(b,k%4==0?1.f:0.f);for(auto v:pos[i])append<float>(b,static_cast<float>(v));append<std::int32_t>(b,parent[i]);unsigned n{};for(auto p:parent)n+=p==static_cast<int>(i);append<std::uint32_t>(b,n);for(unsigned c=0;c<19;++c)if(parent[c]==static_cast<int>(i))append<std::uint32_t>(b,c);}
 return b;
}
struct Fixture {
 AvatarRig rig=read_avatar_rig(asset());AvatarRetargeter retarget;std::array<Matrix4,19> current{},previous{};Matrix4 world=fm(identity());AvatarTargets targets;
 Fixture(){for(auto& m:current)m=fm(identity());previous=current;targets.frame=1;targets.actor=42;targets.valid=true;targets.reference=identity();targets.reference[13]=1.6;targets.head=targets.reference;}
 AvatarPoseResult pose(){return retarget.pose(rig,AvatarVariant::male,targets,world,current,previous);}
 V bone(const AvatarPoseResult& out,unsigned i){M skin{},w{};std::copy(out.current[i].begin(),out.current[i].end(),skin.begin());std::copy(world.begin(),world.end(),w.begin());const auto m=mul(w,mul(skin,rig.bind[i]));return {m[12],m[13],m[14]};}
};
void checks(){
 {Fixture f;auto l=identity(),r=identity();l[12]=.876;r[12]=-.876;l[13]=r[13]=1.35;
 f.targets.hands={l,r};auto body=calibrate_avatar_body(f.targets,1);require(body.has_value(),"tracked T-pose rejected");
 // Each rejection names its first failing condition (shown in the log and as a red flash).
 require(avatar_body_calibration_issue(f.targets)==nullptr,"valid T-pose reported an issue");
 {auto t=f.targets;t.hands[1].reset();require(std::string(avatar_body_calibration_issue(t))=="hand_not_tracked","missing hand reason");
  t=f.targets;t.hands[1]->at(13)+=.2;require(std::string(avatar_body_calibration_issue(t))=="hands_uneven_height","height reason");
  t=f.targets;t.hands[0]->at(14)=-.3;t.hands[1]->at(14)=-.3;t.hands[0]->at(12)=.2;t.hands[1]->at(12)=-.2;require(std::string(avatar_body_calibration_issue(t))=="arms_not_spread","span reason");
  t=f.targets;t.hands[0]->at(14)=.5;t.hands[1]->at(14)=.5;require(std::string(avatar_body_calibration_issue(t))=="hands_too_far_forward","forward reason");
  t=f.targets;t.hands[0]->at(13)=1.6;t.hands[1]->at(13)=1.6;require(std::string(avatar_body_calibration_issue(t))=="hands_too_high","height above shoulders reason");
  // Automatic detection demands a wider span so half-spread arms never recalibrate.
  t=f.targets;t.hands[0]->at(12)=.5;t.hands[1]->at(12)=-.5;
  require(avatar_body_calibration_issue(t)==nullptr&&std::string(avatar_body_calibration_issue(t,avatar_auto_t_pose_min_span))=="arms_not_spread","automatic span threshold");}
 {auto turned=f.targets;const double a=.55,c=std::cos(a),s=std::sin(a);
  for(auto& hand:turned.hands){const double x=hand->at(12),z=hand->at(14);hand->at(12)=c*x+s*z;hand->at(14)=-s*x+c*z;}
  auto calibrated=calibrate_avatar_body(turned,2);require(calibrated.has_value(),"turning in the room rejected a valid T-pose as unequal depth");
  require(std::abs(calibrated->wrist_span-body->wrist_span)<1e-6&&std::abs(calibrated->shoulder_offset[2])<1e-6,"rotated T-pose changed body proportions");}
 auto uncalibrated=f.pose();require(uncalibrated.reach_limited[0],"fixture must expose physical/native reach mismatch");
 f.targets.body=body;auto o=f.pose();require(o.body_calibrated&&std::abs(o.body_scale-1.2)<1e-5,"T-pose body scale incorrect");
 require(!o.reach_limited[0]&&!o.reach_limited[1]&&distance(f.bone(o,11),{.876,1.35,0})<2e-5,"calibrated wrist missed target");
 // wrist_shift reports how far the posed wrist moved from the native wrist (diagnostic for "hands stay on the wheel").
 {const V native_left{f.rig.bind[11][12],f.rig.bind[11][13],f.rig.bind[11][14]},native_right{f.rig.bind[6][12],f.rig.bind[6][13],f.rig.bind[6][14]};
  require(std::abs(o.wrist_shift[0]-distance(f.bone(o,11),native_left))<1e-6&&std::abs(o.wrist_shift[1]-distance(f.bone(o,6),native_right))<1e-6&&o.wrist_shift[0]>.01,"wrist shift not measured");
  auto lost=f.targets.hands[0];f.targets.hands[0].reset();require(f.pose().wrist_shift[0]==0,"untracked wrist reported a shift");f.targets.hands[0]=lost;}
 require(std::abs(distance(f.bone(o,9),f.bone(o,10))-.336)<1e-5,"calibrated upper arm length incorrect");
 // A T-pose held slightly forward (so the headset can see the hands) must not
 // move the body forward: only span and shoulder height come from calibration.
 {auto fl=l,fr=r;fl[14]=fr[14]=.3;auto t=f.targets;t.hands={fl,fr};const auto forward=calibrate_avatar_body(t,5);require(forward.has_value(),"forward T-pose rejected");
  Fixture g;g.targets=f.targets;g.targets.body=forward;const auto og=g.pose();
  const double z_level=(f.bone(o,4)[2]+f.bone(o,9)[2])/2,z_forward=(g.bone(og,4)[2]+g.bone(og,9)[2])/2;
  const double y_level=(f.bone(o,4)[1]+f.bone(o,9)[1])/2,y_forward=(g.bone(og,4)[1]+g.bone(og,9)[1])/2;
  require(std::abs(z_forward-z_level)<1e-6,"forward-held T-pose moved the shoulders forward");
  require(std::abs(y_forward-y_level)<1e-6,"forward-held T-pose changed shoulder height");
  // Arms angled forward shorten the wrist-to-wrist distance; the arm length
  // must include the forward component, so the full sideways reach is reachable.
  const double full=std::sqrt(.876*.876+.3*.3);
  auto wide_l=l,wide_r=r;wide_l[12]=full;wide_r[12]=-full;g.targets.hands={wide_l,wide_r};const auto reach=g.pose();
  require(!reach.reach_limited[0]&&!reach.reach_limited[1]&&distance(g.bone(reach,11),{full,1.35,0})<2e-5,"forward-held T-pose shortened the arms");}
 f.targets.hands[0]->at(12)=4;o=f.pose();require(o.reach_limited[0]&&std::abs(distance(f.bone(o,9),f.bone(o,10))-.336)<1e-5,"calibrated arm stretched dynamically");
 auto female=f.retarget.pose(f.rig,AvatarVariant::female,f.targets,f.world,f.current,f.previous);require(!female.body_calibrated,"calibration leaked to another model");
 f.targets.actor=43;require(!f.pose().body_calibrated,"calibration leaked to another actor");f.targets.actor=42;++f.targets.space_revision;require(f.pose().body_calibrated,"body calibration lost on recenter (LOCAL change)");
 f.targets.hands[1].reset();require(!calibrate_avatar_body(f.targets,2),"one-handed calibration accepted");f.targets.hands={l,r};f.targets.hands[1]->at(13)+=.4;require(!calibrate_avatar_body(f.targets,2),"tilted calibration accepted");
 for(unsigned i=13;i<19;++i)require(o.current[i]==f.current[i],"calibration moved legs");
 }

 {Fixture f;auto o=f.pose();require(o.status==AvatarPoseStatus::applied,"neutral rig rejected");for(unsigned i=0;i<19;++i)for(int k=0;k<16;++k)require(std::abs(o.current[i][k]-f.current[i][k])<1e-5,"neutral pose moved avatar");
 f.targets.head[12]=.2;f.targets.head[13]+=.08;o=f.pose();require(distance(f.bone(o,3),{.2,1.58,0})<1e-5,"head did not follow eye mount");for(unsigned i=13;i<19;++i)require(o.current[i]==f.current[i]&&o.previous[i]==f.previous[i],"hips/legs changed");require(o.current[3]==o.previous[3],"tracked head mixed with native history");
 f.targets.head=offset_game_camera(f.targets.reference,Pose{{0,0,std::sin(.4f),std::cos(.4f)},{.2f,.08f,0}});o=f.pose();auto p=f.bone(o,3);for(int k=0;k<3;++k)p[k]+=f.targets.head[4+k]*.1;require(distance(p,{f.targets.head[12],f.targets.head[13],f.targets.head[14]})<1e-5,"rotation detached eyes from head mount");}
 {Fixture f;(void)f.pose();auto l=identity(),r=identity();l[12]=.42;l[13]=1.25;l[14]=.25;r[12]=-.42;r[13]=1.25;r[14]=.25;f.targets.hands={l,r};auto o=f.pose();require(o.hands[0]&&o.hands[1]&&!o.reach_limited[0]&&!o.reach_limited[1],"reachable wrists rejected");require(distance(f.bone(o,11),{.42,1.25,.25})<1e-5&&distance(f.bone(o,6),{-.42,1.25,.25})<1e-5,"wrist missed target");require(std::abs(distance(f.bone(o,9),f.bone(o,10))-.28)<1e-5&&std::abs(distance(f.bone(o,10),f.bone(o,11))-.25)<1e-5,"IK stretched bones");
 f.targets.hands[0]->at(12)=4;o=f.pose();require(o.reach_limited[0]&&std::abs(distance(f.bone(o,9),f.bone(o,10))-.28)<1e-5,"unreachable wrist stretched arm");f.targets.hands[1].reset();o=f.pose();require(!o.hands[1]&&o.hands[0],"lost hand target retained");f.targets.valid=false;require(f.pose().status==AvatarPoseStatus::inactive,"inactive tracking applied");}
 {Fixture f;for(int i:{0,5,10})f.world[i]=2;f.world[12]=100;f.world[13]=200;f.world[14]=-300;f.targets.reference[12]=100;f.targets.reference[13]=203.2;f.targets.reference[14]=-300;f.targets.head=f.targets.reference;(void)f.pose();f.targets.head[12]+=.2;auto o=f.pose();require(o.status==AvatarPoseStatus::applied&&distance(f.bone(o,3),{100.2,203.,-300})<2e-5,"model scale changed HMD translation");auto large=f.targets;for(auto* m:{&large.reference,&large.head}){(*m)[12]+=1e9;(*m)[14]-=2e9;}f.targets=rebase_avatar_targets(large,{-1e9,0,2e9});o=f.pose();require(distance(f.bone(o,3),{100.2,203.,-300})<2e-5,"floating origin lost centimetres");f.current[3][0]=std::numeric_limits<float>::quiet_NaN();require(f.pose().status==AvatarPoseStatus::invalid_transform,"NaN palette accepted");}
 {Fixture f;auto w=identity();w[0]=0;w[2]=-2;w[5]=2;w[8]=2;w[10]=0;w[12]=100;w[13]=200;w[14]=-300;f.world=fm(w);auto ref=identity();ref[0]=0;ref[2]=-1;ref[8]=-1;ref[10]=0;ref[12]=100;ref[13]=203.2;ref[14]=-300;f.targets.reference=ref;f.targets.head=ref;(void)f.pose();f.targets.head=offset_game_camera(ref,Pose{{0,std::sin(.25f),0,std::cos(.25f)},{.15f,.04f,-.1f}});auto o=f.pose();require(o.status==AvatarPoseStatus::applied,"rotated/reflected mount rejected");M skin{};std::copy(o.current[3].begin(),o.current[3].end(),skin.begin());auto head=mul(w,mul(skin,f.rig.bind[3]));V eye{head[12]+head[4]*.1,head[13]+head[5]*.1,head[14]+head[6]*.1};require(distance(eye,{f.targets.head[12],f.targets.head[13],f.targets.head[14]})<2e-5,"reflection, scale or body rotation detached eye mount");}
 {Fixture f;f.targets.reference[13]=10;f.targets.head=f.targets.reference;require(f.pose().status==AvatarPoseStatus::head_anchor_mismatch,"foreign head palette accepted");auto o=f.retarget.pose(f.rig,AvatarVariant::male,f.targets,f.world,std::span(f.current).first(18),f.previous);require(o.status==AvatarPoseStatus::invalid_palette,"short palette accepted");auto b=asset();b.resize(b.size()-3);bool rejected{};try{(void)read_avatar_rig(b);}catch(const std::exception&){rejected=true;}require(rejected,"truncated skeleton accepted");}
 {XrRuntimeFrame f;f.id=1;f.running=f.focused=f.should_render=f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;f.eyes[0].pose.position={-.032f,1.6f,0};f.eyes[1].pose.position={.032f,1.6f,0};const auto origin=gravity_aligned_origin({f.eyes[0].pose,f.eyes[1].pose});auto& h=f.hands[1];h.active=true;for(auto i:{1u,7u,12u,22u})h.joints[i].position_valid=h.joints[i].position_tracked=true;h.joints[1].pose.position={.3f,1.2f,-.2f};h.joints[7].pose.position={.27f,1.2f,-.3f};h.joints[12].pose.position={.3f,1.2f,-.32f};h.joints[22].pose.position={.34f,1.2f,-.3f};auto a=identity();a[12]=1e9;auto t=make_avatar_targets(f,origin,a);require(t.valid&&t.hands[1]&&std::abs((*t.hands[1])[12]-(1e9+.3))<1e-6,"wrist target or world precision invalid");h.joints[22].position_tracked=false;require(!make_avatar_targets(f,origin,a).hands[1],"untracked palm accepted");f.focused=false;require(!make_avatar_targets(f,origin,a).valid,"unfocused tracking accepted");}
}
}
int main(int argc,char** argv){try{checks();for(int i=1;i<argc;++i){std::ifstream file(std::filesystem::path(argv[i]),std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});const auto rig=swvr::read_avatar_rig(bytes);require(rig.parent[3]==2&&rig.parent[11]==10,"installed rig mismatch");std::cout<<"Verified asset "<<argv[i]<<'\n';}std::cout<<"Avatar mount, scale, floating origin, IK, tracking loss and rig guards passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
