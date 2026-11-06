#include "swvr/body_yaw.hpp"
#include "swvr/game_profile.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace swvr;
constexpr double pi=3.14159265358979323846;
void require(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
// Column-major camera rotated about +Y by `yaw` and about +X by `pitch` (game convention under test).
Matrix4d camera(double yaw,double pitch=0,double x=0){
    const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    // R = RotY(yaw) * RotX(pitch)
    Matrix4d m{cy,0,-sy,0, sy*sp,cp,cy*sp,0, sy*cp,-sp,cy*cp,0, x,1.7,0,1};
    return m;
}
}
int main(int argc,char** argv){try{
    // At startup the native look is straight ahead. A calibrated HMD looking
    // 15 degrees sideways must align the walking body without a mouse nudge.
    {const float initial=argc>1?std::stof(argv[1]):0.f;
        LookYawCalibration c{10,game_profile::look_yaw_sign};for(int i=0;i<20;++i)c.observe(initial,camera(initial));
        const double head=15*pi/180;
        const auto step=follow_headset_yaw_turning(initial,initial,c,camera(initial+head),camera(initial));
        require(std::abs(wrap_pi(step.field-initial-head))<1e-5,"startup: head/body remain 15 degrees apart until mouse movement");
        const auto next=follow_headset_yaw_turning(step.field,step.field,c,camera(initial+head),camera(step.field));
        require(next.field==step.field&&next.turn==0,"startup alignment was undone on the following frame");}
    for(int sign:{1,-1})for(double offset:{0.,pi})for(double field:{0.,pi/2,-pi/2,pi,-pi}){
        LookYawCalibration c{5,sign};for(int i=0;i<4;++i)c.observe(static_cast<float>(field),camera(sign*field+offset));
        require(!c.locked(),"verified sign skipped live look validation window");c.observe(static_cast<float>(field),camera(sign*field+offset));
        require(c.locked()&&c.sign()==sign&&std::abs(wrap_pi(c.offset()-offset))<1e-6,"verified startup mapping did not lock");
        c.reset();require(!c.locked(),"reset retained calibration");for(int i=0;i<5;++i)c.observe(static_cast<float>(field),camera(sign*field+offset));
        require(c.locked(),"reset lost verified sign");
    }
    {LookYawCalibration c{3,1};for(int i=0;i<20;++i)c.observe(.6f,camera(-.6));require(!c.locked(),"mismatched live look accepted verified profile");}
    {LookYawCalibration c{3,1};c.observe(0,camera(0));c.observe(0,camera(0));auto bad=camera(0);bad[8]=bad[10]=0;c.observe(0,bad);
        c.observe(0,camera(0));require(!c.locked(),"invalid look did not reset consecutive window");}
    require(std::abs(game_camera_yaw(camera(.7))-.7)<1e-12&&std::abs(wrap_pi(3*pi/2)+pi/2)<1e-12&&std::abs(wrap_pi(-3*pi/2)-pi/2)<1e-12,"yaw helpers");
    // Identification for each mapping; pitch must not disturb it.
    for(int sign:{1,-1})for(double offset:{0.,pi}) {
        LookYawCalibration c(5);
        for(int i=0;i<5;++i){const double field=.9+.01*i;c.observe(static_cast<float>(field),camera(sign*field+offset,.4));}
        require(c.locked()&&c.sign()==sign&&std::abs(wrap_pi(c.offset()-offset))<1e-6,"mapping not identified");
    }
    // Ill-conditioned (near 0/pi where sign is ambiguous) and inconsistent frames never lock.
    {LookYawCalibration c(5);for(int i=0;i<20;++i)c.observe(.001f,camera(.001));require(!c.locked(),"ambiguous angle locked");}
    {LookYawCalibration c(5);for(int i=0;i<20;++i)c.observe(1.f,camera(i%2?1.:2.2));require(!c.locked(),"inconsistent frames locked");}
    {LookYawCalibration c(3);for(int i=0;i<3;++i)c.observe(1.f,camera(1.));c.reset();require(!c.locked(),"reset kept lock");}
    // Following: field moves so the game camera yaw reaches the headset yaw, the short way round.
    for(int sign:{1,-1})for(double offset:{0.,pi}) {
        LookYawCalibration c(3);for(int i=0;i<3;++i)c.observe(1.f,camera(sign*1.+offset));
        for(double target:{1.5,-2.9,3.1}) {
            const float field=1.f;const auto game=camera(sign*field+offset);
            const float next=follow_headset_yaw(field,c,camera(target,.3,5),game);
            require(std::abs(wrap_pi(game_camera_yaw(camera(sign*next+offset))-target))<1e-5,"game yaw does not reach headset yaw");
            require(next>=-pi-1e-6&&next<=pi+1e-6,"field not wrapped");
        }
        require(follow_headset_yaw(1.f,c,camera(sign*1.+offset+.01),camera(sign*1.+offset))==1.f,"dead zone moved the body");
    }
    // Artificial (mouse) turning rotates a game camera about the vertical axis by
    // the same yaw measure, keeping its position and orthonormality.
    {
        const auto turned=rotate_game_yaw(camera(.3,.2,7),.5);
        require(std::abs(game_camera_yaw(turned)-.8)<1e-12,"yaw rotation measure");
        require(turned[12]==7&&turned[13]==1.7&&turned[14]==0&&std::abs(turned[9]-camera(.3,.2)[9])<1e-12,"position or pitch changed by yaw rotation");
        for(int a=0;a<3;++a)for(int b=0;b<3;++b){double dot{};for(int r=0;r<3;++r)dot+=turned[a*4+r]*turned[b*4+r];require(std::abs(dot-(a==b))<1e-12,"rotation not orthonormal");}
        require(std::abs(game_camera_yaw(rotate_game_yaw(camera(3.),.5))-wrap_pi(3.5))<1e-12,"wrap across pi");
    }
    // Mouse turning applied by the game between frames (field changed since the
    // previous update settled) is kept and becomes a play-space turn; the
    // following frame, with the head anchor rotated by that turn, no longer
    // pulls the body back.
    for(int sign:{1,-1})for(double offset:{0.,pi}) {
        LookYawCalibration c(3);for(int i=0;i<3;++i)c.observe(1.f,camera(sign*1.+offset));
        const auto game=camera(sign*1.+offset);
        for(double head:{0.,.3}) {
            const auto headset=camera(sign*1.+offset+head,.2,4);
            const auto step=follow_headset_yaw_turning(1.4f,1.f,c,headset,game);
            require(std::abs(step.turn-sign*.4)<1e-6,"mouse turn not measured");
            const double expected=1.4+(head?sign*head:0);
            require(std::abs(wrap_pi(step.field-expected))<1e-5,"mouse turn overwritten by headset follow");
            const auto next=follow_headset_yaw_turning(step.field,step.field,c,
                rotate_game_yaw(headset,step.turn),camera(sign*step.field+offset));
            require(next.field==step.field&&next.turn==0,"body pulled back after mouse turn");
        }
        const auto plain=follow_headset_yaw_turning(1.f,std::nullopt,c,camera(sign*1.+offset+.5),game);
        require(plain.turn==0&&plain.field==follow_headset_yaw(1.f,c,camera(sign*1.+offset+.5),game),"no settled value must behave like plain follow");
    }
    for(int sign:{1,-1})for(double offset:{0.,pi}){
        LookYawCalibration c(3);for(int i=0;i<3;++i)c.observe(1.f,camera(sign*1.+offset));
        float field=1;double view=sign*1.+offset;
        for(int frame=0;frame<100;++frame){const auto step=follow_headset_yaw_turning(field,field,c,camera(view),camera(sign*field+offset),.0175,.01);
            require(std::abs(step.turn-.01)<1e-6,"joystick turn missing or duplicated");field=step.field;view+=step.turn;
            require(std::abs(wrap_pi(game_camera_yaw(camera(sign*field+offset))-view))<1e-5,"body and view diverged under smooth turning");}
        require(std::abs(wrap_pi(view-(sign*1.+offset))-1.)<1e-5,"smooth turn did not accumulate");
        const auto stopped=follow_headset_yaw_turning(field,field,c,camera(view),camera(sign*field+offset));
        require(stopped.turn==0&&stopped.field==field,"headset follow undid joystick turn");
    }
    {LookYawCalibration c;const auto s=follow_headset_yaw_turning(.9f,.5f,c,camera(2.),camera(.5));require(s.field==.9f&&s.turn==0,"unlocked calibration turned");}
    {LookYawCalibration c(3);for(int i=0;i<3;++i)c.observe(1.f,camera(1.));
        const auto first=follow_headset_yaw_turning(1.f,std::nullopt,c,camera(1.),camera(1.),.0175,.01);
        require(std::abs(first.field-1.01f)<1e-6&&std::abs(first.turn-.01)<1e-9,"small first joystick turn rotated view without rotating body");}
    // Without a locked calibration nothing changes.
    {LookYawCalibration c;require(follow_headset_yaw(.5f,c,camera(2.),camera(.5))==.5f,"unlocked calibration wrote yaw");}
    std::cout<<"PASS: body yaw mapping identification and headset following\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
