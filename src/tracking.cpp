#include "swvr/tracking.hpp"
#include <cmath>
#include <stdexcept>

namespace swvr {
namespace {
Vec3 add(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 scale(Vec3 a, float s) { return {a.x*s, a.y*s, a.z*s}; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
Quaternion unit(Quaternion q) {
    const float norm = std::hypot(std::hypot(q.x, q.y), std::hypot(q.z, q.w));
    if (!std::isfinite(norm) || norm < 1e-6f) throw std::invalid_argument("Invalid orientation");
    return {q.x/norm, q.y/norm, q.z/norm, q.w/norm};
}
Quaternion multiply(Quaternion a, Quaternion b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
Vec3 rotate(Quaternion q, Vec3 v) {
    const Vec3 u{q.x, q.y, q.z};
    const auto t = scale(cross(u, v), 2);
    return add(v, add(scale(t, q.w), cross(u, t)));
}
}
Pose compose(const Pose& parent, const Pose& child) {
    if (!finite(parent.position) || !finite(child.position)) throw std::invalid_argument("Invalid position");
    const auto a = unit(parent.orientation), b = unit(child.orientation);
    return {unit(multiply(a,b)), add(parent.position, rotate(a,child.position))};
}
Pose inverse(const Pose& pose) {
    if (!finite(pose.position)) throw std::invalid_argument("Invalid position");
    const auto q = unit(pose.orientation);
    const Quaternion conjugate{-q.x,-q.y,-q.z,q.w};
    return {conjugate, rotate(conjugate,scale(pose.position,-1))};
}
Pose stereo_center(const std::array<Pose,2>& eyes) {
    if(!finite(eyes[0].position)||!finite(eyes[1].position))
        throw std::invalid_argument("Invalid stereo position");
    const auto a=unit(eyes[0].orientation),b=unit(eyes[1].orientation);
    const float sign=(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w)<0?-1.f:1.f;
    return {unit({a.x+sign*b.x,a.y+sign*b.y,a.z+sign*b.z,a.w+sign*b.w}),
            add(scale(eyes[0].position,.5f),scale(eyes[1].position,.5f))};
}
Matrix4 projection(Fov fov, float near_z, float far_z) {
    constexpr float half_pi = 1.57079632679f;
    for (float a : {fov.left,fov.right,fov.up,fov.down})
        if (!std::isfinite(a) || std::abs(a)>=half_pi) throw std::invalid_argument("Invalid FOV");
    if (!std::isfinite(near_z) || !std::isfinite(far_z) || near_z<=0 || far_z<=near_z)
        throw std::invalid_argument("Invalid clip planes");
    const float l=std::tan(fov.left), r=std::tan(fov.right);
    const float u=std::tan(fov.up), d=std::tan(fov.down);
    if (r<=l || u<=d) throw std::invalid_argument("Inverted FOV");
    Matrix4 m{};
    m[0]=2/(r-l); m[5]=2/(u-d);
    m[8]=(r+l)/(r-l); m[9]=(u+d)/(u-d);
    m[10]=-(far_z+near_z)/(far_z-near_z); m[11]=-1;
    m[14]=-2*far_z*near_z/(far_z-near_z);
    return m;
}
Matrix4 view_matrix(const Pose& eye) {
    const auto v=inverse(eye);
    const auto x=rotate(v.orientation,{1,0,0});
    const auto y=rotate(v.orientation,{0,1,0});
    const auto z=rotate(v.orientation,{0,0,1});
    return {x.x,x.y,x.z,0,y.x,y.y,y.z,0,z.x,z.y,z.z,0,
            v.position.x,v.position.y,v.position.z,1};
}
Pose gravity_aligned_origin(const std::array<Pose,2>& eyes) {
    auto origin=stereo_center(eyes);
    const auto rotation=view_matrix(inverse(origin));
    const auto horizontal=std::hypot(rotation[8],rotation[10]);
    // Heading from forward projected onto LOCAL's horizontal plane. When the
    // user looks exactly vertically, use the projected right axis as a finite
    // fallback; heading is inherently ambiguous in that posture.
    const float yaw=horizontal>1e-4f?std::atan2(rotation[8],rotation[10]):std::atan2(-rotation[2],rotation[0]);
    origin.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
    return origin;
}
Pose recenter_head_origin(const Pose& origin,const std::array<Pose,2>& eyes) {
    auto out=origin;out.position=stereo_center(eyes).position;
    inverse(out); // Validate the preserved orientation as well as translation.
    return out;
}
std::array<Pose,2> cockpit_views(const Pose& seat, const Pose& origin,
                               const std::array<Pose,2>& eyes) {
    const auto transform = compose(seat,inverse(origin));
    return {compose(transform,eyes[0]),compose(transform,eyes[1])};
}
Vec3 neck_position(const Pose& origin,const std::array<Pose,2>& eyes) {
    const auto head=compose(inverse(origin),stereo_center(eyes));
    return compose(head,Pose{{},head_neck_offset}).position;
}
HeadLimitResult limit_head(const Pose& origin,Vec3 calibrated_neck,
                           const std::array<Pose,2>& eyes,const HeadLimit& limit) {
    if(!std::isfinite(limit.knee) || limit.knee<=0 || limit.knee>=1)
        throw std::invalid_argument("Head limit knee must be inside (0,1)");
    for(float axis:{limit.forward,limit.back,limit.left,limit.right,limit.up,limit.down})
        if(!std::isfinite(axis) || axis<=0) throw std::invalid_argument("Head limit axes must be positive");
    if(!finite(calibrated_neck)) throw std::invalid_argument("Invalid calibrated neck");
    HeadLimitResult out;out.origin=origin;
    const auto neck=neck_position(origin,eyes);
    const Vec3 d{neck.x-calibrated_neck.x,neck.y-calibrated_neck.y,neck.z-calibrated_neck.z};
    out.requested=out.applied=d;
    const float sx=d.x/(d.x<0?limit.left:limit.right);
    const float sy=d.y/(d.y<0?limit.down:limit.up);
    const float sz=d.z/(d.z<0?limit.forward:limit.back);
    const float r=std::hypot(sx,sy,sz);
    if(!std::isfinite(r)) throw std::invalid_argument("Invalid head offset");
    out.extent=r;
    if(r<=limit.knee) return out;
    const float span=1-limit.knee;
    const float compressed=limit.knee+span*std::tanh((r-limit.knee)/span);
    out.applied=scale(d,compressed/r);out.limited=true;
    // Shift the whole tracking space by the correction: views, hands and the
    // avatar head target move together, so hands stay registered with the image.
    const Vec3 correction{out.applied.x-d.x,out.applied.y-d.y,out.applied.z-d.z};
    out.origin.position=add(origin.position,scale(rotate(unit(origin.orientation),correction),-1));
    return out;
}
PinchDetector::PinchDetector(float press, float release, std::int64_t max_age)
    : press_(press), release_(release), max_age_(max_age) {
    if (!std::isfinite(press) || !std::isfinite(release) || press <= 0 || release <= press || max_age <= 0)
        throw std::invalid_argument("Invalid pinch thresholds");
}
PinchEvent PinchDetector::reset() {
    const bool was_pinched = pinched_;
    armed_ = pinched_ = false;
    last_sample_ = -1;
    return was_pinched ? PinchEvent::cancelled : PinchEvent::none;
}
PinchEvent PinchDetector::update(const HandSample& s, std::int64_t now) {
    if (!s.active || !s.focused || !s.position_valid || !s.position_tracked ||
        !finite(s.thumb_tip) || !finite(s.index_tip) || s.time_ns < 0 || now < s.time_ns ||
        now-s.time_ns > max_age_ || s.time_ns < last_sample_) return reset();
    // Repeated samples must not generate additional gestures.
    if (s.time_ns == last_sample_) return PinchEvent::none;
    last_sample_ = s.time_ns;
    const auto delta = add(s.thumb_tip,scale(s.index_tip,-1));
    const float distance = std::hypot(delta.x,delta.y,delta.z);
    if (!std::isfinite(distance)) return reset();
    if (!armed_) {
        armed_ = distance >= release_;
        return PinchEvent::none;
    }
    if (pinched_) {
        if (distance >= release_) { pinched_ = false; return PinchEvent::released; }
        return PinchEvent::held;
    }
    if (distance <= press_) { pinched_ = true; return PinchEvent::began; }
    return PinchEvent::none;
}
}
