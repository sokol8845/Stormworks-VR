#include "swvr/hand_interaction.hpp"
#include "swvr/comfort.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace swvr;
using namespace std::chrono_literals;
namespace {
using Clock=HandInteractionClock;
const auto epoch=Clock::time_point{}+10s;
const Matrix4d identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
const HandCalibration calibration{Pose{{},{0,1,0}},4};
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool near(double a,double b,double tolerance=1e-6){return std::abs(a-b)<tolerance;}
template<class F>void rejects(F&& fn,const char* message){bool caught{};try{fn();}catch(const std::invalid_argument&){caught=true;}check(caught,message);}
XrRuntimeFrame frame(std::uint64_t number,float gap=.05f) {
    XrRuntimeFrame f;f.id=number;f.predicted_display_time_ns=1000000000000LL+static_cast<std::int64_t>(number);
    f.running=f.focused=f.should_render=true;f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
    f.space_revision=4;
    auto& hand=f.hands[1];hand.active=true;
    for(auto i:{std::size_t(7),xr_index_tip,xr_thumb_tip})hand.joints[i].position_valid=hand.joints[i].position_tracked=true;
    hand.joints[7].pose.position={.2f,1,-.3f};hand.joints[xr_index_tip].pose.position={.2f,1,-.4f};
    hand.joints[xr_thumb_tip].pose.position={.2f+gap,1,-.4f};
    return f;
}
void begin_pinch(HandInteractionBridge& bridge) {
    bridge.publish(frame(1),calibration,identity,epoch);bridge.consume(epoch);
    bridge.publish(frame(2,.01f),calibration,identity,epoch+1ms);
    check(bridge.consume(epoch+1ms).pinch==PinchEvent::began,"pinch setup");
}
std::array<double,3> to_eye(const Matrix4d& eye,const std::array<double,3>& world) {
    std::array<double,3> result{};
    for(int col=0;col<3;++col)for(int row=0;row<3;++row)result[col]+=eye[col*4+row]*(world[row]-eye[12+row]);
    result[2]=-result[2];return result;
}
}
int main() {
    try {
        {
            // Same pipeline as NativeVr: moving beyond the head limit changes
            // view_origin every frame even though calibration stays unchanged.
            HandInteractionBridge bridge;
            const std::array<Pose,2> neutral{Pose{{},{-.032f,1,0}},Pose{{},{.032f,1,0}}};
            const auto neck=neck_position(calibration.local_origin,neutral);
            for(unsigned id=1;id<=3;++id){
                auto f=frame(id,id==1?.06f:.01f);auto eyes=neutral;
                for(auto& eye:eyes)eye.position.x+=.4f+.01f*static_cast<float>(id);
                f.eyes[0].pose=eyes[0];f.eyes[1].pose=eyes[1];
                const auto view=head_comfort_view(calibration.local_origin,neck,eyes,{},true,false);
                check(view.limited,"head-limit regression did not exercise a moving view origin");
                bridge.publish(f,calibration,view.origin,identity,epoch+std::chrono::milliseconds(id));
                const auto action=bridge.consume(epoch+std::chrono::milliseconds(id));
                check(action.valid_ray,"head-limit regression lost pointing ray");
                check(near(action.ray.origin[0],.2-view.origin.position.x)&&near(action.ray.direction[2],1),
                      "click ray stopped following the displayed hand after head correction");
                check(action.pinch==(id==1?PinchEvent::none:id==2?PinchEvent::began:PinchEvent::held),
                      "vehicle click lost while head comfort moves the view origin");
            }
        }
        // Priority applies only among usable hands. A partially tracked or
        // degenerate preferred ray must not erase the other hand's open/pinch.
        for(unsigned preferred=0;preferred<2;++preferred)for(unsigned defect=0;defect<2;++defect){
            const auto other=1u-preferred;
            HandInteractionBridge bridge{HandInteractionOptions{.hand=static_cast<InteractionHand>(preferred),.both_hands=true}};
            for(unsigned id=1;id<=8;++id){
                auto f=frame(id,id<=5?(id%2?.06f:.01f):.01f);f.hands[0]=f.hands[1];
                if(id<=6){
                    if(defect==0)f.hands[preferred].joints[xr_index_tip].position_tracked=false;
                    else f.hands[preferred].joints[xr_index_proximal].pose.position=f.hands[preferred].joints[xr_index_tip].pose.position;
                }else if(id==7)f.hands[preferred]=frame(id,.06f).hands[1];
                bridge.publish(f,calibration,identity,epoch+std::chrono::milliseconds(id));
                const auto state=bridge.consume(epoch+std::chrono::milliseconds(id));
                const auto expected=id==1?PinchEvent::none:id>=7?PinchEvent::held:id%2?PinchEvent::released:PinchEvent::began;
                check(state.valid_ray&&static_cast<unsigned>(state.hand)==other,"invalid preferred hand blocked the usable hand");
                check(state.pinch==expected,"fallback hand lost a gesture or recovered priority stole its hold");
            }
        }
        {
            HandInteractionBridge priority{HandInteractionOptions{.both_hands=true}};priority.set_priority(InteractionHand::left,epoch);
            auto open=frame(1);open.hands[0]=open.hands[1];priority.publish(open,calibration,identity,epoch);priority.consume(epoch);
            auto both=frame(2,.01f);both.hands[0]=both.hands[1];priority.publish(both,calibration,identity,epoch+1ms);
            const auto selected=priority.consume(epoch+1ms);check(selected.hand==InteractionHand::left&&selected.pinch==PinchEvent::began,"left priority did not win simultaneous pinches");
            priority.set_priority(InteractionHand::right,epoch+2ms);both.id=3;++both.predicted_display_time_ns;priority.publish(both,calibration,identity,epoch+3ms);
            check(!priority.inspect(epoch+3ms).pinch_down,"priority change reused a closed pinch");
        }
        {
            HandInteractionBridge bridge{HandInteractionOptions{.both_hands=true}};
            const auto publish=[&](unsigned id,bool left,bool right){
                auto f=frame(id);f.hands[0]=frame(id,left?.01f:.06f).hands[1];f.hands[1]=frame(id,right?.01f:.06f).hands[1];
                bridge.publish(f,calibration,identity,epoch+std::chrono::milliseconds(id));
                return bridge.inspect(epoch+std::chrono::milliseconds(id));
            };
            publish(1,false,false);bridge.consume(epoch+1ms);
            auto left=publish(2,true,false);check(left.hand==InteractionHand::left&&left.pinch_down,"left pinch did not select left ray");
            check(bridge.consume_selected(left.sequence,epoch+2ms).pinch==PinchEvent::began,"left begin missing");
            auto both=publish(3,true,true);check(both.hand==InteractionHand::left,"second hand stole an active hold");
            check(bridge.consume(epoch+3ms).pinch==PinchEvent::held,"left hold lost");
            auto opened=publish(4,false,true);check(opened.hand==InteractionHand::left&&!opened.pinch_down,"release was replaced by another hand");
            check(bridge.consume(epoch+4ms).pinch==PinchEvent::released,"left release missing");
            check(!publish(5,false,true).pinch_down,"already closed other hand manufactured a new press");
            publish(6,false,false);bridge.consume(epoch+6ms);
            auto right=publish(7,false,true);check(right.hand==InteractionHand::right&&bridge.consume_selected(right.sequence,epoch+7ms).pinch==PinchEvent::began,"right did not rearm after open");
            bridge.reset(epoch+8ms);check(!publish(9,true,true).pinch_down,"reset armed closed fists");
        }
        {
            HandInteractionBridge bridge;
            check(bridge.inspect(epoch).status==HandInteractionStatus::no_sample,"unpublished ray usable");
            bridge.publish(frame(1),calibration,identity,epoch);
            const auto ray=bridge.inspect(epoch);
            check(ray.valid_ray && ray.pinch==PinchEvent::none && ray.hand==InteractionHand::right,"default right ray");
            check(near(ray.ray.origin[0],.2)&&near(ray.ray.origin[1],0)&&near(ray.ray.origin[2],.4),"LOCAL origin or reflected basis lost");
            check(near(ray.ray.direction[0],0)&&near(ray.ray.direction[1],0)&&near(ray.ray.direction[2],1),"OpenXR -Z must become native +Z");
            check(ray.xr_time_ns==1000000000001LL && ray.published_at==epoch,"clock domains mixed");
            auto distant=identity;distant[12]=1000000000.125;
            auto f=frame(2);f.hands[1].joints[xr_index_tip].pose.position.x=.00390625f;
            f.hands[1].joints[7].pose.position.x=.00390625f;f.hands[1].joints[xr_thumb_tip].pose.position.x=.05390625f;
            bridge.publish(f,calibration,distant,epoch+1ms);
            check(near(bridge.inspect(epoch+1ms).ray.origin[0]-distant[12],.00390625,1e-8),"large world position erased hand offset");
            check(near(ray.ray.origin[0],.2),"published value mutated an earlier snapshot copy");
        }
        {
            // Gaze ray: the head's stereo centre and view direction, published even
            // when the hand cannot point (e.g. resting on the keyboard).
            HandInteractionBridge bridge;auto f=frame(1);f.eyes[0].pose.position={-.03f,1,0};f.eyes[1].pose.position={.03f,1,0};
            f.hands[1].active=false;bridge.publish(f,calibration,identity,epoch);
            auto s=bridge.inspect(epoch);
            check(!s.valid_ray && s.valid_gaze,"gaze missing while the hand is unavailable");
            check(near(s.gaze.origin[0],0)&&near(s.gaze.origin[1],0)&&near(s.gaze.origin[2],0),"gaze origin transform");
            check(near(s.gaze.direction[0],0)&&near(s.gaze.direction[1],0)&&near(s.gaze.direction[2],1),"gaze direction must follow OpenXR -Z to native +Z");
            check(!bridge.inspect(epoch+101ms).valid_gaze,"stale gaze usable");
            auto both=frame(2);both.eyes=f.eyes;bridge.publish(both,calibration,identity,epoch+2ms);
            s=bridge.inspect(epoch+2ms);check(s.valid_ray && s.valid_gaze,"gaze lost while the hand points");
            auto lost=frame(3);lost.focused=false;bridge.publish(lost,calibration,identity,epoch+3ms);
            check(!bridge.inspect(epoch+3ms).valid_gaze,"unfocused gaze usable");
            auto head=frame(4);head.position_tracked=false;bridge.publish(head,calibration,identity,epoch+4ms);
            check(!bridge.inspect(epoch+4ms).valid_gaze,"untracked head produced gaze");
        }
        {
            const float s=std::sqrt(.5f);
            const HandCalibration rotated_origin{Pose{{0,s,0,s},{1,1.5f,2}},4};
            const Matrix4d base{0,0,-1,0,0,1,0,0,1,0,0,0,10,20,30,1};
            auto f=frame(1);auto& hand=f.hands[1];
            const Pose relative_tip{{},{.1f,-.2f,-.5f}};
            hand.joints[xr_index_tip].pose.position=compose(rotated_origin.local_origin,relative_tip).position;
            hand.joints[7].pose.position=compose(rotated_origin.local_origin,Pose{{},{.1f,-.2f,-.4f}}).position;
            hand.joints[xr_thumb_tip].pose.position=compose(rotated_origin.local_origin,Pose{{},{.15f,-.2f,-.5f}}).position;
            HandInteractionBridge bridge;bridge.publish(f,rotated_origin,base,epoch);const auto result=bridge.inspect(epoch);
            check(result.valid_ray && near(result.ray.origin[0],10.5)&&near(result.ray.origin[1],19.8)&&near(result.ray.origin[2],29.9),"rotated calibration/base mismatch");
            check(near(result.ray.direction[0],1)&&near(result.ray.direction[2],0),"direction included translation or wrong rotation");
            const Pose eye{{0,std::sin(.15f),0,std::cos(.15f)},{-.032f,.01f,.02f}};
            const auto eye_t=offset_game_camera(base,eye);const auto expected=compose(inverse(eye),relative_tip);
            const auto roundtrip=to_eye(eye_t,result.ray.origin);
            check(near(roundtrip[0],expected.position.x,2e-6)&&near(roundtrip[1],expected.position.y,2e-6)&&near(roundtrip[2],expected.position.z,2e-6),"world ray origin did not roundtrip to eye coordinates");
            auto along=result.ray.origin;for(int i=0;i<3;++i)along[i]+=result.ray.direction[i]*.1;
            const auto ray_end=to_eye(eye_t,along);const auto expected_end=compose(inverse(eye),Pose{{},{.1f,-.2f,-.6f}});
            for(int i=0;i<3;++i)check(near(ray_end[i],i==0?expected_end.position.x:i==1?expected_end.position.y:expected_end.position.z,3e-6),"world ray direction did not roundtrip to eye coordinates");
        }
        {
            HandInteractionBridge bridge;bridge.publish(frame(1),calibration,identity,epoch);bridge.consume(epoch);
            bridge.publish(frame(2,.01f),calibration,identity,epoch+1ms);
            check(bridge.inspect(epoch+1ms).pinch==PinchEvent::none && bridge.inspect(epoch+1ms).pinch==PinchEvent::none,"inspect emitted/consumed gesture");
            check(bridge.consume(epoch+1ms).pinch==PinchEvent::began,"diagnostics stole pinch began");
            check(bridge.consume(epoch+2ms).pinch==PinchEvent::none,"same snapshot replayed began");
            bridge.publish(frame(3,.03f),calibration,identity,epoch+3ms);
            check(bridge.consume(epoch+3ms).pinch==PinchEvent::held,"pinch hysteresis lost");
            bridge.publish(frame(4,.05f),calibration,identity,epoch+4ms);
            check(bridge.consume(epoch+4ms).pinch==PinchEvent::released,"pinch release lost");
            check(bridge.consume(epoch+5ms).pinch==PinchEvent::none,"same snapshot replayed released");
        }
        {
            HandInteractionBridge bridge;begin_pinch(bridge);
            check(!bridge.inspect(epoch+102ms).valid_ray && bridge.inspect(epoch+102ms).pinch==PinchEvent::none,"stale diagnostic ray usable");
            auto cancelled=bridge.consume(epoch+102ms);
            check(cancelled.status==HandInteractionStatus::stale && cancelled.pinch==PinchEvent::cancelled,"staleness did not cancel consumer");
            check(bridge.consume(epoch+103ms).pinch==PinchEvent::none,"stale cancellation replayed");
            bridge.publish(frame(3,.01f),calibration,identity,epoch+104ms);
            check(bridge.consume(epoch+104ms).pinch==PinchEvent::none,"closed hand rearmed after staleness");
            bridge.publish(frame(4),calibration,identity,epoch+105ms);bridge.consume(epoch+105ms);
            bridge.publish(frame(5,.01f),calibration,identity,epoch+106ms);
            check(bridge.consume(epoch+106ms).pinch==PinchEvent::began,"open hand failed to rearm");
        }
        {
            HandInteractionBridge bridge;begin_pinch(bridge);
            bridge.publish(frame(3,.01f),calibration,identity,epoch+200ms);
            check(bridge.consume(epoch+200ms).pinch==PinchEvent::cancelled,"publish gap did not cancel");
            auto lost=frame(4);lost.focused=false;bridge.publish(lost,calibration,identity,epoch+201ms);
            bridge.publish(frame(5),calibration,identity,epoch+202ms);bridge.consume(epoch+202ms);
            bridge.publish(frame(6,.01f),calibration,identity,epoch+203ms);check(bridge.consume(epoch+203ms).pinch==PinchEvent::began,"gap recovery");
        }
        {
            HandInteractionBridge bridge;begin_pinch(bridge);
            auto lost=frame(3,.01f);lost.focused=false;bridge.publish(lost,calibration,identity,epoch+2ms);
            bridge.publish(frame(4),calibration,identity,epoch+3ms); // overwrite invalid sample before logic reads
            check(bridge.consume(epoch+3ms).pinch==PinchEvent::cancelled,"missed focus loss became a committing release");
            check(bridge.consume(epoch+3ms).pinch==PinchEvent::none,"missed-loss cancellation replayed");
            bridge.publish(frame(5,.01f),calibration,identity,epoch+4ms);
            check(bridge.consume(epoch+4ms).pinch==PinchEvent::began,"new open/closed sequence failed after cancellation");
        }
        {
            HandInteractionBridge bridge;begin_pinch(bridge);const auto original=bridge.inspect(epoch+1ms);
            bridge.publish(frame(2,.01f),calibration,identity,epoch+50ms);
            auto same=bridge.inspect(epoch+50ms);check(same.sequence==original.sequence && same.published_at==original.published_at,"duplicate XR time refreshed snapshot age");
            bridge.publish(frame(2,.01f),calibration,identity,epoch+102ms);
            check(bridge.consume(epoch+102ms).pinch==PinchEvent::cancelled,"repeated old frame did not expire");
        }
        {
            HandInteractionBridge bridge;begin_pinch(bridge);auto older=frame(3);older.predicted_display_time_ns=1;
            bridge.publish(older,calibration,identity,epoch+2ms);const auto out=bridge.consume(epoch+2ms);
            check(out.status==HandInteractionStatus::out_of_order && out.pinch==PinchEvent::cancelled,"XR time reversal accepted");
            bridge.reset(epoch+3ms);check(bridge.inspect(epoch+3ms).status==HandInteractionStatus::reset,"reset missing");
            auto restarted=frame(1);restarted.predicted_display_time_ns=1;bridge.publish(restarted,calibration,identity,epoch+4ms);
            check(bridge.consume(epoch+4ms).valid_ray,"explicit reset did not allow new XR clock history");
        }
        {
            HandInteractionBridge bridge;begin_pinch(bridge);auto next=frame(3,.01f);next.space_revision=5;
            const HandCalibration changed{calibration.local_origin,5};bridge.publish(next,changed,identity,epoch+2ms);
            check(bridge.consume(epoch+2ms).pinch==PinchEvent::cancelled,"reference-space change retained pinch");
            next=frame(4,.01f);next.space_revision=5;bridge.publish(next,changed,identity,epoch+3ms);
            check(bridge.consume(epoch+3ms).pinch==PinchEvent::none,"closed hand rearmed on space change");
        }
        for(int kind=0;kind<10;++kind) {
            HandInteractionBridge bridge;begin_pinch(bridge);auto f=frame(3,.01f);auto c=calibration;auto base=identity;
            HandInteractionStatus expected{};
            switch(kind) {
            case 0:f.hands[1].active=false;expected=HandInteractionStatus::invalid_hand;break;
            case 1:f.hands[1].joints[7].position_tracked=false;expected=HandInteractionStatus::invalid_hand;break;
            case 2:f.hands[1].joints[xr_thumb_tip].pose.position.x=std::numeric_limits<float>::quiet_NaN();expected=HandInteractionStatus::invalid_hand;break;
            case 3:f.hands[1].joints[7].pose.position=f.hands[1].joints[xr_index_tip].pose.position;expected=HandInteractionStatus::degenerate_direction;break;
            case 4:c.space_revision=99;expected=HandInteractionStatus::calibration_mismatch;break;
            case 5:c.local_origin.orientation={0,0,0,0};expected=HandInteractionStatus::invalid_calibration;break;
            case 6:base[0]=2;expected=HandInteractionStatus::invalid_game_transform;break;
            case 7:f.orientation_tracked=false;expected=HandInteractionStatus::invalid_head;break;
            case 8:f.should_render=false;expected=HandInteractionStatus::inactive_frame;break;
            case 9:f={};expected=HandInteractionStatus::inactive_frame;break;
            }
            bridge.publish(f,c,base,epoch+2ms);const auto out=bridge.consume(epoch+2ms);
            check(!out.valid_ray && out.status==expected && out.pinch==PinchEvent::cancelled,"invalid input failed to cancel");
            check(out.ray.origin==std::array<double,3>{} && bridge.consume(epoch+3ms).pinch==PinchEvent::none,"invalid snapshot leaked ray or repeated cancellation");
        }
        {
            HandInteractionBridge bridge;begin_pinch(bridge);
            // Earlier explicit read time can race a newer publisher. It must not
            // consume/cancel the publication; no-argument reads timestamp under lock.
            check(!bridge.consume(epoch).valid_ray,"future publication accepted for old read time");
            check(bridge.consume(epoch+2ms).pinch==PinchEvent::none,"old read time changed consumer state");
            bridge.publish(frame(3),calibration,identity,epoch);const auto out=bridge.consume(epoch+2ms);
            check(out.status==HandInteractionStatus::clock_reversal && out.pinch==PinchEvent::cancelled,"publication clock reversal accepted");
        }
        {
            HandInteractionBridge bridge;begin_pinch(bridge);
            const auto first=bridge.inspect(epoch+1ms);
            bridge.publish(frame(3),calibration,identity,epoch+2ms);
            const auto opened=bridge.inspect(epoch+2ms);
            bridge.publish(frame(4,.01f),calibration,identity,epoch+3ms);
            const auto second=bridge.inspect(epoch+3ms);
            check(bridge.consume(epoch+3ms).pinch==PinchEvent::cancelled,"missed release/repress merged into an old hold");
            check(first.pinch_down && first.pinch_gesture_id>0,"source pinch identity missing");
            check(!opened.pinch_down && opened.pinch_gesture_id==first.pinch_gesture_id,"release lost gesture identity");
            check(second.pinch_down && second.pinch_gesture_id>first.pinch_gesture_id &&
                  second.cancellation_generation==first.cancellation_generation,"normal repress lacks a new identity");
            bridge.publish(frame(5,.01f),calibration,identity,epoch+4ms);
            check(bridge.consume(epoch+4ms).pinch==PinchEvent::none,"discarded repress resumed without opening");
            bridge.publish(frame(6),calibration,identity,epoch+5ms);bridge.consume(epoch+5ms);
            bridge.publish(frame(7,.01f),calibration,identity,epoch+6ms);
            check(bridge.consume(epoch+6ms).pinch==PinchEvent::began,"new gesture failed after missed-repress cancellation");
            check(!bridge.inspect(epoch+107ms).pinch_down,"stale diagnostic kept a pinch down");
            bridge.reset(epoch+108ms);
            bridge.publish(frame(8),calibration,identity,epoch+109ms);bridge.consume(epoch+109ms);
            bridge.publish(frame(9,.01f),calibration,identity,epoch+110ms);
            check(bridge.inspect(epoch+110ms).pinch_gesture_id>second.pinch_gesture_id,"reset reused a source gesture identity");
        }
        {
            HandInteractionBridge bridge;
            bridge.publish(frame(1),calibration,identity,epoch);
            const auto selected_open=bridge.inspect(epoch);
            bridge.consume_selected(selected_open.sequence,epoch);
            bridge.publish(frame(2,.01f),calibration,identity,epoch+1ms);
            const auto selected_pinch=bridge.inspect(epoch+1ms);
            check(bridge.consume_selected(selected_open.sequence,epoch+1ms).pinch==PinchEvent::none,"old selection consumed a newer pinch");
            check(bridge.inspect(epoch+1ms).sequence==selected_pinch.sequence,"selection mismatch mutated publication");
            check(bridge.consume_selected(selected_pinch.sequence,epoch+2ms).pinch==PinchEvent::began,"matching selection lost deferred pinch");
            check(bridge.consume_selected(selected_pinch.sequence,epoch+2ms).pinch==PinchEvent::none,"selected pinch replayed");
            bridge.publish(frame(3,.01f),calibration,identity,epoch+3ms);
            const auto moved=bridge.inspect(epoch+3ms);
            check(moved.pinch_gesture_id==selected_pinch.pinch_gesture_id && moved.pinch_down,"same gesture changed identity with pose");
            check(bridge.consume_selected(selected_pinch.sequence,epoch+3ms).pinch==PinchEvent::none,"old selection consumed held pose");
            check(bridge.consume_selected(moved.sequence,epoch+3ms).pinch==PinchEvent::held,"new matching selection lost hold");
            bridge.publish(frame(4),calibration,identity,epoch+4ms);
            check(bridge.consume_selected(0,epoch+4ms).pinch==PinchEvent::released,"selection mismatch blocked cleanup release");
            check(bridge.consume_selected(0,epoch+4ms).pinch==PinchEvent::none,"selected release replayed");
            bridge.publish(frame(5,.01f),calibration,identity,epoch+5ms);
            check(bridge.consume_selected(0,epoch+5ms).pinch==PinchEvent::none,"zero selection consumed a pinch");
            check(bridge.consume_selected(bridge.inspect(epoch+5ms).sequence,epoch+5ms).pinch==PinchEvent::began,"zero selection destroyed a deferred pinch");
            auto lost=frame(6,.01f);lost.focused=false;
            bridge.publish(lost,calibration,identity,epoch+6ms);
            check(bridge.consume_selected(0,epoch+6ms).pinch==PinchEvent::cancelled,"selection mismatch blocked focus cancellation");
            check(bridge.consume_selected(0,epoch+7ms).pinch==PinchEvent::none,"selected cancellation replayed");
        }
        {
            HandInteractionOptions options;options.hand=InteractionHand::left;
            HandInteractionBridge bridge(options);auto f=frame(1);f.hands[0]=f.hands[1];f.hands[1]={};
            bridge.publish(f,calibration.local_origin,identity,epoch);check(bridge.inspect(epoch).valid_ray,"explicit left hand selection failed");
            options.max_age=Clock::duration::zero();rejects([&]{HandInteractionBridge bad(options);},"zero age accepted");
        }
        {
            HandInteractionBridge bridge;std::atomic<bool> start{},done{},bad{};std::atomic<unsigned> observed{};
            std::thread producer([&]{while(!start.load())std::this_thread::yield();for(std::uint64_t i=1;i<=2000;++i) {
                auto base=identity;base[12]=1000000000.+static_cast<double>(i);bridge.publish(frame(i),calibration,base);
                if(i%8==0)std::this_thread::yield();
            }done=true;});
            std::thread reader([&]{start=true;do {const auto value=bridge.inspect();if(value.valid_ray) {
                ++observed;if(!near(value.ray.origin[0]-(1000000000.+static_cast<double>(value.xr_frame_id)),.2,1e-6)||value.pinch!=PinchEvent::none)bad=true;
            }}while(!done.load());});
            producer.join();reader.join();check(!bad && observed>0,"cross-thread snapshot was torn or diagnostic consumed gesture");
            HandInteractionBridge migration;begin_pinch(migration);migration.publish(frame(3),calibration,identity,epoch+2ms);
            PinchEvent first{},second{};std::thread one([&]{first=migration.consume(epoch+2ms).pinch;});one.join();
            std::thread two([&]{second=migration.consume(epoch+2ms).pinch;});two.join();
            check(first==PinchEvent::released && second==PinchEvent::none,"logic thread migration duplicated transition");
        }
        std::cout<<"Hand interaction world rays, precision, clocks, cancellation and cross-thread snapshots passed (no game calls).\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
