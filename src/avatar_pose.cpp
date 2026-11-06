#include "swvr/avatar_pose.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace swvr {
namespace {
using V=std::array<double,3>;
using M=Matrix4d;
M identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
V add(V a,V b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
V sub(V a,V b){for(int i=0;i<3;++i)a[i]-=b[i];return a;}
V scale(V a,double s){for(auto& v:a)v*=s;return a;}
double dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
double length(V a){return std::sqrt(dot(a,a));}
V cross(V a,V b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
V unit(V a){const auto l=length(a);if(!std::isfinite(l)||l<1e-8)throw std::invalid_argument("Degenerate avatar direction");return scale(a,1/l);}
V position(const M& m){return {m[12],m[13],m[14]};}
void position(M& m,V p){for(int i=0;i<3;++i)m[12+i]=p[i];}
M matrix(const Matrix4& m){M r{};std::copy(m.begin(),m.end(),r.begin());return r;}
Matrix4 floats(const M& m){Matrix4 r{};for(int i=0;i<16;++i){if(!std::isfinite(m[i])||std::abs(m[i])>1e8)throw std::invalid_argument("Avatar palette overflow");r[i]=static_cast<float>(m[i]);}return r;}
M multiply(const M& a,const M& b){M r{};for(int c=0;c<4;++c)for(int row=0;row<4;++row)for(int k=0;k<4;++k)r[c*4+row]+=a[k*4+row]*b[c*4+k];return r;}
V vector(const M& m,V v){return {m[0]*v[0]+m[4]*v[1]+m[8]*v[2],m[1]*v[0]+m[5]*v[1]+m[9]*v[2],m[2]*v[0]+m[6]*v[1]+m[10]*v[2]};}
V point(const M& m,V v){return add(vector(m,v),position(m));}
double affine_scale(const M& m) {
    for(auto v:m)if(!std::isfinite(v))throw std::invalid_argument("Nonfinite avatar transform");
    if(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>1e-5)throw std::invalid_argument("Nonaffine avatar transform");
    const double s=length({m[0],m[1],m[2]});
    if(s<.01||s>100)throw std::invalid_argument("Invalid avatar scale");
    for(int a=0;a<3;++a)for(int b=0;b<3;++b){double d{};for(int row=0;row<3;++row)d+=m[a*4+row]*m[b*4+row];if(std::abs(d-(a==b?s*s:0))>s*s*1e-3)throw std::invalid_argument("Sheared avatar transform");}
    return s;
}
M inverse_affine(const M& m) {
    const auto s=affine_scale(m);auto r=identity();
    for(int c=0;c<3;++c)for(int row=0;row<3;++row)r[c*4+row]=m[row*4+c]/(s*s);
    position(r,scale(vector(r,position(m)),-1));return r;
}
M rotation(M m){const auto s=affine_scale(m);for(int c=0;c<3;++c)for(int row=0;row<3;++row)m[c*4+row]/=s;position(m,{0,0,0});return m;}
M rotate_between(V from,V to) {
    from=unit(from);to=unit(to);double d=std::clamp(dot(from,to),-1.,1.);auto r=identity();
    if(d>1-1e-9)return r;
    V axis=cross(from,to);
    if(d< -1+1e-7){axis=unit(cross(from,std::abs(from[0])<.8?V{1,0,0}:V{0,1,0}));d=-1;}
    else axis=unit(axis);
    const double c=d,s=std::sqrt(std::max(0.,1-d*d)),t=1-c;
    for(int col=0;col<3;++col)for(int row=0;row<3;++row)r[col*4+row]=t*axis[row]*axis[col]+(row==col?c:0);
    r[4]-=s*axis[2];r[8]+=s*axis[1];r[1]+=s*axis[2];r[9]-=s*axis[0];r[2]-=s*axis[1];r[6]+=s*axis[0];return r;
}
M moved_bone(const M& source,V old_a,V old_b,V new_a,V new_b) {
    auto result=multiply(rotate_between(sub(old_b,old_a),sub(new_b,new_a)),source);position(result,new_a);return result;
}
struct Arm {V elbow{},wrist{};bool limited{};};
Arm solve_arm(V shoulder,V elbow,V wrist,V target,V pole) {
    const double upper=length(sub(elbow,shoulder)),lower=length(sub(wrist,elbow));
    if(upper<1e-4||lower<1e-4)throw std::invalid_argument("Degenerate avatar arm");
    auto difference=sub(target,shoulder);const double requested=length(difference);
    const auto direction=requested>1e-7?scale(difference,1/requested):unit(sub(wrist,shoulder));
    const double lo=std::abs(upper-lower)+1e-5,hi=upper+lower-1e-5;
    const double distance=std::clamp(requested,lo,hi);
    auto bend=sub(pole,scale(direction,dot(pole,direction)));
    if(length(bend)<1e-5)bend=sub(sub(elbow,shoulder),scale(direction,dot(sub(elbow,shoulder),direction)));
    if(length(bend)<1e-5)bend=cross(direction,std::abs(direction[1])<.9?V{0,1,0}:V{1,0,0});
    bend=unit(bend);
    const double along=(upper*upper+distance*distance-lower*lower)/(2*distance);
    const double height=std::sqrt(std::max(0.,upper*upper-along*along));
    return {add(add(shoulder,scale(direction,along)),scale(bend,height)),add(shoulder,scale(direction,distance)),std::abs(distance-requested)>1e-4};
}
template<class T>T read(std::span<const unsigned char> b,std::size_t& at){if(at>b.size()||sizeof(T)>b.size()-at)throw std::invalid_argument("Truncated avatar rig");T v{};std::memcpy(&v,b.data()+at,sizeof(T));at+=sizeof(T);return v;}
constexpr std::array<const char*,avatar_bone_count> names{"root","stomach","chest","head","right_shoulder","right_elbow","right_hand","right_fingers","right_fingers_2","left_shoulder","left_elbow","left_hand","left_fingers","right_hip","right_knee","right_foot","left_hip","left_knee","left_foot"};
constexpr std::array<int,avatar_bone_count> parents{-1,0,1,2,2,4,5,6,7,2,9,10,11,0,13,14,0,16,17};
bool joint_valid(const XrTrackedJoint& j){return j.position_valid&&j.position_tracked&&std::isfinite(j.pose.position.x)&&std::isfinite(j.pose.position.y)&&std::isfinite(j.pose.position.z);}
M native_joint(const Matrix4d& anchor,const Pose& origin,const XrTrackedJoint& joint){return offset_game_camera(anchor,compose(inverse(origin),Pose{{},joint.pose.position}));}
}
AvatarRig read_avatar_rig(std::span<const unsigned char> anim) {
    if(anim.size()<16||anim.size()>16*1024*1024||std::memcmp(anim.data(),"anim\1\0\0\0",8))throw std::invalid_argument("Unsupported survivor animation");
    std::optional<AvatarRig> found;
    for(std::size_t start=12;start+6<anim.size();++start) {
        if(std::memcmp(anim.data()+start,"\4\0root",6))continue;
        try {
            auto at=start-4;if(read<std::uint32_t>(anim,at)!=avatar_bone_count)continue;
            AvatarRig rig{};std::array<std::vector<unsigned>,avatar_bone_count> children;
            for(std::size_t i=0;i<avatar_bone_count;++i){
                const auto n=read<std::uint16_t>(anim,at);const std::string expected=names[i];
                if(n!=expected.size()||at+n>anim.size()||std::memcmp(anim.data()+at,expected.data(),n))throw std::invalid_argument("Unknown survivor hierarchy");at+=n;
                auto local=identity();for(int k=0;k<9;++k)local[(k/3)*4+k%3]=read<float>(anim,at);
                for(int k=0;k<3;++k)local[12+k]=read<float>(anim,at);
                if(std::abs(affine_scale(local)-1)>1e-4)throw std::invalid_argument("Nonrigid survivor bind pose");
                rig.parent[i]=read<std::int32_t>(anim,at);if(rig.parent[i]!=parents[i])throw std::invalid_argument("Survivor parent mismatch");
                const auto count=read<std::uint32_t>(anim,at);if(count>avatar_bone_count)throw std::invalid_argument("Invalid survivor children");
                for(unsigned c=0;c<count;++c)children[i].push_back(read<std::uint32_t>(anim,at));
                rig.bind[i]=rig.parent[i]<0?local:multiply(rig.bind[static_cast<std::size_t>(rig.parent[i])],local);
                rig.inverse_bind[i]=inverse_affine(rig.bind[i]);
            }
            for(std::size_t i=0;i<avatar_bone_count;++i){std::vector<unsigned> expected;for(std::size_t c=0;c<avatar_bone_count;++c)if(parents[c]==static_cast<int>(i))expected.push_back(static_cast<unsigned>(c));std::sort(children[i].begin(),children[i].end());if(children[i]!=expected)throw std::invalid_argument("Survivor child mismatch");}
            if(found)throw std::runtime_error("Ambiguous survivor skeleton");found=rig;
        }catch(const std::invalid_argument&){continue;}
    }
    if(!found)throw std::invalid_argument("No verified survivor skeleton in animation");return *found;
}
AvatarTargets make_avatar_targets(const XrRuntimeFrame& frame,const Pose& origin,const Matrix4d& anchor) {
    AvatarTargets out;out.frame=frame.id;out.space_revision=frame.space_revision;out.reference=anchor;
    if(!frame.id||!frame.running||frame.terminal||!frame.should_render||!frame.focused||!frame.position_valid||!frame.orientation_valid||!frame.position_tracked||!frame.orientation_tracked)return out;
    try {
        const auto center=stereo_center({frame.eyes[0].pose,frame.eyes[1].pose});
        out.head=offset_game_camera(anchor,compose(inverse(origin),center));out.valid=true;
        for(unsigned hand=0;hand<2;++hand){
            const auto& h=frame.hands[hand];if(!h.active)continue;
            if(!joint_valid(h.joints[1])||!joint_valid(h.joints[7])||!joint_valid(h.joints[12])||!joint_valid(h.joints[22]))continue;
            try {
                auto wrist=native_joint(anchor,origin,h.joints[1]);
                const auto index=position(native_joint(anchor,origin,h.joints[7])),middle=position(native_joint(anchor,origin,h.joints[12])),little=position(native_joint(anchor,origin,h.joints[22]));
                const auto finger=unit(sub(middle,position(wrist))),across=sub(index,little);
                const auto x=scale(finger,hand==1?-1.:1.);
                const auto y=unit(hand==1?cross(across,finger):cross(finger,across));const auto z=unit(cross(x,y));
                for(int k=0;k<3;++k){wrist[k]=x[k];wrist[4+k]=y[k];wrist[8+k]=z[k];}out.hands[hand]=wrist;
                double sum{};unsigned samples{};
                for(unsigned base:{7u,12u,17u,22u})if(joint_valid(h.joints[base])&&joint_valid(h.joints[base+1])&&joint_valid(h.joints[base+2])){
                    const auto a=h.joints[base].pose.position,b=h.joints[base+1].pose.position,c=h.joints[base+2].pose.position;
                    try {const auto u=unit(V{b.x-a.x,b.y-a.y,b.z-a.z}),v=unit(V{c.x-b.x,c.y-b.y,c.z-b.z});sum+=std::acos(std::clamp(dot(u,v),-1.,1.));++samples;}catch(const std::invalid_argument&){}
                }
                out.curl[hand]=samples?static_cast<float>(std::clamp(sum/samples,0.,1.3)):0;
            }catch(const std::exception&){out.hands[hand].reset();}
        }
    }catch(const std::exception&){out.valid=false;}
    return out;
}
namespace {
struct TPoseMeasure {V left{},right{},center{};double span{};};
std::optional<TPoseMeasure> measure_t_pose(const AvatarTargets& t) {
    const auto inv=inverse_affine(rotation(t.reference));
    TPoseMeasure m;
    m.left=vector(inv,sub(position(*t.hands[0]),position(t.head)));
    m.right=vector(inv,sub(position(*t.hands[1]),position(t.head)));
    // The player may face a different direction from the initial XR origin.
    // Measure in the horizontal shoulder line, not that old heading. A head
    // turn alone also must not require moving the wrists to a new depth.
    const auto line=sub(m.left,m.right);const double horizontal=std::hypot(line[0],line[2]);
    if(!std::isfinite(horizontal)||horizontal<.01)return std::nullopt;
    const double x=line[0]/horizontal,z=line[2]/horizontal;
    for(auto* p:{&m.left,&m.right}){const auto v=*p;(*p)[0]=v[0]*x+v[2]*z;(*p)[2]=-v[0]*z+v[2]*x;}
    m.span=length(sub(m.left,m.right));m.center=scale(add(m.left,m.right),.5);
    if(!std::isfinite(m.span))return std::nullopt;
    return m;
}
}
const char* avatar_body_calibration_issue(const AvatarTargets& t,double min_span) noexcept {
    if(!t.valid||!t.actor)return "tracking_not_ready";
    if(!t.hands[0]||!t.hands[1])return "hand_not_tracked";
    try {
        const auto m=measure_t_pose(t);
        if(!m)return "invalid_transform";
        if(m->span<min_span)return "arms_not_spread";
        if(m->span>2.4)return "hands_too_far_apart";
        if(std::abs(m->left[1]-m->right[1])>.1)return "hands_uneven_height";
        if(std::abs(m->left[0]-m->right[0])<m->span*.95)return "arms_not_sideways";
        if(m->center[1]>-.12)return "hands_too_high";
        if(m->center[1]<-.65)return "hands_too_low";
        if(std::abs(m->center[0])>.15)return "hands_off_centre";
        if(std::abs(m->center[2])>.35)return "hands_too_far_forward";
        return nullptr;
    }catch(const std::exception&){return "invalid_transform";}
}
std::optional<AvatarBodyCalibration> calibrate_avatar_body(const AvatarTargets& t,std::uint64_t generation,AvatarVariant variant) noexcept {
    if(!generation||avatar_body_calibration_issue(t))return {};
    try {
        const auto m=measure_t_pose(t);if(!m)return {};
        return AvatarBodyCalibration{t.actor,t.space_revision,generation,variant,m->span,m->center};
    }catch(const std::exception&){return {};}
}
AvatarTargets rebase_avatar_targets(AvatarTargets t,V shift){for(auto* m:{&t.reference,&t.head})position(*m,add(position(*m),shift));for(auto& h:t.hands)if(h)position(*h,add(position(*h),shift));return t;}
void AvatarRetargeter::reset() noexcept {calibrated_=false;}
AvatarPoseResult AvatarRetargeter::pose(const AvatarRig& rig,AvatarVariant variant,const AvatarTargets& targets,const Matrix4& world_f,std::span<const Matrix4> current,std::span<const Matrix4> previous) noexcept {
    AvatarPoseResult out;
    if(!targets.valid||!targets.frame||!targets.actor){reset();return out;}
    if(current.size()!=avatar_bone_count||previous.size()!=avatar_bone_count){out.status=AvatarPoseStatus::invalid_palette;return out;}
    try {
        const auto world=matrix(world_f),inv_world=inverse_affine(world);const auto world_scale=affine_scale(world);
        std::array<M,avatar_bone_count> original{},posed{};
        for(std::size_t i=0;i<avatar_bone_count;++i){
            const auto skin=matrix(current[i]);if(std::abs(affine_scale(skin)-1)>1e-3)throw std::invalid_argument("Nonrigid bone palette");
            original[i]=multiply(world,multiply(skin,rig.bind[i]));posed[i]=original[i];
            (void)affine_scale(matrix(previous[i]));out.current[i]=current[i];out.previous[i]=previous[i];
        }
        const auto reference=rotation(targets.reference),head_rotation=rotation(original[3]);
        out.head_distance=length(sub(position(original[3]),position(targets.reference)));
        if(out.head_distance>.55||length(sub(position(targets.head),position(targets.reference)))>1.5){out.status=AvatarPoseStatus::head_anchor_mismatch;reset();return out;}
        if(!calibrated_||actor_!=targets.actor||revision_!=targets.space_revision||variant_!=variant||std::abs(scale_-world_scale)>1e-5){
            mount_=multiply(inverse_affine(reference),head_rotation);
            eye_offset_=vector(inverse_affine(head_rotation),sub(position(targets.reference),position(original[3])));
            calibrated_=true;actor_=targets.actor;revision_=targets.space_revision;variant_=variant;scale_=world_scale;
        }
        auto head=multiply(rotation(targets.head),mount_);
        const auto head_position=sub(position(targets.head),vector(head,eye_offset_));
        const auto delta=sub(head_position,position(original[3]));
        // Seated upper-body lean. Hips/legs retain native animation. Smooth
        // skin weights bridge the lower stomach/chest translation difference.
        for(unsigned i=1;i<=12;++i)position(posed[i],add(position(posed[i]),scale(delta,i==1?.35:1.)));
        for(int c=0;c<3;++c)for(int r=0;r<3;++r)head[c*4+r]*=world_scale;
        position(head,head_position);posed[3]=head;
        if(targets.body&&targets.body->actor==targets.actor&&targets.body->variant==variant&&targets.body->generation){
            const auto& body=*targets.body;
            const auto native_span=length(sub(position(rig.bind[11]),position(rig.bind[6])))*world_scale;
            const auto center=scale(add(position(posed[9]),position(posed[4])),.5);
            // Only span (scale) and shoulder height come from the T-pose. The
            // forward/sideways placement stays the model's own: a T-pose held
            // slightly forward (to keep the hands in view) must not push the
            // whole upper body forward.
            const auto native_offset=vector(inverse_affine(reference),sub(center,position(targets.head)));
            // Arms angled forward shorten the wrist-to-wrist distance: each arm
            // spans sqrt(half_span^2 + forward^2) from the shoulder line.
            const double forward=body.shoulder_offset[2]-native_offset[2];
            const double arm_span=2*std::sqrt(body.wrist_span*body.wrist_span/4+forward*forward);
            const auto factor=arm_span/native_span;
            if(std::isfinite(factor)&&factor>=.65&&factor<=1.75){
                const V shoulder_offset{native_offset[0],body.shoulder_offset[1],native_offset[2]};
                const auto destination=add(position(targets.head),vector(reference,shoulder_offset));
                // Fixed proportions from one explicit calibration, never from
                // the current wrist reach. Head mount and legs stay unchanged.
                for(unsigned i=4;i<=12;++i)position(posed[i],add(destination,scale(sub(position(posed[i]),center),factor)));
                const auto torso_shift=sub(destination,center);
                position(posed[2],add(position(posed[2]),torso_shift));
                position(posed[1],add(position(posed[1]),scale(torso_shift,.35)));
                out.body_scale=factor;out.body_calibrated=true;
            }
        }
        for(unsigned hand=0;hand<2;++hand){
            if(!targets.hands[hand])continue;
            const unsigned shoulder=hand==0?9:4,elbow=shoulder+1,wrist=shoulder+2;
            const auto s=position(posed[shoulder]),e=position(posed[elbow]),w=position(posed[wrist]);
            const auto pole=vector(reference,{hand==0?.35:-.35,-1.,-.3});
            const auto arm=solve_arm(s,e,w,position(*targets.hands[hand]),pole);
            posed[shoulder]=moved_bone(posed[shoulder],s,e,s,arm.elbow);
            posed[elbow]=moved_bone(posed[elbow],e,w,arm.elbow,arm.wrist);
            auto target=*targets.hands[hand];for(int c=0;c<3;++c)for(int r=0;r<3;++r)target[c*4+r]*=world_scale;position(target,arm.wrist);posed[wrist]=target;
            const auto wrist_delta=multiply(target,inverse_affine(original[wrist]));
            const unsigned last=hand==0?12:8;
            for(unsigned finger=wrist+1;finger<=last;++finger)posed[finger]=multiply(wrist_delta,original[finger]);
            for(unsigned finger=wrist+1;finger<=last;++finger){
                auto curl=identity();const double angle=targets.curl[hand]*(hand==0?-1.:1.);curl[0]=curl[5]=std::cos(angle);curl[1]=std::sin(angle);curl[4]=-std::sin(angle);
                const auto at=position(posed[finger]);auto rotated=multiply(posed[finger],curl);position(rotated,at);
                const auto change=multiply(rotated,inverse_affine(posed[finger]));posed[finger]=rotated;
                for(unsigned child=finger+1;child<=last;++child)posed[child]=multiply(change,posed[child]);
            }
            out.hands[hand]=true;out.reach_limited[hand]=arm.limited;
            out.wrist_shift[hand]=length(sub(position(posed[wrist]),position(original[wrist])));
        }
        for(unsigned i=1;i<=12;++i){
            out.current[i]=floats(multiply(inv_world,multiply(posed[i],rig.inverse_bind[i])));
            // Both endpoints get the same predicted display pose for affected
            // bones, avoiding interpolation with an untracked native hand/head.
            out.previous[i]=out.current[i];
        }
        out.status=AvatarPoseStatus::applied;return out;
    }catch(const std::exception&){out.status=AvatarPoseStatus::invalid_transform;reset();return out;}
}
const char* avatar_pose_status_name(AvatarPoseStatus s) noexcept {
    switch(s){case AvatarPoseStatus::applied:return "applied";case AvatarPoseStatus::inactive:return "inactive";case AvatarPoseStatus::invalid_palette:return "invalid_palette";case AvatarPoseStatus::invalid_transform:return "invalid_transform";case AvatarPoseStatus::head_anchor_mismatch:return "head_anchor_mismatch";}return "unknown";
}
}
