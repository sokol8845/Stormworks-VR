#include "swvr/hand_recording.hpp"
#include "swvr/locomotion_replay.hpp"
#include <bit>
#include <chrono>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace swvr;
namespace {
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
template<class F>void rejects(F f){bool bad{};try{f();}catch(const std::exception&){bad=true;}check(bad,"corrupt recording accepted");}
HandRecordingSample sample(unsigned id,float gap,float forward=0){
    HandRecordingSample s;s.elapsed_ns=static_cast<std::int64_t>(id)*16'000'000;s.walking_allowed=true;
    auto& f=s.frame;f.id=id;f.space_revision=7;f.predicted_display_time_ns=1'000'000'000+s.elapsed_ns;
    f.running=f.focused=f.should_render=f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
    f.eyes[0].pose.position={-.032f,1.6f,0};f.eyes[1].pose.position={.032f,1.6f,0};
    auto& h=f.hands[0];h.active=true;for(auto& j:h.joints)j.position_valid=j.position_tracked=true;
    h.joints[1].pose.position={-.3f,1.1f,-.35f-forward};h.joints[5].pose.position={-.3f,1.1f,-.4f-forward};
    h.joints[15].pose.position={-.3f+gap,1.1f,-.4f-forward};h.joints[10].pose.position={-.3f,1.1f,-.5f-forward};return s;
}
}
int main(int argc,char** argv){try{
    check(argc==2,"project temporary directory required");GestureSettings legacy;legacy.joystick.activation=JoystickActivation::middle_pinch;legacy.arm_swing.activation=RunActivation::alternating;auto settings=legacy;settings.joystick.dead_zone_m=.031234567f;
    settings.joystick.forward_only=true;
    auto original=sample(1,.06f);original.frame.eyes[0].texture=999;
    original.frame.hands[1].joints[25].pose.position.x=std::bit_cast<float>(std::uint32_t{0x7fc01234});
    original.frame.hands[1].joints[24].pose.position.y=-0.f;
    std::stringstream buffer;write_hand_recording_header(buffer);write_hand_configuration(buffer,settings,LocomotionMode::joystick);write_hand_sample(buffer,original);buffer<<"END\n";
    read_hand_recording_header(buffer);const auto configuration=read_hand_record(buffer);
    check(configuration.settings==settings,"configuration precision/forward-only preference lost");
    const auto record=read_hand_record(buffer);const auto& copy=record.sample;
    check(copy.elapsed_ns==original.elapsed_ns&&copy.frame.space_revision==7&&copy.walking_allowed&&copy.frame.eyes[0].texture==0,"sample identity or borrowed GL lifetime serialized incorrectly");
    check(std::bit_cast<std::uint32_t>(copy.frame.hands[1].joints[25].pose.position.x)==0x7fc01234&&std::bit_cast<std::uint32_t>(copy.frame.hands[1].joints[24].pose.position.y)==0x80000000,"invalid/untracked float bits not preserved");
    check(read_hand_record(buffer).kind==HandRecordKind::end,"END missing");
    for(unsigned version:{1u,2u,3u}){std::stringstream old;write_hand_recording_header(old,version);if(version>=2)old<<"Q 1 0 0\n";old<<"C joystick\n[joystick]\n[arm_swing]\nEND_CONFIG\nEND\n";
        read_hand_recording_header(old);const auto c=read_hand_record(old);
        check(!c.settings.joystick.forward_only&&c.settings.joystick.activation==(version<3?JoystickActivation::middle_pinch:JoystickActivation::whole_hand)&&c.settings.arm_swing.activation==(version<3?RunActivation::alternating:RunActivation::two_fists),"recording schema changed historical activation semantics");}
    for(const char* invalid:{"SWVR_HANDS 9\n","SWVR_HANDS 1"})rejects([&]{std::istringstream in(invalid);read_hand_recording_header(in);});
    for(const char* invalid:{"F 1 0\n","C invalid\n","C joystick\n[joystick]\nwat=5\nEND_CONFIG\n","END\ntrailing\n",""})rejects([&]{std::istringstream in(invalid);read_hand_record(in);});
    std::stringstream replay;write_hand_recording_header(replay);write_hand_configuration(replay,legacy,LocomotionMode::joystick);
    const std::array<HandRecordingSample,4> frames{sample(1,.06f),sample(2,.01f),sample(3,.01f,.14f),sample(4,.06f,.14f)};
    HandLocomotion live,offline;live.configure(legacy);std::array<std::uint64_t,4> expected{};
    for(unsigned i=0;i<frames.size();++i){const auto now=HandLocomotion::Clock::time_point{}+std::chrono::nanoseconds(frames[i].elapsed_ns);live.publish(frames[i].frame,true,now);expected[i]=locomotion_buttons(live.inspect(now));write_hand_sample(replay,frames[i]);}
    replay<<"END\n";read_hand_recording_header(replay);unsigned seen{};
    for(;;){const auto r=read_hand_record(replay);if(r.kind==HandRecordKind::end)break;if(r.kind==HandRecordKind::configuration){offline.configure(r.settings);offline.set_mode(r.mode);continue;}
        const auto now=HandLocomotion::Clock::time_point{}+std::chrono::nanoseconds(r.sample.elapsed_ns);offline.publish(r.sample.frame,r.sample.walking_allowed,now);
        check(seen<expected.size()&&locomotion_buttons(offline.inspect(now))==expected[seen++],"replay diverged from recorded detector");}
    check(seen==4&&expected[2]==(move_forward|move_sprint)&&expected[3]==0,"recording test did not cover sprint and release");
    const auto base=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(base);
    const auto file=base/"hand-replay-fixture-20261104.tsv";
    {std::ofstream output(file,std::ios::binary);output<<replay.str();}
    const auto limited_path=base/("record-limit-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".tsv");
    HandRecordingWriter limited;limited.open(limited_path,{},LocomotionMode::joystick,1024);check(!limited.append(frames[0])&&limited.limited(),"recording byte cap not enforced");limited.close();
    check(std::filesystem::file_size(limited_path)<=1024,"END exceeded recording byte cap");std::filesystem::remove(limited_path);
    HandLocomotion journaled;journaled.configure(legacy);journaled.start_journal();
    const auto publish=[&](unsigned id,float gap,float distance){const auto f=sample(id,gap,distance);journaled.publish(f.frame,true);journaled.inspect();};
    publish(1,.06f,0);publish(2,.01f,0);publish(3,.01f,.14f);
    journaled.note_native_state(8,{move_back,0x1000});journaled.set_native_block("STEROWANIE KLAWIATURA");journaled.reset("physical_keyboard");journaled.inspect();
    journaled.set_native_block(nullptr);publish(4,.01f,.14f);publish(5,.06f,0);publish(6,.01f,0);publish(7,.01f,.14f);
    const auto batch=journaled.drain_journal(true);check(!batch.overflow&&batch.events.size()>15,"input journal missing events");
    std::stringstream full;write_hand_recording_header(full,2);for(const auto& event:batch.events)write_locomotion_event(full,event);full<<"END\n";
    read_hand_recording_header(full);LocomotionReplay replayed;unsigned resets{},inspections{};bool physical{};
    for(;;){const auto record=read_hand_record(full);if(record.kind==HandRecordKind::end)break;const auto state=replayed.apply(record);check(!state.mismatch,"full replay diverged after physical cancellation");
        if(record.event.kind==LocomotionEventKind::reset)++resets;if(record.event.kind==LocomotionEventKind::inspect)++inspections;if(state.blocks==8&&state.physical[0]==move_back)physical=true;}
    check(resets==1&&inspections==8&&physical,"native state/reset/inspect not preserved");
    journaled.start_journal();for(int i=0;i<1200;++i)journaled.reset("overflow_test");const auto overflow=journaled.drain_journal(true);check(overflow.overflow&&overflow.events.size()==1024,"journal overflow was silently dropped");
    std::cout<<"PASS hands recording: complete schema, exact float bits, config precision, truncation, replay and byte cap\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
