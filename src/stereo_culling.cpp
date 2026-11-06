#include "swvr/stereo_culling.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace swvr {
namespace {
using DVec=std::array<double,3>;
using DQuat=std::array<double,4>;
constexpr double pi_over_two=1.57079632679489661923;
double dot(const DVec& a,const DVec& b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
DVec cross(const DVec& a,const DVec& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}
void finite(const DVec& value) {
    for(const auto component:value) if(!std::isfinite(component))
        throw std::invalid_argument("Non-finite stereo culling coordinates");
}
DQuat unit(DQuat q) {
    const auto norm=std::hypot(std::hypot(q[0],q[1]),std::hypot(q[2],q[3]));
    if(!std::isfinite(norm)||norm<1e-12) throw std::invalid_argument("Invalid stereo culling orientation");
    for(auto& value:q) value/=norm;
    return q;
}
DQuat quaternion(const Pose& pose) {
    return unit({pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w});
}
DVec rotate(const DQuat& q,const DVec& point) {
    const DVec axis{q[0],q[1],q[2]};
    const auto first=cross(axis,point),second=cross(axis,first);
    return {point[0]+2*(q[3]*first[0]+second[0]),
            point[1]+2*(q[3]*first[1]+second[1]),
            point[2]+2*(q[3]*first[2]+second[2])};
}
DVec game_vector(const Matrix4d& base,const DVec& xr) {
    // Native camera transforms look toward +Z. Apply the measured reflection
    // after the XR pose, not before the eye's rotation or translation.
    return {base[0]*xr[0]+base[4]*xr[1]-base[8]*xr[2],
            base[1]*xr[0]+base[5]*xr[1]-base[9]*xr[2],
            base[2]*xr[0]+base[6]*xr[1]-base[10]*xr[2]};
}
void validate_base(const Matrix4d& base) {
    for(double value:base) if(!std::isfinite(value))
        throw std::invalid_argument("Non-finite stereo culling base");
    if(std::abs(base[3])>1e-10 || std::abs(base[7])>1e-10 ||
       std::abs(base[11])>1e-10 || std::abs(base[15]-1)>1e-10)
        throw std::invalid_argument("Stereo culling requires an affine camera base");
    for(int a=0;a<3;++a) for(int b=0;b<3;++b) {
        double product{};
        for(int row=0;row<3;++row) product+=base[a*4+row]*base[b*4+row];
        if(std::abs(product-(a==b?1:0))>1e-5)
            throw std::invalid_argument("Stereo culling requires a rigid camera base");
    }
    const DVec x{base[0],base[1],base[2]},y{base[4],base[5],base[6]},z{base[8],base[9],base[10]};
    if(dot(cross(x,y),z)<0)
        throw std::invalid_argument("Stereo culling base must be a proper transform before the native reflection");
}
std::array<double,4> tangents(Fov fov) {
    const std::array<double,4> angles{fov.left,fov.right,fov.down,fov.up};
    for(const auto angle:angles) if(!std::isfinite(angle)||std::abs(angle)>=pi_over_two)
        throw std::invalid_argument("Invalid stereo culling FOV");
    if(angles[0]>=angles[1]||angles[2]>=angles[3])
        throw std::invalid_argument("Inverted stereo culling FOV");
    return {std::tan(angles[0]),std::tan(angles[1]),std::tan(angles[2]),std::tan(angles[3])};
}
}
StereoCullPlanes make_stereo_cull_planes(const Matrix4d& base,
    const std::array<Pose,2>& eyes,const std::array<Fov,2>& fovs,
    double near_distance,double far_distance) {
    validate_base(base);
    if(!std::isfinite(near_distance)||!std::isfinite(far_distance)||
       near_distance<=0||far_distance<=near_distance)
        throw std::invalid_argument("Invalid stereo culling clip distances");
    std::array<DQuat,2> rotations{quaternion(eyes[0]),quaternion(eyes[1])};
    const std::array<std::array<double,4>,2> lens{tangents(fovs[0]),tangents(fovs[1])};
    std::array<DVec,16> corners{};
    double local_extent=1;
    std::size_t next{};
    for(std::size_t eye=0;eye<2;++eye) {
        const DVec position{eyes[eye].position.x,eyes[eye].position.y,eyes[eye].position.z};
        finite(position);
        for(double depth:{near_distance,far_distance}) for(int y:{2,3}) for(int x:{0,1}) {
            auto point=rotate(rotations[eye],{depth*lens[eye][x],depth*lens[eye][y],-depth});
            for(int axis=0;axis<3;++axis) point[axis]+=position[axis];
            const auto extent=std::hypot(point[0],point[1],point[2]);
            if(!std::isfinite(extent)) throw std::invalid_argument("Stereo culling local extent overflow");
            local_extent=std::max(local_extent,extent);
            point=game_vector(base,point);
            for(int axis=0;axis<3;++axis) point[axis]+=base[12+axis];
            finite(point);corners[next++]=point;
        }
    }
    double orientation_dot{};
    for(int i=0;i<4;++i) orientation_dot+=rotations[0][i]*rotations[1][i];
    const double sign=orientation_dot<0?-1:1;
    DQuat center{};
    for(int i=0;i<4;++i) center[i]=rotations[0][i]+sign*rotations[1][i];
    center=unit(center);
    const double left=std::min(lens[0][0],lens[1][0]),right=std::max(lens[0][1],lens[1][1]);
    const double bottom=std::min(lens[0][2],lens[1][2]),top=std::max(lens[0][3],lens[1][3]);
    const std::array<DVec,6> local_normals{{{0,0,-1},{0,0,1},{1,0,left},
                                          {-1,0,-right},{0,1,bottom},{0,-1,-top}}};
    StereoCullPlanes result{};
    for(std::size_t i=0;i<result.size();++i) {
        auto normal=game_vector(base,rotate(center,local_normals[i]));
        const auto magnitude=std::hypot(normal[0],normal[1],normal[2]);
        if(!std::isfinite(magnitude)||magnitude<=0)
            throw std::invalid_argument("Invalid stereo culling plane normal");
        for(int axis=0;axis<3;++axis) result[i].normal[axis]=static_cast<float>(normal[axis]/magnitude);
        // Native normal quantization is part of the actual plane definition.
        // Compute support with those stored floats, never the pre-cast vector.
        const DVec stored{result[i].normal[0],result[i].normal[1],result[i].normal[2]};
        double support=std::numeric_limits<double>::infinity(),scale=1;
        for(const auto& corner:corners) {
            const auto product=dot(stored,corner);
            const auto terms=std::abs(stored[0]*corner[0])+std::abs(stored[1]*corner[1])+std::abs(stored[2]*corner[2]);
            if(!std::isfinite(product)||!std::isfinite(terms))
                throw std::invalid_argument("Stereo culling support overflow");
            support=std::min(support,product);scale=std::max(scale,terms);
        }
        // Cover double cancellation at distant coordinates plus small native
        // float pose/projection arithmetic differences at the local frustum
        // scale. Never use float epsilon * world position: that would expand a
        // distant camera by hundreds of metres. Normal quantization is already
        // included exactly in support. This is not a complete renderer error bound.
        const double margin=1e-5+64*std::numeric_limits<double>::epsilon()*scale+
                            8*std::numeric_limits<float>::epsilon()*local_extent;
        result[i].distance=std::nextafter(support-margin,-std::numeric_limits<double>::infinity());
        if(!std::isfinite(result[i].distance)) throw std::invalid_argument("Stereo culling plane overflow");
    }
    return result;
}
}
