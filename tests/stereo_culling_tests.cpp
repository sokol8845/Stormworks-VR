#include "swvr/stereo_culling.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using Point=std::array<double,3>;
using Quat=std::array<double,4>;
void require(bool test,const char* message) {if(!test)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message) {
    bool rejected{};try {f();}catch(const std::invalid_argument&){rejected=true;}require(rejected,message);
}
Quat q(const swvr::Pose& pose) {
    Quat value{pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w};
    double length{};for(auto v:value)length+=v*v;length=std::sqrt(length);
    for(auto& v:value)v/=length;return value;
}
// Independent rotation-matrix oracle, rather than the implementation's cross products.
Point rotate(Quat a,Point p) {
    const auto x=a[0],y=a[1],z=a[2],w=a[3];
    return {(1-2*y*y-2*z*z)*p[0]+(2*x*y-2*w*z)*p[1]+(2*x*z+2*w*y)*p[2],
            (2*x*y+2*w*z)*p[0]+(1-2*x*x-2*z*z)*p[1]+(2*y*z-2*w*x)*p[2],
            (2*x*z-2*w*y)*p[0]+(2*y*z+2*w*x)*p[1]+(1-2*x*x-2*y*y)*p[2]};
}
swvr::Quaternion multiply(swvr::Quaternion a,swvr::Quaternion b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
swvr::Quaternion yaw(float angle) {return {0,std::sin(angle/2),0,std::cos(angle/2)};}
swvr::Matrix4d base(double yaw_angle=0,double pitch_angle=0,Point translation={}) {
    const double c=std::cos(yaw_angle),s=std::sin(yaw_angle),cp=std::cos(pitch_angle),sp=std::sin(pitch_angle);
    return {c,0,-s,0,s*sp,cp,c*sp,0,s*cp,-sp,c*cp,0,translation[0],translation[1],translation[2],1};
}
Point point(const swvr::Matrix4d& b,const swvr::Pose& eye,Point local) {
    auto p=rotate(q(eye),local);
    p[0]+=eye.position.x;p[1]+=eye.position.y;p[2]+=eye.position.z;
    p[2]=-p[2]; // S follows the entire eye transform.
    Point result{};
    for(int row=0;row<3;++row)result[row]=b[row]*p[0]+b[4+row]*p[1]+b[8+row]*p[2]+b[12+row];
    return result;
}
std::vector<Point> sample(const swvr::Matrix4d& b,const std::array<swvr::Pose,2>& eyes,
    const std::array<swvr::Fov,2>& fovs,double near_z,double far_z,bool corners_only,bool native_float_tangents=true) {
    std::vector<Point> result;
    const auto weights=corners_only?std::vector<double>{0,1}:std::vector<double>{0,.17,.5,.83,1};
    const auto depths=corners_only?std::vector<double>{near_z,far_z}:
        std::vector<double>{near_z,near_z+.01*(far_z-near_z),std::sqrt(near_z*far_z),.5*(near_z+far_z),far_z};
    for(unsigned i=0;i<2;++i) {
        const auto tangent=[&](float angle) {return native_float_tangents?static_cast<double>(std::tan(angle)):std::tan(static_cast<double>(angle));};
        const auto l=tangent(fovs[i].left),r=tangent(fovs[i].right);
        const auto d=tangent(fovs[i].down),u=tangent(fovs[i].up);
        for(auto depth:depths)for(auto x:weights)for(auto y:weights)
            result.push_back(point(b,eyes[i],{depth*((1-x)*l+x*r),depth*((1-y)*d+y*u),-depth}));
    }
    return result;
}
double support(const swvr::NativeCullPlane& plane,const Point& p) {
    return plane.normal[0]*p[0]+plane.normal[1]*p[1]+plane.normal[2]*p[2];
}
bool inside(const swvr::StereoCullPlanes& planes,const Point& p) {
    for(const auto& plane:planes)if(support(plane,p)<plane.distance)return false;
    return true;
}
swvr::StereoCullPlanes verify(const swvr::Matrix4d& b,const std::array<swvr::Pose,2>& eyes,
    const std::array<swvr::Fov,2>& fovs,double near_z,double far_z) {
    const auto original_base=b;
    const auto original_eyes=eyes;
    const auto planes=swvr::make_stereo_cull_planes(b,eyes,fovs,near_z,far_z);
    require(b==original_base && std::memcmp(eyes.data(),original_eyes.data(),sizeof(eyes))==0,"Culling modified inputs");
    const auto corners=sample(b,eyes,fovs,near_z,far_z,true);
    require(corners.size()==16,"Two finite frusta must have sixteen corner samples");
    for(bool float_tangents:{false,true})for(const auto& p:sample(b,eyes,fovs,near_z,far_z,false,float_tangents))
        require(inside(planes,p),"Stereo volume rejects an eye corner or interior point");
    for(const auto& plane:planes) {
        const double n2=plane.normal[0]*static_cast<double>(plane.normal[0])+
                        plane.normal[1]*static_cast<double>(plane.normal[1])+
                        plane.normal[2]*static_cast<double>(plane.normal[2]);
        require(std::abs(n2-1)<2e-7 && plane.padding==0,"Native plane layout or normalization");
        double nearest=std::numeric_limits<double>::infinity();
        for(const auto& p:corners)nearest=std::min(nearest,support(plane,p));
        const auto margin=nearest-plane.distance;
        require(margin>=0 && margin<.0001+4e-6*far_z,
                "D must use stored float normal and local-scale margin, not float world-scale inflation");
    }
    Point outside{b[12]+planes[0].normal[0]*far_z*1000,b[13]+planes[0].normal[1]*far_z*1000,
                  b[14]+planes[0].normal[2]*far_z*1000};
    require(!inside(planes,outside),"Finite stereo volume failed to reject a far outside point");
    return planes;
}
}
int main() {
    try {
        const std::array<swvr::Pose,2> eyes{{{{},{-.032f,0,0}},{{},{.032f,0,0}}}};
        const std::array<swvr::Fov,2> symmetric{{{-.7f,.7f,.8f,-.8f},{-.7f,.7f,.8f,-.8f}}};
        const auto stereo=verify(base(),eyes,symmetric,.1,50);
        require(inside(stereo,{0,0,1}) && !inside(stereo,{0,0,-1}),
                "Native camera forward must be +Z after reflecting XR -Z");
        const auto mono=swvr::make_stereo_cull_planes(base(),{eyes[0],eyes[0]},symmetric,.1,50);
        bool exposed{};
        for(const auto& p:sample(base(),eyes,symmetric,.1,50,true))
            if(!inside(mono,p) && inside(stereo,p))exposed=true;
        require(exposed,"Regression case must expose geometry omitted by monocular culling");
        const std::array<swvr::Fov,2> asymmetric{{{-.92f,.63f,.81f,-.51f},{-.60f,.97f,.72f,-.62f}}};
        auto moved=eyes;
        const swvr::Quaternion pitch{std::sin(-.15f),0,0,std::cos(-.15f)};
        const auto head=multiply(yaw(.55f),pitch);
        for(unsigned i=0;i<2;++i) {
            const auto displacement=rotate(q({head,{}}),{eyes[i].position.x,0,0});
            moved[i].position={static_cast<float>(displacement[0]+.2),static_cast<float>(displacement[1]+.1),
                               static_cast<float>(displacement[2]-.3)};
            moved[i].orientation=multiply(head,yaw(i==0?-.09f:.09f));
        }
        verify(base(.8,-.3,{17,4,-29}),moved,asymmetric,.025,20100);
        const auto distant_base=base(.8,-.3,{1e9+.125,-1e9+.25,1e9+.5});
        verify(distant_base,moved,asymmetric,.025,20100);
        const auto distant_stereo=verify(base(0,0,{1e9,1e9,1e9}),eyes,symmetric,.1,50);
        const auto distant_mono=swvr::make_stereo_cull_planes(base(0,0,{1e9,1e9,1e9}),{eyes[0],eyes[0]},symmetric,.1,50);
        require(std::abs(distant_stereo[3].distance-distant_mono[3].distance)>.03,
                "Large world coordinates erased eye separation in culling");
        // Some canted-eye corners lie behind the center orientation. Independent
        // half-spaces still contain them; a common-apex x/(-z) cone would fail.
        auto wide=eyes;wide[0].orientation=yaw(-1.2f);wide[1].orientation=yaw(1.2f);
        verify(base(),wide,symmetric,.025,80);
        bool behind{};for(const auto& p:sample(base(),wide,symmetric,.025,80,true))if(p[2]<0)behind=true;
        require(behind,"Wide-eye regression must cross the center apex plane");
        auto signs=moved;auto& right=signs[1].orientation;
        right={-right.x,-right.y,-right.z,-right.w};
        const auto signed_planes=verify(base(),signs,asymmetric,.1,100);
        const auto reference=verify(base(),moved,asymmetric,.1,100);
        for(unsigned i=0;i<6;++i)require(std::abs(signed_planes[i].distance-reference[i].distance)<1e-9 &&
                                      signed_planes[i].normal==reference[i].normal,"Quaternion sign changed volume");
        verify(base(),eyes,{{{.1f,.8f,.9f,.15f},{.2f,.9f,.8f,.1f}}},.1,50);
        for(unsigned kind=0;kind<13;++kind) {
            auto b=base();auto e=eyes;auto f=symmetric;double n=.1,z=100;
            if(kind==0)b[0]=2;
            if(kind==1)b[3]=.1;
            if(kind==2)b[12]=std::numeric_limits<double>::quiet_NaN();
            if(kind==3)e[1].orientation={0,0,0,0};
            if(kind==4)e[0].position.x=std::numeric_limits<float>::infinity();
            if(kind==5)f[0].left=f[0].right;
            if(kind==6)f[0].up=1.6f;
            if(kind==7)n=0;
            if(kind==8)z=n;
            if(kind==9)b[10]=-1;
            if(kind==10)f[1].down=std::numeric_limits<float>::quiet_NaN();
            if(kind==11)e[0].orientation.w=std::numeric_limits<float>::infinity();
            if(kind==12)z=std::numeric_limits<double>::infinity();
            rejects([&]{swvr::make_stereo_cull_planes(b,e,f,n,z);},"Invalid stereo frustum input accepted");
        }
        std::cout<<"Stereo culling tests passed: asymmetric/canted eyes, head pose, native basis, large world and conservative support.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
