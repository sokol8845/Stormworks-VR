#include "swvr/hand_interaction.hpp"
#include <cmath>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace swvr {
namespace {
// Verified against the vendored XR_HAND_JOINT_SET_DEFAULT_EXT enum:
// INDEX_PROXIMAL=7, INDEX_TIP=10, THUMB_TIP=5. No orientation flags are needed
// for this position-derived ray; all three positions must be valid and tracked.
static_assert(xr_index_proximal==7 && xr_index_tip==10 && xr_thumb_tip==5);
using D3=std::array<double,3>;
using D4=std::array<double,4>;
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool good_joint(const XrTrackedJoint& j){return j.position_valid&&j.position_tracked&&finite(j.pose.position);}
D3 cross(const D3& a,const D3& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
D3 rotate(const D4& q,const D3& p) {
    const D3 axis{q[0],q[1],q[2]};auto t=cross(axis,p);
    for(auto& v:t)v*=2;
    const auto second=cross(axis,t);
    return {p[0]+q[3]*t[0]+second[0],p[1]+q[3]*t[1]+second[1],p[2]+q[3]*t[2]+second[2]};
}
bool inverse_orientation(const Pose& origin,D4& q) {
    if(!finite(origin.position))return false;
    const auto& p=origin.orientation;
    const double norm=std::hypot(std::hypot(static_cast<double>(p.x),p.y),std::hypot(static_cast<double>(p.z),p.w));
    if(!std::isfinite(norm)||norm<1e-6)return false;
    q={-p.x/norm,-p.y/norm,-p.z/norm,p.w/norm};return true;
}
bool same_calibration(const HandCalibration& a,const HandCalibration& b) {
    const auto& x=a.local_origin;const auto& y=b.local_origin;
    return a.space_revision==b.space_revision && x.position.x==y.position.x && x.position.y==y.position.y &&
        x.position.z==y.position.z && x.orientation.x==y.orientation.x && x.orientation.y==y.orientation.y &&
        x.orientation.z==y.orientation.z && x.orientation.w==y.orientation.w;
}
bool rigid(const Matrix4d& m) {
    for(auto v:m)if(!std::isfinite(v))return false;
    if(std::abs(m[3])>1e-8||std::abs(m[7])>1e-8||std::abs(m[11])>1e-8||std::abs(m[15]-1)>1e-8)return false;
    for(int a=0;a<3;++a)for(int b=0;b<3;++b) {
        double dot{};for(int row=0;row<3;++row)dot+=m[a*4+row]*m[b*4+row];
        if(std::abs(dot-(a==b?1.:0.))>1e-4)return false;
    }
    return true;
}
D3 world_vector(const Matrix4d& t,D3 p,bool position) {
    p[2]=-p[2]; // Measured native convention: V*T=S, S=diag(1,1,-1,1).
    D3 out{};
    for(int row=0;row<3;++row)out[row]=t[row]*p[0]+t[4+row]*p[1]+t[8+row]*p[2]+(position?t[12+row]:0.);
    return out;
}
bool finite(const D3& p){return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);}
// Head gaze: stereo centre of the eyes, OpenXR view direction (-Z), mapped like the hand ray.
std::optional<WorldHandRay> gaze_ray(const XrRuntimeFrame& frame,const HandCalibration& calibration,const D4& inverse_rotation,const Matrix4d& base) {
    try {
        const auto centre=stereo_center({frame.eyes[0].pose,frame.eyes[1].pose});
        const auto& q=centre.orientation;
        const double norm=std::hypot(std::hypot(static_cast<double>(q.x),q.y),std::hypot(static_cast<double>(q.z),q.w));
        if(!std::isfinite(norm)||norm<1e-6)return std::nullopt;
        const D4 head{q.x/norm,q.y/norm,q.z/norm,q.w/norm};
        const auto forward=rotate(head,D3{0,0,-1});
        const auto& zero=calibration.local_origin.position;
        const D3 position{static_cast<double>(centre.position.x)-zero.x,static_cast<double>(centre.position.y)-zero.y,static_cast<double>(centre.position.z)-zero.z};
        WorldHandRay ray{world_vector(base,rotate(inverse_rotation,position),true),world_vector(base,rotate(inverse_rotation,forward),false)};
        const double length=std::hypot(ray.direction[0],ray.direction[1],ray.direction[2]);
        if(!finite(ray.origin)||!finite(ray.direction)||!std::isfinite(length)||length<1e-6)return std::nullopt;
        for(auto& v:ray.direction)v/=length;
        return ray;
    } catch(const std::exception&) {return std::nullopt;}
}
}
struct HandInteractionBridge::Impl {
    using Clock=HandInteractionClock;
    HandInteractionOptions options;
    InteractionHand preferred;
    mutable std::mutex mutex;
    HandInteractionSnapshot latest;
    std::array<PinchDetector,2> detectors;
    bool source_pinched{},consumer_pinched{};
    std::uint64_t sequence{},cancel_generation{},consumed_cancel_generation{},consumed_sequence{},invalidated_sequence{};
    std::uint64_t gesture_id{},consumer_gesture_id{},suppressed_gesture_id{};
    HandInteractionStatus invalidated_status{HandInteractionStatus::no_sample};
    std::optional<std::int64_t> last_xr_time;
    std::optional<HandCalibration> previous_calibration;
    std::optional<Clock::time_point> publish_clock_high_water;
    // Gaze computed for the frame being published; attached by store().
    std::optional<WorldHandRay> pending_gaze;
    explicit Impl(HandInteractionOptions o):options(o),preferred(o.hand),detectors{PinchDetector(o.pinch_press_metres,o.pinch_release_metres),PinchDetector(o.pinch_press_metres,o.pinch_release_metres)} {
        if((o.hand!=InteractionHand::left && o.hand!=InteractionHand::right)||o.max_age<=Clock::duration::zero()||
           !std::isfinite(o.minimum_index_length_metres)||o.minimum_index_length_metres<=0)
            throw std::invalid_argument("Invalid hand interaction options");
        latest.hand=o.hand;
    }
    void cancel_source() {
        for(auto& d:detectors)d.reset();source_pinched=false;++cancel_generation;
    }
    void store(const XrRuntimeFrame& frame,Clock::time_point now,HandInteractionStatus status,const WorldHandRay& ray={}) {
        latest={};latest.sequence=++sequence;latest.xr_frame_id=frame.id;latest.space_revision=frame.space_revision;
        latest.cancellation_generation=cancel_generation;latest.xr_time_ns=frame.predicted_display_time_ns;
        latest.published_at=now;latest.hand=options.hand;latest.status=status;
        latest.valid_ray=status==HandInteractionStatus::ready;
        latest.pinch_gesture_id=gesture_id;latest.pinch_down=latest.valid_ray && source_pinched;
        if(latest.valid_ray)latest.ray=ray;
        if(pending_gaze){latest.valid_gaze=true;latest.gaze=*pending_gaze;}
        invalidated_sequence=0;
    }
    void fail(const XrRuntimeFrame& frame,Clock::time_point now,HandInteractionStatus status) {
        cancel_source();store(frame,now,status);
    }
    HandInteractionStatus freshness(Clock::time_point now) const {
        if(!latest.sequence)return HandInteractionStatus::no_sample;
        if(invalidated_sequence==latest.sequence)return invalidated_status;
        if(latest.status!=HandInteractionStatus::ready)return latest.status;
        if(now<latest.published_at)return HandInteractionStatus::clock_reversal;
        if(now-latest.published_at>options.max_age)return HandInteractionStatus::stale;
        return HandInteractionStatus::ready;
    }
    HandInteractionSnapshot diagnostic(Clock::time_point now) const {
        auto out=latest;out.pinch=PinchEvent::none;out.status=freshness(now);out.cancellation_generation=cancel_generation;
        out.valid_ray=out.status==HandInteractionStatus::ready;
        if(!out.valid_ray){out.ray={};out.pinch_down=false;}
        out.valid_gaze=latest.valid_gaze && invalidated_sequence!=latest.sequence && now>=latest.published_at && now-latest.published_at<=options.max_age;
        if(!out.valid_gaze)out.gaze={};
        return out;
    }
    void publish(const XrRuntimeFrame& frame,const HandCalibration& calibration,const Pose& view_origin,const Matrix4d& base,Clock::time_point now) {
        if(publish_clock_high_water && now<*publish_clock_high_water) {
            fail(frame,now,HandInteractionStatus::clock_reversal);return;
        }
        publish_clock_high_water=now;pending_gaze.reset();
        if(!frame.id || !frame.running || frame.terminal || !frame.should_render) {
            fail(frame,now,HandInteractionStatus::inactive_frame);return;
        }
        if(frame.predicted_display_time_ns<0 || (last_xr_time && frame.predicted_display_time_ns<*last_xr_time)) {
            fail(frame,now,HandInteractionStatus::out_of_order);return;
        }
        const bool duplicate=last_xr_time && frame.predicted_display_time_ns==*last_xr_time;
        last_xr_time=frame.predicted_display_time_ns;
        if(!frame.focused){fail(frame,now,HandInteractionStatus::unfocused);return;}
        if(!frame.position_valid||!frame.orientation_valid||!frame.position_tracked||!frame.orientation_tracked) {
            fail(frame,now,HandInteractionStatus::invalid_head);return;
        }
        if(frame.space_revision!=calibration.space_revision) {
            fail(frame,now,HandInteractionStatus::calibration_mismatch);return;
        }
        D4 inverse_rotation{};
        D4 calibration_rotation{};
        if(!inverse_orientation(calibration.local_origin,calibration_rotation)||!inverse_orientation(view_origin,inverse_rotation)) {
            fail(frame,now,HandInteractionStatus::invalid_calibration);return;
        }
        const bool changed=previous_calibration && !same_calibration(*previous_calibration,calibration);
        if(!rigid(base)){fail(frame,now,HandInteractionStatus::invalid_game_transform);return;}
        pending_gaze=gaze_ray(frame,HandCalibration{view_origin,calibration.space_revision},inverse_rotation,base);
        // Duplicates may neither extend freshness nor replay/change hand choice.
        if(duplicate && !changed) {
            if(latest.status==HandInteractionStatus::ready && freshness(now)!=HandInteractionStatus::ready)
                fail(frame,now,freshness(now));
            return;
        }
        if(changed || (latest.sequence && (now-latest.published_at>options.max_age || invalidated_sequence==latest.sequence)))cancel_source();
        previous_calibration=calibration;
        std::array<PinchEvent,2> events{};
        std::array<HandInteractionStatus,2> availability{};
        std::array<D3,2> directions{};
        for(unsigned h=0;h<2;++h) {
            const auto& tracked=frame.hands[h];
            availability[h]=HandInteractionStatus::invalid_hand;
            if(tracked.active&&good_joint(tracked.joints[xr_thumb_tip])&&good_joint(tracked.joints[xr_index_tip])&&good_joint(tracked.joints[xr_index_proximal])){
                const auto& p=tracked.joints[xr_index_tip].pose.position;const auto& start=tracked.joints[xr_index_proximal].pose.position;
                directions[h]={static_cast<double>(p.x)-start.x,static_cast<double>(p.y)-start.y,static_cast<double>(p.z)-start.z};
                const auto length=std::hypot(directions[h][0],directions[h][1],directions[h][2]);
                availability[h]=std::isfinite(length)&&length>=options.minimum_index_length_metres?HandInteractionStatus::ready:HandInteractionStatus::degenerate_direction;
                if(availability[h]==HandInteractionStatus::ready)for(auto& v:directions[h])v/=length;
            }
            const bool valid=availability[h]==HandInteractionStatus::ready;
            events[h]=detectors[h].update({valid,valid,valid,true,frame.predicted_display_time_ns,
                tracked.joints[xr_thumb_tip].pose.position,tracked.joints[xr_index_tip].pose.position},frame.predicted_display_time_ns);
        }
        const auto old=static_cast<unsigned>(options.hand);
        if(options.both_hands&&!source_pinched&&(!consumer_pinched||consumed_cancel_generation!=cancel_generation)) {
            const auto first=static_cast<unsigned>(preferred),second=1u-first;
            auto chosen=events[first]==PinchEvent::began?first:events[second]==PinchEvent::began?second:
                availability[first]==HandInteractionStatus::ready?first:availability[second]==HandInteractionStatus::ready?second:old;
            if(chosen!=old&&events[chosen]==PinchEvent::held)chosen=old;
            if(chosen!=old){options.hand=static_cast<InteractionHand>(chosen);++cancel_generation;}
        }
        const auto selected=static_cast<unsigned>(options.hand);
        if(availability[selected]!=HandInteractionStatus::ready) {
            fail(frame,now,availability[selected]);return;
        }
        const auto& p=frame.hands[selected].joints[xr_index_tip].pose.position;
        const auto& direction=directions[selected];
        const auto& zero=view_origin.position;
        D3 position{static_cast<double>(p.x)-zero.x,static_cast<double>(p.y)-zero.y,static_cast<double>(p.z)-zero.z};
        WorldHandRay ray{world_vector(base,rotate(inverse_rotation,position),true),
                         world_vector(base,rotate(inverse_rotation,direction),false)};
        const double world_length=std::hypot(ray.direction[0],ray.direction[1],ray.direction[2]);
        if(!finite(ray.origin)||!finite(ray.direction)||!std::isfinite(world_length)||world_length<1e-6) {
            fail(frame,now,HandInteractionStatus::invalid_game_transform);return;
        }
        for(auto& v:ray.direction)v/=world_length;
        // Age is checked with steady_clock separately. These two equal values
        // remain exclusively in the XR time domain; no cross-clock subtraction.
        const auto event=events[static_cast<unsigned>(options.hand)];
        if(event==PinchEvent::began)++gesture_id;
        if(event==PinchEvent::began || event==PinchEvent::held)source_pinched=true;
        if(event==PinchEvent::released || event==PinchEvent::cancelled)source_pinched=false;
        store(frame,now,HandInteractionStatus::ready,ray);
    }
};
HandInteractionBridge::HandInteractionBridge(HandInteractionOptions options):impl_(std::make_unique<Impl>(options)){}
HandInteractionBridge::~HandInteractionBridge()=default;
void HandInteractionBridge::publish(const XrRuntimeFrame& frame,const HandCalibration& calibration,
                                    const Matrix4d& base,Clock::time_point now) {
    publish(frame,calibration,calibration.local_origin,base,now);
}
void HandInteractionBridge::publish(const XrRuntimeFrame& frame,const HandCalibration& calibration,
                                    const Pose& view_origin,const Matrix4d& base,Clock::time_point now) {
    std::lock_guard lock(impl_->mutex);impl_->publish(frame,calibration,view_origin,base,now);
}
void HandInteractionBridge::publish(const XrRuntimeFrame& frame,const Pose& origin,const Matrix4d& base,Clock::time_point now) {
    publish(frame,HandCalibration{origin,frame.space_revision},base,now);
}
HandInteractionSnapshot HandInteractionBridge::inspect(Clock::time_point now) const {
    std::lock_guard lock(impl_->mutex);
    if(now==Clock::time_point::max())now=Clock::now();
    return impl_->diagnostic(now);
}
HandInteractionSnapshot HandInteractionBridge::consume(Clock::time_point now) {
    return consume_impl(0,false,now);
}
HandInteractionSnapshot HandInteractionBridge::consume_selected(std::uint64_t selected_sequence,Clock::time_point now) {
    return consume_impl(selected_sequence,true,now);
}
HandInteractionSnapshot HandInteractionBridge::consume_impl(std::uint64_t selected_sequence,bool selected,Clock::time_point now) {
    auto& s=*impl_;std::lock_guard lock(s.mutex);
    if(now==Clock::time_point::max())now=Clock::now();
    auto out=s.diagnostic(now);
    // A caller-supplied read time can precede a concurrently published value.
    // Do not turn that ordering race into a destructive tracking cancellation.
    if(out.status==HandInteractionStatus::clock_reversal && s.latest.status==HandInteractionStatus::ready &&
       now<s.latest.published_at && s.invalidated_sequence!=s.latest.sequence)return out;
    if(!out.valid_ray) {
        if(s.latest.sequence && s.latest.status==HandInteractionStatus::ready && s.invalidated_sequence!=s.latest.sequence) {
            s.invalidated_sequence=s.latest.sequence;s.invalidated_status=out.status;s.cancel_source();
        }
        if(s.consumer_pinched)out.pinch=PinchEvent::cancelled;
        s.consumer_pinched=false;s.consumed_sequence=s.latest.sequence;
        s.consumed_cancel_generation=s.cancel_generation;out.cancellation_generation=s.cancel_generation;
        return out;
    }
    if(s.consumed_cancel_generation!=s.cancel_generation) {
        s.consumed_cancel_generation=s.cancel_generation;
        if(s.consumer_pinched) {
            s.consumer_pinched=false;s.consumed_sequence=s.latest.sequence;
            if(s.source_pinched)s.suppressed_gesture_id=s.gesture_id;
            out.pinch=PinchEvent::cancelled;return out;
        }
    }
    // An entire release->repress can occur between logic reads without tracking
    // loss. End the old hold once, and require another opening before adopting
    // a new gesture; never merge two source pinches into one native hold.
    if(s.consumer_pinched && s.source_pinched && s.consumer_gesture_id!=s.gesture_id) {
        s.consumer_pinched=false;s.suppressed_gesture_id=s.gesture_id;
        s.consumed_sequence=s.latest.sequence;out.pinch=PinchEvent::cancelled;return out;
    }
    if(s.source_pinched && s.suppressed_gesture_id==s.gesture_id) {
        s.consumed_sequence=s.latest.sequence;return out;
    }
    if(selected && s.source_pinched && selected_sequence!=s.latest.sequence)return out;
    if(s.consumed_sequence==s.latest.sequence)return out;
    if(s.source_pinched)out.pinch=s.consumer_pinched?PinchEvent::held:PinchEvent::began;
    else if(s.consumer_pinched)out.pinch=PinchEvent::released;
    s.consumer_pinched=s.source_pinched;s.consumer_gesture_id=s.gesture_id;
    s.consumed_sequence=s.latest.sequence;return out;
}
void HandInteractionBridge::reset(Clock::time_point now) {
    auto& s=*impl_;std::lock_guard lock(s.mutex);s.cancel_source();s.last_xr_time.reset();s.previous_calibration.reset();
    if(!s.publish_clock_high_water || now>*s.publish_clock_high_water)s.publish_clock_high_water=now;
    s.store({},now,HandInteractionStatus::reset);
}
void HandInteractionBridge::set_priority(InteractionHand hand,Clock::time_point now){
    if(hand!=InteractionHand::left&&hand!=InteractionHand::right)throw std::invalid_argument("Invalid pointing priority");
    auto& s=*impl_;std::lock_guard lock(s.mutex);s.cancel_source();s.last_xr_time.reset();s.previous_calibration.reset();
    s.preferred=hand;s.options.hand=hand;
    if(!s.publish_clock_high_water||now>*s.publish_clock_high_water)s.publish_clock_high_water=now;
    s.store({},now,HandInteractionStatus::reset);
}
const char* hand_interaction_status_name(HandInteractionStatus status) noexcept {
    switch(status) {
    case HandInteractionStatus::no_sample:return "no_sample";
    case HandInteractionStatus::ready:return "ready";
    case HandInteractionStatus::inactive_frame:return "inactive_frame";
    case HandInteractionStatus::unfocused:return "unfocused";
    case HandInteractionStatus::invalid_head:return "invalid_head";
    case HandInteractionStatus::invalid_hand:return "invalid_hand";
    case HandInteractionStatus::degenerate_direction:return "degenerate_direction";
    case HandInteractionStatus::calibration_mismatch:return "calibration_mismatch";
    case HandInteractionStatus::invalid_calibration:return "invalid_calibration";
    case HandInteractionStatus::invalid_game_transform:return "invalid_game_transform";
    case HandInteractionStatus::out_of_order:return "out_of_order";
    case HandInteractionStatus::stale:return "stale";
    case HandInteractionStatus::clock_reversal:return "clock_reversal";
    case HandInteractionStatus::reset:return "reset";
    }
    return "unknown";
}
}
