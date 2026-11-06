#include "swvr/native_hand_buttons.hpp"
#include "swvr/hand_event_registry.hpp"
#include "swvr/native_button_bindings.hpp"
#include "swvr/native_button_retirement.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace swvr {
#ifdef SWVR_HAND_BUTTONS_TESTING
// Unit-fixture synchronization at the final pre-handoff boundary. Production
// builds contain neither this callback nor a runtime switch for it.
void native_hand_buttons_before_submit_for_test();
#endif
namespace {
using U=std::uintptr_t;
using Loop=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t);
using ShortLoop=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,int*);
using Dispatch=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,void*);
using Action=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,int,int);
using Dtor=void*(*)(void*,std::uint32_t);
using Allocate=void*(*)(std::size_t);
using Push=void(*)(void*,void*);
using Deactivate=void(*)(void*,std::uint64_t,void*,const int*);
using Replace=U(*)(void*,std::uint64_t,void*,std::uint16_t,const void*,std::uint64_t);
Loop original_loop{};ShortLoop original_short_loop{};Dispatch original_dispatch{};
Action original_action{};Dtor original_dtor{};
Deactivate original_deactivate{};Replace original_replace{};
constexpr U owner_vt=0xb0f4a0,simulation_vt=0xb1a988,data_vt=0xb17cc0,button_vt=0xb75658;
constexpr auto max_age=std::chrono::milliseconds(100);
struct HookSpec {U rva;std::array<unsigned char,24> prefix;};
constexpr std::array<HookSpec,7> specs{{
 {0x86a150,{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xb8,0xfd,0xff,0xff,0x48,0x81}},
 {0x86cc30,{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec}},
 {0x790850,{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0xb8,0xfc,0xff,0xff,0x48,0x81}},
 {0x695e70,{0x48,0x89,0x4c,0x24,0x08,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8d,0x05,0x4c,0xc2,0x50,0x00,0x48,0x89,0x01,0xf6}},
 {0x78c7a0,{0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x4c,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41}},
 {0x7aca70,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x41,0x80,0xb8,0x33}},
 {0x5f2950,{0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xe9,0x48,0x81,0xec}}
}};
bool accessible(U at,std::size_t bytes,bool writable=false,bool executable=false) noexcept {
    if(!at || bytes>std::numeric_limits<U>::max()-at)return false;
    const auto end=at+bytes;
    while(at<end) {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(reinterpret_cast<void*>(at),&m,sizeof(m)) || m.State!=MEM_COMMIT ||
           (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto p=m.Protect&0xffu;
        const bool x=p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
        const bool w=p==PAGE_READWRITE || p==PAGE_WRITECOPY || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
        if((!x&&!w&&p!=PAGE_READONLY) || (writable&&!w) || (executable&&!x))return false;
        const auto next=reinterpret_cast<U>(m.BaseAddress)+m.RegionSize;
        if(next<=at)return false;at=next;
    }
    return true;
}
template<class T>bool read(U object,std::size_t offset,T& value) noexcept {
    if(!object || offset>std::numeric_limits<U>::max()-object || !accessible(object+offset,sizeof(T)))return false;
    std::memcpy(&value,reinterpret_cast<const void*>(object+offset),sizeof(T));return true;
}
template<class T>T value(U object,std::size_t offset) noexcept {T v{};read(object,offset,v);return v;}
bool zero_byte(U object,std::size_t offset) noexcept {unsigned char v{1};return read(object,offset,v)&&v==0;}
struct Live {
    U owner{},simulation{},controller{},data{},request_context{},world{},actor{};
    bool valid{};
    bool operator==(const Live& other) const noexcept {
        return owner==other.owner&&controller==other.controller&&data==other.data&&world==other.world&&actor==other.actor;
    }
};
struct Hold {HandEventCertificate cert{};bool attempted{};};
struct Backend {
    U base{};HandInteractionBridge* bridge{};HandEventRegistry events;
    std::unique_ptr<NativeButtonRequestBindings> bindings;
    std::optional<NativeButtonRequestApi> release_api;
    Allocate allocate{};Push push{};
    std::mutex state_mutex,log_mutex;
    NativeHandButtonsStats counters{};
    Live identity{};std::uint64_t epoch=1,operation_nonce{},foreign_revision{};
    bool operation{},cancel_requested{},require_open{},blocked{};
    unsigned transition_depth{};
    NativeInputLifecycleCallbacks lifecycle;
    U last_target{};int last_target_type{-2}; // Diagnostic target classification (state_mutex).
    std::optional<HandEventCertificate> candidate;
    std::optional<HandEventToken> pending;
    std::optional<Hold> hold;
    std::atomic<unsigned> event_operations{};
    FILE* log{};std::uint64_t rows{};
    void note(const char* kind,const char* why,const HandEventCertificate* c=nullptr) noexcept {
        try {std::lock_guard lock(log_mutex);if(!log||rows>=10000)return;
            std::fprintf(log,"%llu\t%lu\t%s\t%s\t%llu\t%llu\t%llx\n",
                static_cast<unsigned long long>(++rows),GetCurrentThreadId(),kind,why,
                static_cast<unsigned long long>(c?c->selected_sequence:0),
                static_cast<unsigned long long>(c?c->pinch_gesture_id:0),
                static_cast<unsigned long long>(c?c->target_token:0));
            if(rows%20==0)std::fflush(log);
        }catch(...){}
    }
    void fail(const char* why) noexcept {
        {std::lock_guard lock(state_mutex);if(blocked)return;
            blocked=true;++counters.faults;counters.last_fault=why;candidate.reset();}
        note("fault",why);
    }
};
Backend* backend{};std::atomic<bool> installed{};
std::atomic<bool> keypad_opening{};
struct LoopScope {Live live;LoopScope* previous{};bool known{};};
thread_local LoopScope* current_loop{};
struct OwnedDispatch {HandEventCertificate cert;Live live;bool action_seen{},action_executed{};OwnedDispatch* previous{};};
thread_local OwnedDispatch* owned_dispatch{};
Live resolve_simulation(Backend& b,U owner_token,U simulation,U data) noexcept {
    Live l;l.owner=owner_token;l.simulation=simulation;l.data=data;
    if(!owner_token || !data || data>std::numeric_limits<U>::max()-0x280 || value<U>(data,0)!=b.base+data_vt)return l;
    l.request_context=data+0x280;
    if(value<U>(l.simulation,0)!=b.base+simulation_vt || l.simulation>std::numeric_limits<U>::max()-0x38)return l;
    l.controller=l.simulation+0x38;
    if(!read(l.controller,0,l.world)||!l.world)return l;
    // The measured native active-character chain. Actor is an identity token:
    // never cache it for later dereference or use a retired character's C.
    U node{},holder{};int kind{};
    if(read(l.world,0xbb9f8,node) && node && read(node,0,kind) && kind==4 &&
       read(node,0x90,holder) && holder)read(holder,0,l.actor);
    l.valid=true;return l;
}
Live resolve(Backend& b,U owner,U data,U request_context,U expected_controller=0) noexcept {
    Live invalid;invalid.owner=owner;invalid.data=data;invalid.request_context=request_context;
    if(!owner || value<U>(owner,0)!=b.base+owner_vt)return invalid;
    auto l=resolve_simulation(b,owner,value<U>(owner,0xce190),data);
    if(request_context!=l.request_context || (expected_controller && expected_controller!=l.controller))l.valid=false;
    return l;
}
bool still_live(Backend& b,const Live& live) noexcept {
    const auto current=resolve(b,live.owner,live.data,live.request_context,live.controller);
    return current.valid && current==live;
}
const char* interaction_gate(Backend& b,const Live& l) noexcept {
    if(!l.valid)return "invalid_live_context";
    if(!l.actor)return "no_active_character";
    if(value<U>(l.owner,0x350)!=l.simulation)return "inactive_simulation";
    if(!zero_byte(l.data,0x233))return "benchmark";
    for(auto offset:{0x494u,0x49cu,0x49fu,0x4bcu,0x4bdu})if(!zero_byte(l.controller,offset))return "controller_mode";
    const auto pause=value<U>(l.owner,0xce150);
    int pane{-1};
    if(value<U>(pause,0)!=b.base+0xb07c10 || !read(pause,0x58,pane) || pane!=0)return "pause_ui";
    for(auto p:std::array<std::pair<U,std::size_t>,4>{{{l.owner,0xcde80},{l.simulation,0x25b0},{l.simulation,0x1d00},{l.simulation,0x25a8}}}) {
        U token{};if(!read(p.first,p.second,token)||token)return "ui_capture";
    }
    unsigned char mouse_route{};if(!read(l.simulation,0x3010,mouse_route)||!mouse_route)return "mouse_route";
    return nullptr;
}
const char* press_gate(Backend& b,const Live& l,U target) noexcept {
    if(!l.valid || !still_live(b,l))return "live_identity_changed";
    if(const auto why=interaction_gate(b,l))return why;
    if(!zero_byte(l.controller,0x4bf)||!zero_byte(l.controller,0x4c0))return "native_hand_down";
    for(auto offset:{0x558u,0x560u,0x550u}){U held{};if(!read(l.controller,offset,held)||held)return "native_hold";}
    if(!target || value<U>(l.controller,0x520)!=target || value<U>(target,0)!=b.base+button_vt)return "target_mismatch";
    const auto definition=value<U>(target,0x58);int type{-1};
    // 0=Push, 1=Toggle, 4=throttle lever: the dispatcher sends all through the
    // same type-10 press/release request as keyboard E (78C7A0); a held lever
    // moves natively. 5/6 open keypad UI; 2/3 are unverified.
    if(!read(definition,0xda4,type)||(type!=0&&type!=1&&type!=4&&!(keypad_opening&&(type==5||type==6))))return "unsupported_button_type";
    return nullptr;
}
bool compatible(const HandInteractionSnapshot& s,const HandEventCertificate& c) noexcept {
    const auto now=HandInteractionClock::now();
    return s.valid_ray&&s.pinch_down&&s.hand==(c.action==HandAction::q?InteractionHand::left:InteractionHand::right)&&s.pinch_gesture_id==c.pinch_gesture_id&&
        s.space_revision==c.space_revision&&s.cancellation_generation==c.cancellation_generation&&
        now>=s.published_at&&now-s.published_at<=max_age&&now>=c.selected_at&&now-c.selected_at<=max_age;
}
bool observe_identity(Backend& b,const Live& l) {
    bool changed{},unresolved{},invalid=!l.valid;
    {std::lock_guard lock(b.state_mutex);
        if(!l.valid) {
            changed=b.identity.valid;b.identity.valid=false;b.candidate.reset();
            if(changed){++b.counters.scene_generation;++b.epoch;}
            unresolved=b.hold.has_value();
        } else if(!b.identity.valid || !(b.identity==l)) {
            changed=b.identity.valid;b.identity=l;++b.counters.scene_generation;++b.epoch;b.candidate.reset();
            unresolved=b.hold.has_value();
        }
    }
    if(unresolved)b.fail(invalid?"live_context_lost":"scene_changed_with_hold");
    if(changed){b.events.reset();b.note("scene",invalid?"live_context_lost":"identity_changed");}
    return !invalid;
}
bool reserve(Backend& b,std::uint64_t& nonce) {
    std::lock_guard lock(b.state_mutex);if(b.operation)return false;
    b.operation=true;nonce=++b.operation_nonce;return true;
}
struct Operation {
    Backend& b;std::uint64_t nonce;
    ~Operation(){std::lock_guard lock(b.state_mutex);if(b.operation_nonce==nonce)b.operation=false;}
};
void cleanup_hold(Backend& b,const Live& live,bool force_cancel) noexcept {
    try {
        Hold h;std::uint64_t nonce{},foreign{};
        {std::lock_guard lock(b.state_mutex);
            if(b.blocked||!b.hold||b.hold->attempted||b.operation)return;
            h=*b.hold;b.hold->attempted=true;b.operation=true;nonce=++b.operation_nonce;foreign=b.foreign_revision;
        }
        Operation operation{b,nonce};
        if(!live.valid || h.cert.owner_token!=live.owner || h.cert.controller_token!=live.controller || !still_live(b,live) ||
           value<U>(live.controller,hand_held_offset(h.cert.action))!=h.cert.target_token) {b.fail("cleanup_context_or_hold_lost");return;}
        bool identity_matches{};
        {std::lock_guard lock(b.state_mutex);identity_matches=h.cert.scene_generation==b.counters.scene_generation && live==b.identity;}
        // Leave old values as identity tokens; never dereference an old scene's target.
        if(!identity_matches){b.fail("cleanup_identity_mismatch");return;}
        const auto initial_flag=value<unsigned char>(live.controller,hand_down_offset(h.cert.action));
        const auto latest=b.bridge->inspect();
        bool cancel=force_cancel || !latest.valid_ray || latest.pinch_down ||
            latest.pinch_gesture_id!=h.cert.pinch_gesture_id || latest.space_revision!=h.cert.space_revision ||
            latest.cancellation_generation!=h.cert.cancellation_generation;
        {std::lock_guard lock(b.state_mutex);cancel=cancel||b.cancel_requested;}
        const auto captured=b.bindings->capture_release({h.cert.scene_generation,live.owner,
            reinterpret_cast<void*>(live.controller),true},h.cert);
        if(!captured){b.note("release_rejected",native_button_capture_status_name(captured.status),&h.cert);b.fail("release_capture_failed");return;}
        if(!still_live(b,live)){b.fail("cleanup_identity_changed");return;}
        if(!b.release_api)b.release_api=wrap_native_button_request_api(b.bindings->api());
        NativeButtonReleaseRequest request{*b.release_api};
        try {request.prepare(captured.data.release_data(cancel));}
        catch(...) {
            if(request.state()==NativeButtonRequestState::preparation_failed)try{request.discard();}catch(...){}
            b.fail("release_prepare_failed");return;
        }
        try {request.submit(reinterpret_cast<void*>(live.request_context+0x340));}
        catch(...) {b.fail("release_handoff_uncertain");return;}
        if(!still_live(b,live)){b.fail("release_handoff_identity_changed");return;}
        bool no_foreign{};{std::lock_guard lock(b.state_mutex);no_foreign=b.foreign_revision==foreign;}
        if(!no_foreign || value<U>(live.controller,hand_held_offset(h.cert.action))!=h.cert.target_token ||
           value<unsigned char>(live.controller,hand_down_offset(h.cert.action))!=initial_flag ||
           !accessible(live.controller+hand_held_offset(h.cert.action),sizeof(U),true)||!accessible(live.controller+hand_down_offset(h.cert.action),1,true)) {
            b.fail("release_handoff_native_state_changed");return;
        }
        const U empty{};const unsigned char up{};
        std::memcpy(reinterpret_cast<void*>(live.controller+hand_held_offset(h.cert.action)),&empty,sizeof(empty));
        std::memcpy(reinterpret_cast<void*>(live.controller+hand_down_offset(h.cert.action)),&up,sizeof(up));
        {std::lock_guard lock(b.state_mutex);
            if(b.hold && b.hold->cert==h.cert)b.hold.reset();
            ++b.counters.release_requests;b.require_open=true;
        }
        b.note("release",cancel?"cancel_request":"normal_open_request",&h.cert);
    } catch(...) {b.fail("cleanup_exception");}
}
void service(Backend& b,const Live& live) noexcept {
    try {
        {std::lock_guard lock(b.state_mutex);if(b.transition_depth)return;}
        if(!observe_identity(b,live))return;
        const auto sample=b.bridge->inspect();bool need_release{},cancel{};std::optional<Hold> held;
        {std::lock_guard lock(b.state_mutex);
            if(b.blocked)return;
            if(sample.valid_ray&&!sample.pinch_down)b.require_open=false;
            held=b.hold;
            if(b.hold){need_release=b.cancel_requested||!sample.valid_ray||!sample.pinch_down||
                sample.hand!=(b.hold->cert.action==HandAction::q?InteractionHand::left:InteractionHand::right)||sample.pinch_gesture_id!=b.hold->cert.pinch_gesture_id||
                sample.cancellation_generation!=b.hold->cert.cancellation_generation||sample.space_revision!=b.hold->cert.space_revision;
                cancel=b.cancel_requested;
            }
        }
        if(held) {
            U current{};
            if(!read(live.controller,hand_held_offset(held->cert.action),current) || current!=held->cert.target_token) {
                b.fail("held_target_lost");return;
            }
            // A press-only gate also rejects our own down/held flags and a new
            // hover target. Only mode/UI/context gates cancel an existing hold.
            if(interaction_gate(b,live)){need_release=true;cancel=true;}
        }
        if(need_release)cleanup_hold(b,live,cancel);
        HandEventCertificate cert;std::uint64_t epoch{},nonce{};
        {std::lock_guard lock(b.state_mutex);
            if(b.blocked||b.operation||b.hold||b.pending||!b.candidate)return;
            cert=*b.candidate;b.candidate.reset();epoch=b.epoch;b.operation=true;nonce=++b.operation_nonce;
        }
        Operation operation{b,nonce};
        const auto why=press_gate(b,live,cert.target_token);
        if(why||!compatible(b.bridge->inspect(),cert)) {b.note("enqueue_rejected",why?why:"gesture_changed",&cert);return;}
        struct KeyEvent {U vtable;int action,phase;};static_assert(sizeof(KeyEvent)==16);
        auto* event=static_cast<KeyEvent*>(b.allocate(sizeof(KeyEvent)));
        if(!event){b.fail("key_allocation_failed");return;}
        *event={b.base+0xbb2110,static_cast<int>(cert.action),1};
        const auto registration=b.events.register_event(reinterpret_cast<U>(event),HandEventPhase::press,cert);
        if(!registration){original_dtor(event,1);b.fail("event_registration_failed");return;}
        bool valid{};
        {std::lock_guard lock(b.state_mutex);valid=!b.blocked&&b.epoch==epoch&&!b.cancel_requested&&
            cert.scene_generation==b.counters.scene_generation;if(valid)b.pending=registration.token;}
        if(!valid || !compatible(b.bridge->inspect(),cert) || press_gate(b,live,cert.target_token)) {
            b.events.erase_before_destroy(registration.token);original_dtor(event,1);
            {std::lock_guard lock(b.state_mutex);if(b.pending==registration.token)b.pending.reset();}
            b.note("enqueue_rejected","reset_or_gate_during_allocation",&cert);return;
        }
#ifdef SWVR_HAND_BUTTONS_TESTING
        native_hand_buttons_before_submit_for_test();
#endif
        if(!b.events.mark_submitted(registration.token)) {
            // Reset may cancel registration after the last gate. Native push
            // has not run, so ownership is still ours and must not be retained
            // as an uncertain handoff waiting for a native destructor.
            b.events.erase_before_destroy(registration.token);original_dtor(event,1);
            {std::lock_guard lock(b.state_mutex);if(b.pending==registration.token)b.pending.reset();}
            b.note("enqueue_rejected","cancelled_before_handoff",&cert);return;
        }
        try {b.push(reinterpret_cast<void*>(live.owner+0xce6b8),event);}
        catch(...) {b.events.mark_uncertain(registration.token);b.fail("key_handoff_uncertain");return;}
        {std::lock_guard lock(b.state_mutex);++b.counters.events_queued;}
        b.note("queued",cert.action==HandAction::q?"press19_phase1":"press20_phase1",&cert);
    }catch(...) {b.fail("service_exception");}
}
void selection_complete(void*,const NativeHandSelection& selection) noexcept {
    if(!backend||!installed.load(std::memory_order_acquire))return;
    auto& b=*backend;
    try {
        {std::lock_guard lock(b.state_mutex);++b.counters.selections;if(b.transition_depth)return;}
        const auto a6=reinterpret_cast<U>(selection.a6_live);
        if(a6<0x358)return;
        const U owner=a6-0x358;
        if(selection.a3!=owner+0xce600)return;
        const auto live=resolve(b,owner,selection.a4,selection.a2,reinterpret_cast<U>(selection.controller_live));
        if(!observe_identity(b,live))return;
        {   // Diagnostic: which kind of component the hand or gaze is aiming at.
            const U target=selection.target_token;int type=-1;bool button{};
            if(target) {
                U vtable{};
                if(read(target,0,vtable)&&vtable==b.base+button_vt) {
                    button=true;U definition{};
                    if(!read(target,0x58,definition)||!definition||!read(definition,0xda4,type))type=-1;
                }
            }
            std::lock_guard lock(b.state_mutex);
            if(!target)b.last_target=0;
            else if(target!=b.last_target||type!=b.last_target_type) {
                b.last_target=target;b.last_target_type=type;
                if(button)++b.counters.targeted_types[type>=0&&type<7?static_cast<std::size_t>(type):7u];
                else ++b.counters.non_button_targets;
            }
        }
        const bool ray_ok=selection.completed&&selection.used_hand_ray&&!selection.nested_selection;
        if(!ray_ok || press_gate(b,live,selection.target_token)) {
            const auto latest=b.bridge->inspect();
            std::lock_guard lock(b.state_mutex);b.candidate.reset();if(latest.pinch_down)b.require_open=true;return;
        }
        const auto gesture=b.bridge->consume_selected(selection.sample.sequence);
        std::lock_guard lock(b.state_mutex);
        if(gesture.valid_ray&&!gesture.pinch_down)b.require_open=false;
        if(gesture.pinch!=PinchEvent::began || !gesture.valid_ray || !gesture.pinch_down ||
           gesture.sequence!=selection.sample.sequence || b.blocked || b.require_open || b.hold || b.pending || b.operation)return;
        HandEventCertificate c{gesture.sequence,gesture.xr_frame_id,gesture.space_revision,
            gesture.cancellation_generation,gesture.pinch_gesture_id,b.counters.scene_generation,
            gesture.published_at,live.owner,live.controller,selection.target_token,gesture.hand==InteractionHand::left?HandAction::q:HandAction::e};
        b.candidate=c;++b.counters.certified;b.cancel_requested=false;
    }catch(...) {b.fail("selection_observer_exception");}
}
struct RestoreLoop {LoopScope& scope;~RestoreLoop(){current_loop=scope.previous;}};
bool begin_transition(Backend& b,const Live& live,const char* kind,bool& observed) noexcept {
    bool held{},busy{},matching{};
    {std::lock_guard lock(b.state_mutex);
        if(live.simulation!=b.identity.simulation || live.data!=b.identity.data)return false;
        matching=live.valid && b.identity.valid && live==b.identity;
        ++b.transition_depth;held=b.hold.has_value();busy=b.operation || owned_dispatch!=nullptr;
    }
    if(matching&&b.lifecycle.begin)observed=b.lifecycle.begin(b.lifecycle.context,live.owner,live.request_context);
    reset_native_hand_buttons();b.note("lifecycle_begin",kind);
    if(busy)b.fail("lifecycle_operation_in_flight");
    else if(held) {
        if(!matching)b.fail("lifecycle_identity_mismatch");
        else {
            cleanup_hold(b,live,true);
            std::lock_guard lock(b.state_mutex);if(!b.hold)++b.counters.lifecycle_releases;
        }
    }
    return true;
}
void end_transition(Backend& b,const Live& live,bool observed,bool succeeded=true) noexcept {
    if(observed&&b.lifecycle.end)b.lifecycle.end(b.lifecycle.context,succeeded);
    {std::lock_guard lock(b.state_mutex);
        if(b.transition_depth)--b.transition_depth;
        if(b.identity.simulation==live.simulation && b.identity.data==live.data) {
            b.identity.valid=false;++b.epoch;++b.counters.scene_generation;b.candidate.reset();b.require_open=true;
        }
    }
    b.events.reset();b.note("lifecycle_end","identity_invalidated");
}
void deactivate(void* simulation,std::uint64_t opaque,void* data,const int* state) {
    auto& b=*backend;const auto caller=reinterpret_cast<U>(_ReturnAddress());Live live;bool active{},observed{};
    // Both guarded callers pass LIVE owner+358 as R9 and D as R8. The exit
    // caller runs this callback before resetting D+280 at 84C73B.
    if((caller==b.base+0x84c6f1 || caller==b.base+0x84dfb4) && reinterpret_cast<U>(state)>=0x358 &&
       reinterpret_cast<U>(data)<=std::numeric_limits<U>::max()-0x280 && reinterpret_cast<U>(simulation)<=std::numeric_limits<U>::max()-0x38) {
        const auto owner=reinterpret_cast<U>(state)-0x358;
        live=resolve(b,owner,reinterpret_cast<U>(data),reinterpret_cast<U>(data)+0x280,reinterpret_cast<U>(simulation)+0x38);
        if(live.valid && live.simulation==reinterpret_cast<U>(simulation) && value<U>(owner,0x350)==live.simulation)
            active=begin_transition(b,live,"simulation_deactivate",observed);
    }
    try {original_deactivate(simulation,opaque,data,state);}
    catch(...) {if(active){b.fail("native_deactivation_exception");end_transition(b,live,observed,false);}throw;}
    if(active)end_transition(b,live,observed);
}
U replace_character(void* world,std::uint64_t graphics,void* context,std::uint16_t player,const void* pose,std::uint64_t connection) {
    auto& b=*backend;Live live;bool active{},observed{};std::uint16_t local{};
    if(reinterpret_cast<U>(_ReturnAddress())==b.base+0x852d06 && read(reinterpret_cast<U>(context),0x338,local) && local==player) {
        if(current_loop && current_loop->known && current_loop->live.world==reinterpret_cast<U>(world) &&
           current_loop->live.request_context==reinterpret_cast<U>(context)) {
            const auto& loop_live=current_loop->live;
            live=resolve(b,loop_live.owner,loop_live.data,loop_live.request_context,loop_live.controller);
            active=begin_transition(b,live,"local_character_replace",observed);
        } else {
            bool relevant{},held{};
            {std::lock_guard lock(b.state_mutex);relevant=b.identity.world==reinterpret_cast<U>(world) && b.identity.request_context==reinterpret_cast<U>(context);held=b.hold.has_value();}
            if(relevant){reset_native_hand_buttons();if(held)b.fail("character_transition_without_live_logic");}
        }
    }
    U result{};
    try {result=original_replace(world,graphics,context,player,pose,connection);}
    catch(...) {if(active){b.fail("native_replacement_exception");end_transition(b,live,observed,false);}throw;}
    if(active)end_transition(b,live,observed);return result;
}
void loop(void* owner,std::uint64_t a2,std::uint64_t data,std::uint64_t context) {
    auto& b=*backend;const auto caller=reinterpret_cast<U>(_ReturnAddress());
    LoopScope scope{resolve(b,reinterpret_cast<U>(owner),data,context),current_loop,caller==b.base+0x84cd83};
    RestoreLoop restore{scope};current_loop=&scope;
    if(scope.known)service(b,scope.live);
    original_loop(owner,a2,data,context);
}
void short_loop(void* owner,std::uint64_t a2,std::uint64_t data,std::uint64_t context,int* a5) {
    auto& b=*backend;const auto caller=reinterpret_cast<U>(_ReturnAddress());
    LoopScope scope{resolve(b,reinterpret_cast<U>(owner),data,context),current_loop,caller==b.base+0x84cb35};
    RestoreLoop restore{scope};current_loop=&scope;
    if(scope.known)service(b,scope.live);
    original_short_loop(owner,a2,data,context,a5);
}
void dispatch(void* controller,std::uint64_t data,std::uint64_t a3,std::uint64_t context,
              std::uint64_t a5,std::uint64_t a6,void* event) {
    auto& b=*backend;const auto caller=reinterpret_cast<U>(_ReturnAddress());
    struct EventOperation {Backend& b;EventOperation(Backend& s):b(s){++b.event_operations;}~EventOperation(){--b.event_operations;}} event_operation{b};
    const bool known=current_loop&&current_loop->known&&
        (caller==b.base+0x86a76c||caller==b.base+0x86cd3d);
    const auto claim=b.events.claim_for_dispatch(reinterpret_cast<U>(event),known);
    if(claim.disposition==HandEventClaimDisposition::foreign_passthrough) {
        original_dispatch(controller,data,a3,context,a5,a6,event);return;
    }
    if(!claim.record)return;
    const auto record=*claim.record;const auto& cert=record.certificate;
    // Resolve only from this live loop's owner, never a certificate's retired
    // owner pointer when an old queued event arrives in a different context.
    const auto live=known?resolve(b,current_loop->live.owner,data,context,reinterpret_cast<U>(controller)):Live{};
    if(known)observe_identity(b,live);
    bool state_ok{};
    {std::lock_guard lock(b.state_mutex);state_ok=!b.blocked&&!b.transition_depth&&!b.cancel_requested&&!b.hold&&
        b.pending==record.token&&cert.scene_generation==b.counters.scene_generation&&live==b.identity;}
    const auto why=press_gate(b,live,cert.target_token);
    bool accepted{},called{},keypad_opened{};std::uint64_t foreign_before{};
    {std::lock_guard lock(b.state_mutex);foreign_before=b.foreign_revision;}
    if(claim.disposition==HandEventClaimDisposition::owned_evaluate&&known&&state_ok&&!why&&
       a5==live.owner+0xce5d0&&a6==live.owner+0xce600&&compatible(b.bridge->inspect(),cert)) {
        OwnedDispatch own{cert,live,false,false,owned_dispatch};owned_dispatch=&own;
        try {
            original_dispatch(controller,data,a3,context,a5,a6,event);
            called=own.action_executed;
            accepted=own.action_executed&&value<U>(live.controller,hand_held_offset(cert.action))==cert.target_token;
            const auto type=value<int>(value<U>(cert.target_token,0x58),0xda4);
            keypad_opened=keypad_opening&&own.action_executed&&(type==5||type==6)&&value<U>(live.controller,0x518)==cert.target_token;
            owned_dispatch=own.previous;
        }catch(...) {owned_dispatch=own.previous;b.events.finish(record.token,false);b.fail("native_dispatch_exception");throw;}
    }
    if(claim.disposition==HandEventClaimDisposition::owned_evaluate)b.events.finish(record.token,called);
    // Native phase1 raises the down flag before its actor/health/distance gates.
    // A rejected own call has no component hold to release. Undo only that flag
    // when no external Action20 or competing native hold can own it.
    if(called&&!accepted) {
        bool attributable{};{std::lock_guard lock(b.state_mutex);attributable=b.foreign_revision==foreign_before&&live==b.identity;}
        if(attributable&&still_live(b,live)&&zero_byte(live.controller,hand_down_offset(cert.action))==false&&
           value<U>(live.controller,0x558)==0&&value<U>(live.controller,0x560)==0&&value<U>(live.controller,0x550)==0&&
           accessible(live.controller+hand_down_offset(cert.action),1,true)) {
            const unsigned char up{};std::memcpy(reinterpret_cast<void*>(live.controller+hand_down_offset(cert.action)),&up,1);
        }
    }
    {std::lock_guard lock(b.state_mutex);
        if(b.pending==record.token)b.pending.reset();
        if(keypad_opened){++b.counters.presses_accepted;b.require_open=true;}
        else if(accepted){b.hold=Hold{cert,false};++b.counters.presses_accepted;}
        else ++b.counters.presses_rejected;
    }
    b.note(keypad_opened?"keypad_opened":accepted?"accepted":"press_rejected",why?why:(called?"native_result":"final_gate"),&cert);
    if(accepted) {
        const auto current=resolve(b,live.owner,live.data,live.request_context,live.controller);
        if(!observe_identity(b,current) || !(current==live))return;
        const auto latest=b.bridge->inspect();bool cancelled{};{std::lock_guard lock(b.state_mutex);cancelled=b.cancel_requested;}
        if(cancelled||!latest.valid_ray||!latest.pinch_down||latest.pinch_gesture_id!=cert.pinch_gesture_id)
            cleanup_hold(b,live,cancelled);
    }
}
void action(void* controller,std::uint64_t a2,std::uint64_t context,std::uint64_t a4,
            std::uint64_t a5,int id,int phase) {
    auto& b=*backend;const auto caller=reinterpret_cast<U>(_ReturnAddress());
    if(owned_dispatch && (id==19||id==20) && phase==1) {
        auto& own=*owned_dispatch;own.action_seen=true;
        const bool matches=id==static_cast<int>(own.cert.action)&&caller==b.base+0x7933b2 && reinterpret_cast<U>(controller)==own.live.controller&&
            context==own.live.request_context&&a4==own.live.owner+0xce5d0&&a5==own.live.owner+0xce600;
        if(!matches)return;
        const auto current=resolve(b,own.live.owner,own.live.data,context,reinterpret_cast<U>(controller));
        if(!observe_identity(b,current) || !(current==own.live) ||
           press_gate(b,current,own.cert.target_token)||!compatible(b.bridge->inspect(),own.cert))return;
        bool allowed{};{std::lock_guard lock(b.state_mutex);allowed=!b.transition_depth&&!b.cancel_requested&&!b.blocked&&own.cert.scene_generation==b.counters.scene_generation;}
        if(!allowed)return;
        own.action_executed=true;original_action(controller,a2,context,a4,a5,id,phase);return;
    }
    if(id==20||id==19) {
        bool held{};{std::lock_guard lock(b.state_mutex);++b.foreign_revision;held=b.hold.has_value();}
        if(held) {
            Live live;
            if(caller==b.base+0x7933b2 && a5>=0xce600 && context>=0x280) {
                live=resolve(b,a5-0xce600,context-0x280,context,reinterpret_cast<U>(controller));
                if(a4!=live.owner+0xce5d0)live.valid=false;
            }
            if(live.valid)cleanup_hold(b,live,true);else b.fail("foreign_action_context_unknown");
        }
    }
    original_action(controller,a2,context,a4,a5,id,phase);
}
void* dtor(void* event,std::uint32_t flags) {
    auto& b=*backend;
    struct EventOperation {Backend& b;EventOperation(Backend& s):b(s){++b.event_operations;}~EventOperation(){--b.event_operations;}} event_operation{b};
    const auto record=b.events.erase_before_destroy(reinterpret_cast<U>(event));
    if(record) {std::lock_guard lock(b.state_mutex);if(b.pending==record->token)b.pending.reset();}
    return original_dtor(event,flags);
}
FILE* create_log(const std::filesystem::path& path) {
    const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return nullptr;
    const int fd=_open_osfhandle(reinterpret_cast<std::intptr_t>(handle),_O_BINARY|_O_WRONLY);
    if(fd==-1){CloseHandle(handle);return nullptr;}
    auto* file=_fdopen(fd,"wb");if(!file)_close(fd);return file;
}
}
NativeHandButtonsStatus install_native_hand_buttons(U base,bool sha,const std::filesystem::path& trace,
                                                   HandInteractionBridge& bridge) noexcept {
    wchar_t env[8]{};
    if(GetEnvironmentVariableW(L"SWVR_HAND_BUTTONS",env,8)!=1||env[0]!=L'1')return NativeHandButtonsStatus::disabled;
    if(installed.load(std::memory_order_acquire))return NativeHandButtonsStatus::already_installed;
    if(!sha||!base)return NativeHandButtonsStatus::unsupported_profile;
    for(const auto& spec:specs)if(!accessible(base+spec.rva,spec.prefix.size(),false,true)||
        std::memcmp(reinterpret_cast<void*>(base+spec.rva),spec.prefix.data(),spec.prefix.size()))return NativeHandButtonsStatus::signature_mismatch;
    constexpr std::array<unsigned char,6> event_type{0xb8,0x17,0,0,0,0xc3};
    if(value<U>(base+0xbb2110,0)!=base+0x695e70 || value<U>(base+0xbb2110,8)!=base+0x37c4d0 ||
       !accessible(base+0x37c4d0,event_type.size(),false,true) ||
       std::memcmp(reinterpret_cast<void*>(base+0x37c4d0),event_type.data(),event_type.size()))return NativeHandButtonsStatus::signature_mismatch;
    std::array<unsigned char,5> replace_call{};
    if(value<U>(base+simulation_vt,0x20)!=base+0x7aca70 || !read(base+0x852d01,0,replace_call) || replace_call[0]!=0xe8)
        return NativeHandButtonsStatus::signature_mismatch;
    for(const auto site:{0x84c6eeu,0x84dfb1u}) {
        std::array<unsigned char,3> call{};
        if(!read(base+site,0,call) || call!=std::array<unsigned char,3>{0xff,0x50,0x20})return NativeHandButtonsStatus::signature_mismatch;
    }
    std::int32_t relative{};std::memcpy(&relative,replace_call.data()+1,sizeof(relative));
    if(static_cast<U>(static_cast<std::intptr_t>(0x852d06)+relative)!=0x5f2950)return NativeHandButtonsStatus::signature_mismatch;
    if(!trace.is_absolute())return NativeHandButtonsStatus::io_failure;
    std::unique_ptr<Backend> next;std::size_t created{};
    try {
        next=std::make_unique<Backend>();next->base=base;next->bridge=&bridge;
        next->bindings=std::make_unique<NativeButtonRequestBindings>(base,sha);
        next->allocate=reinterpret_cast<Allocate>(base+0x95c6f8);next->push=reinterpret_cast<Push>(base+0x236ed0);
    }catch(...) {return NativeHandButtonsStatus::bindings_failure;}
    next->log=create_log(trace.wstring()+L".hand-buttons.tsv");if(!next->log)return NativeHandButtonsStatus::io_failure;
    std::fputs("#policy\tpress20_phase1_only_dedicated_release_request_no_phase0\nseq\tthread\tkind\treason\tselected_sequence\tgesture_id\ttarget\n",next->log);
    const auto retirement=install_native_button_retirement(base,sha,trace);
    if(retirement!=NativeButtonRetirementStatus::installed&&retirement!=NativeButtonRetirementStatus::already_installed) {
        std::fclose(next->log);return NativeHandButtonsStatus::hook_failure;
    }
    const std::array<void*,7> detours{reinterpret_cast<void*>(loop),reinterpret_cast<void*>(short_loop),reinterpret_cast<void*>(dispatch),reinterpret_cast<void*>(dtor),reinterpret_cast<void*>(action),reinterpret_cast<void*>(deactivate),reinterpret_cast<void*>(replace_character)};
    const std::array<void**,7> originals{reinterpret_cast<void**>(&original_loop),reinterpret_cast<void**>(&original_short_loop),reinterpret_cast<void**>(&original_dispatch),reinterpret_cast<void**>(&original_dtor),reinterpret_cast<void**>(&original_action),reinterpret_cast<void**>(&original_deactivate),reinterpret_cast<void**>(&original_replace)};
    for(;created<specs.size();++created)if(MH_CreateHook(reinterpret_cast<void*>(base+specs[created].rva),detours[created],originals[created])!=MH_OK)break;
    if(created!=specs.size()) {
        while(created)MH_RemoveHook(reinterpret_cast<void*>(base+specs[--created].rva));
        std::fclose(next->log);return NativeHandButtonsStatus::hook_failure;
    }
    backend=next.release();installed.store(true,std::memory_order_release);
    return NativeHandButtonsStatus::installed;
}
const char* native_hand_buttons_status_name(NativeHandButtonsStatus status) noexcept {
    switch(status){
    case NativeHandButtonsStatus::disabled:return "disabled";
    case NativeHandButtonsStatus::installed:return "hooks_created";
    case NativeHandButtonsStatus::already_installed:return "already_installed";
    case NativeHandButtonsStatus::unsupported_profile:return "unsupported_profile";
    case NativeHandButtonsStatus::signature_mismatch:return "signature_mismatch";
    case NativeHandButtonsStatus::io_failure:return "io_failure";
    case NativeHandButtonsStatus::hook_failure:return "hook_failure";
    case NativeHandButtonsStatus::bindings_failure:return "bindings_failure";
    }return "unknown";
}
NativeHandRayCallbacks native_hand_buttons_callbacks() noexcept {
    return installed.load(std::memory_order_acquire)?NativeHandRayCallbacks{backend,selection_complete}:NativeHandRayCallbacks{};
}
NativeHandButtonsStats native_hand_buttons_stats() noexcept {
    NativeHandButtonsStats stats;if(!backend)return stats;
    try {auto& b=*backend;const auto events=b.events.size();
        {std::lock_guard lock(b.state_mutex);stats=b.counters;stats.pending_events=events+(b.operation?1u:0u)+(b.candidate?1u:0u)+b.event_operations.load()+b.transition_depth;
            stats.own_hold=b.hold.has_value();stats.blocked=b.blocked;}
        {std::lock_guard lock(b.log_mutex);stats.log_rows=b.rows;}
    }catch(...) {stats.blocked=true;}return stats;
}
void reset_native_hand_buttons() noexcept {
    if(!backend)return;auto& b=*backend;
    try {{std::lock_guard lock(b.state_mutex);++b.epoch;b.cancel_requested=true;b.require_open=true;b.candidate.reset();}
        b.events.reset();note_native_button_retirement_reset();b.note("reset","metadata_cancel_ownership_preserved");
    }catch(...) {b.fail("reset_exception");}
}
void flush_native_hand_buttons() noexcept {
    if(!backend)return;try{std::lock_guard lock(backend->log_mutex);if(backend->log)std::fflush(backend->log);}catch(...){}
    flush_native_button_retirement();
}
bool set_native_hand_lifecycle_callbacks(NativeInputLifecycleCallbacks callbacks) noexcept {
    if(!backend||!installed.load(std::memory_order_acquire))return false;
    if(!callbacks.begin||!callbacks.end||backend->lifecycle.begin)return false;
    backend->lifecycle=callbacks;return true;
}
void enable_native_keypad_opening(bool enabled) noexcept {keypad_opening=enabled;}
}
