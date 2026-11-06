#include "swvr/tracking.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace swvr;
void require(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
constexpr float radians=0.017453292519943295f;
Quaternion yaw(float degrees){return {0,std::sin(degrees*radians/2),0,std::cos(degrees*radians/2)};}
Quaternion pitch(float degrees){return {std::sin(degrees*radians/2),0,0,std::cos(degrees*radians/2)};}
std::array<Pose,2> eyes(const Pose& head){return {compose(head,{{},{-.032f,0,0}}),compose(head,{{},{.032f,0,0}})};}
// Head pose whose neck pivot stays at `neck` (LOCAL), as a physical head turn does.
Pose head_about_neck(Quaternion q,Vec3 neck){
    const Pose pivot{q,neck};return compose(pivot,inverse(Pose{{},head_neck_offset}));
}
Vec3 center(const Pose& origin,const std::array<Pose,2>& e){return stereo_center(cockpit_views({},origin,e)).position;}
float length(Vec3 v){return std::hypot(v.x,v.y,v.z);}
Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
}

int main(){try{
    const Pose seated{yaw(30),{.4f,1.2f,-.3f}};
    const auto calibration=eyes(seated);
    const auto origin=gravity_aligned_origin(calibration);
    const auto neck=neck_position(origin,calibration);
    const HeadLimit limit{};

    // Calibration posture and small motion are untouched.
    auto r=limit_head(origin,neck,calibration,limit);
    require(!r.limited&&length(sub(r.origin.position,origin.position))<1e-6f,"neutral posture limited");
    auto near=seated;near.position.x+=.02f;
    r=limit_head(origin,neck,eyes(near),limit);
    require(!r.limited&&length(sub(center(r.origin,eyes(near)),center(origin,eyes(near))))<1e-6f,"small lean changed");

    // Turning/pitching the head about the neck is never treated as leaning.
    const Vec3 pivot=compose(seated,Pose{{},head_neck_offset}).position;
    for(float a:{-80.f,-45.f,45.f,80.f}){
        const auto turned=head_about_neck(yaw(30+a),pivot);
        r=limit_head(origin,neck,eyes(turned),limit);
        require(!r.limited&&length(r.requested)<1e-4f,"head turn about the neck was limited");
        const auto down=head_about_neck(compose({yaw(30),{}},{pitch(-40),{}}).orientation,pivot);
        r=limit_head(origin,neck,eyes(down),limit);
        require(!r.limited,"looking down about the neck was limited");
    }

    // Forward lean (seat forward is -Z in the calibrated frame) is compressed
    // smoothly, never beyond the limit, and keeps increasing.
    float previous_applied=0;
    for(float lean:{.1f,.2f,.3f,.5f,.8f,2.f}){
        auto leaned=seated;const auto forward=compose({origin.orientation,{}},{{},{0,0,-lean}}).position;
        leaned.position={seated.position.x+forward.x,seated.position.y+forward.y,seated.position.z+forward.z};
        r=limit_head(origin,neck,eyes(leaned),limit);
        const float applied=-r.applied.z;
        require(std::abs(-r.requested.z-lean)<1e-4f,"requested lean measured wrongly");
        // tanh saturates to exactly the limit in float for extreme leans.
        const bool ok=applied<=lean+1e-6f&&applied<=limit.forward&&(lean>1.f?applied>=previous_applied:applied>previous_applied);
        if(!ok)std::cerr<<"lean="<<lean<<" applied="<<applied<<" previous="<<previous_applied<<" extent="<<r.extent<<'\n';
        require(ok,"forward lean not monotonic/bounded");
        if(lean<=limit.forward*limit.knee)require(!r.limited&&std::abs(applied-lean)<1e-5f,"lean inside knee changed");
        else require(r.limited,"lean beyond knee not reported");
        // The whole tracking space moves together: camera centre equals the applied offset.
        const auto shifted=sub(center(r.origin,eyes(leaned)),center(origin,calibration));
        require(length(sub(shifted,r.applied))<1e-4f,"camera does not follow the limited offset");
        previous_applied=applied;
    }
    // Continuity at the knee.
    {
        const float k=limit.forward*limit.knee;auto a=seated,b=seated;
        for(auto [pose,d]:{std::pair<Pose*,float>{&a,k-1e-4f},std::pair<Pose*,float>{&b,k+1e-4f}}){
            const auto f=compose({origin.orientation,{}},{{},{0,0,-d}}).position;pose->position={seated.position.x+f.x,seated.position.y+f.y,seated.position.z+f.z};}
        const auto ra=limit_head(origin,neck,eyes(a),limit),rb=limit_head(origin,neck,eyes(b),limit);
        require(std::abs(ra.applied.z-rb.applied.z)<3e-4f,"discontinuity at the knee");
    }
    // Asymmetric axes: backward and upward limits are tighter than forward/down.
    {
        auto back=seated,up=seated;
        const auto b=compose({origin.orientation,{}},{{},{0,0,.3f}}).position;back.position={seated.position.x+b.x,seated.position.y+b.y,seated.position.z+b.z};
        up.position.y+=.3f;
        require(limit_head(origin,neck,eyes(back),limit).applied.z<limit.back,"backward limit ignored");
        require(limit_head(origin,neck,eyes(up),limit).applied.y<limit.up,"upward limit ignored");
        auto side=seated;const auto s=compose({origin.orientation,{}},{{},{-.3f,0,0}}).position;side.position={seated.position.x+s.x,seated.position.y+s.y,seated.position.z+s.z};
        const auto rs=limit_head(origin,neck,eyes(side),limit);
        require(-rs.applied.x<limit.left&&-rs.applied.x>limit.left*limit.knee,"lateral limit wrong");
    }
    // Hands share the correction, so they stay registered with the view.
    {
        auto leaned=seated;leaned.position.y-=.6f;
        r=limit_head(origin,neck,eyes(leaned),limit);
        const Pose hand{{},{seated.position.x,seated.position.y-.5f,seated.position.z-.4f}};
        const auto raw=compose(inverse(origin),hand).position,limited=compose(inverse(r.origin),hand).position;
        require(length(sub(sub(limited,raw),sub(r.applied,r.requested)))<1e-4f,"hand correction differs from head correction");
    }
    // Configuration is validated.
    for(auto bad:{HeadLimit{0},HeadLimit{1.f},HeadLimit{.6f,-.1f}}){
        bool caught{};try{(void)limit_head(origin,neck,calibration,bad);}catch(const std::invalid_argument&){caught=true;}
        require(caught,"invalid head limit accepted");
    }
    std::cout<<"PASS: neck-pivot head limit keeps turns free, compresses lean smoothly and shifts hands with the view\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
