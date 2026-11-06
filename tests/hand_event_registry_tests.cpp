#include "swvr/hand_event_registry.hpp"
#include <array>
#include <atomic>
#include <barrier>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace swvr;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
HandEventCertificate certificate(std::uint64_t sequence=41) {
    return {sequence,53,7,11,13,17,std::chrono::steady_clock::time_point{std::chrono::milliseconds(123)},
            0x1000,0x2000,0x3000};
}
HandEventToken registered(HandEventRegistry& registry,std::uintptr_t address,
                          HandEventPhase phase=HandEventPhase::press) {
    const auto result=registry.register_event(address,phase,certificate());
    require(static_cast<bool>(result),"registration failed");return result.token;
}
void basic_gate_and_immutable_certificate() {
    HandEventRegistry registry;
    require(registry.capacity()==128 && registry.size()==0 && registry.generation()==1,"initial registry state");
    auto input=certificate();
    const auto reg=registry.register_event(0x1234,HandEventPhase::press,input);
    require(static_cast<bool>(reg) && reg.token.event_id && reg.token.registration_generation==1,"registration token");
    input.target_token=0x9999;input.selected_sequence=500;
    require(registry.mark_submitted(reg.token),"submit before native handoff");
    require(!registry.mark_submitted(reg.token),"duplicate submission allowed");
    const auto claim=registry.claim_for_dispatch(0x1234,true);
    require(claim.disposition==HandEventClaimDisposition::owned_evaluate && claim.record.has_value(),"submitted event was not claimed");
    require(claim.reason==HandEventClaimReason::eligible_for_final_gate,"claim must require final gate");
    require(claim.record->certificate==certificate(),"certificate aliased caller storage");
    auto diagnostic=*registry.find(0x1234);diagnostic.certificate.target_token=0;
    require(registry.find(0x1234)->certificate.target_token==0x3000,"diagnostic record mutated registry");
    require(registry.finish(reg.token,false),"final gate rejection was not recorded");
    require(registry.find(0x1234)->dispatch==HandEventDispatch::not_executed,"rejected final gate marked executed");
    require(!registry.finish(reg.token,true),"duplicate finish overwrote result");
    const auto duplicate=registry.claim_for_dispatch(0x1234,true);
    require(duplicate.disposition==HandEventClaimDisposition::owned_suppress &&
            duplicate.reason==HandEventClaimReason::duplicate_dispatch,"duplicate claim was replayed");
    const auto erased=registry.erase_before_destroy(reg.token);
    require(erased && erased->dispatch==HandEventDispatch::not_executed && registry.size()==0,"destructor metadata removal");
}
void foreign_unknown_allocated_and_uncertain() {
    HandEventRegistry registry(8);
    for(const bool known:{false,true}) {
        const auto foreign=registry.claim_for_dispatch(0x9000,known);
        require(foreign.disposition==HandEventClaimDisposition::foreign_passthrough && !foreign.record,"physical event was intercepted");
    }
    const auto allocated=registered(registry,0x1000);
    const auto alloc_claim=registry.claim_for_dispatch(0x1000,true);
    require(alloc_claim.disposition==HandEventClaimDisposition::owned_suppress &&
            alloc_claim.reason==HandEventClaimReason::not_submitted,"unsubmitted allocation was dispatched");
    require(!registry.mark_submitted(allocated),"suppressed allocation was revived");
    const auto uncertain=registered(registry,0x2000);
    require(registry.mark_submitted(uncertain) && registry.mark_uncertain(uncertain),"uncertain handoff transition");
    const auto uncertain_claim=registry.claim_for_dispatch(0x2000,true);
    require(uncertain_claim.disposition==HandEventClaimDisposition::owned_suppress &&
            uncertain_claim.reason==HandEventClaimReason::uncertain_ownership,"uncertain event escaped gate");
    const auto unknown=registered(registry,0x3000,HandEventPhase::release);
    require(registry.mark_submitted(unknown),"release submit");
    const auto unknown_claim=registry.claim_for_dispatch(0x3000,false);
    require(unknown_claim.disposition==HandEventClaimDisposition::owned_suppress &&
            unknown_claim.reason==HandEventClaimReason::unknown_caller,"owned event became foreign on unknown caller");
    require(registry.claim_for_dispatch(0x3000,true).disposition==HandEventClaimDisposition::owned_suppress,
            "unknown-caller suppression was replayed later");
    require(!registry.finish(unknown,true),"unclaimed owned event marked executed");
}
void reset_keeps_tombstones_and_capacity() {
    HandEventRegistry registry(2);
    const auto a=registered(registry,0x10),b=registered(registry,0x20);
    require(registry.mark_submitted(a) && registry.mark_submitted(b),"submit capacity fixture");
    const auto generation=registry.reset();
    require(generation>a.registration_generation && registry.size()==2,"reset erased queued records");
    require(registry.register_event(0x30,HandEventPhase::press,certificate()).status==
            HandEventRegistrationStatus::capacity_reached,"capacity evicted a tombstone");
    require(registry.register_event(0x10,HandEventPhase::press,certificate()).status==
            HandEventRegistrationStatus::address_in_use,"reset allowed address to be registered twice");
    for(const auto address:{std::uintptr_t{0x10},std::uintptr_t{0x20}}) {
        const auto result=registry.claim_for_dispatch(address,true);
        require(result.disposition==HandEventClaimDisposition::owned_suppress &&
                result.reason==HandEventClaimReason::cancelled,"cancelled event became physical input");
        require(result.record->cancelled_at_generation==generation,"reset generation missing");
    }
    require(registry.erase_before_destroy(0x10).has_value(),"first teardown deletion");
    const auto replacement=registered(registry,0x30);
    require(replacement.registration_generation==generation && replacement.event_id>b.event_id,"new generation registration token");
    require(registry.erase_before_destroy(0x20) && registry.erase_before_destroy(0x30),"remaining teardown deletions");
    require(registry.size()==0,"queue teardown retained metadata");
}
void pointer_reuse_rejects_stale_callbacks() {
    HandEventRegistry registry(1);
    const auto old=registered(registry,0x4567);
    require(registry.mark_submitted(old),"old submit");
    require(registry.claim_for_dispatch(old.address,true).disposition==HandEventClaimDisposition::owned_evaluate,"old claim");
    require(registry.erase_before_destroy(old).has_value(),"live destructor failed");
    const auto next=registered(registry,old.address,HandEventPhase::release);
    require(next.event_id>old.event_id,"address reuse replayed old event id");
    require(!registry.mark_submitted(old) && !registry.mark_uncertain(old) && !registry.finish(old,true) &&
            !registry.erase_before_destroy(old),"stale callback changed reused address");
    require(registry.mark_submitted(next),"new object was corrupted by stale callback");
    const auto claim=registry.claim_for_dispatch(next.address,true);
    require(claim.disposition==HandEventClaimDisposition::owned_evaluate && claim.record->phase==HandEventPhase::release,
            "new release certificate missing");
    require(registry.finish(next,true),"new object finish");
    require(registry.find(next.address)->dispatch==HandEventDispatch::executed,"new execution result missing");
}
void reset_while_native_call_is_claimed() {
    HandEventRegistry registry;
    const auto token=registered(registry,0x123);
    require(registry.mark_submitted(token),"claimed-reset submit");
    const auto claim=registry.claim_for_dispatch(token.address,true);
    require(claim.disposition==HandEventClaimDisposition::owned_evaluate,"claimed-reset claim");
    const auto generation=registry.reset();
    require(registry.mark_uncertain(token),"uncertain outcome after reset was not retained");
    const auto in_flight=*registry.find(token.address);
    require(in_flight.cancelled && in_flight.dispatch==HandEventDispatch::claimed &&
            in_flight.ownership==HandEventOwnership::uncertain,"uncertain-after-reset lost in-flight evidence");
    require(registry.finish(token,true),"reset hid an actual native execution");
    const auto result=*registry.find(token.address);
    require(result.dispatch==HandEventDispatch::executed && result.cancelled &&
            result.cancelled_at_generation==generation,"execution-after-reset evidence was lost");
    require(registry.claim_for_dispatch(token.address,true).disposition==HandEventClaimDisposition::owned_suppress,
            "executed cancelled record was claimed again");
    require(registry.mark_uncertain(token),"late uncertain outcome was not retained");
    const auto late=*registry.find(token.address);
    require(late.dispatch==HandEventDispatch::executed && late.cancelled &&
            late.cancelled_at_generation==generation && late.ownership==HandEventOwnership::uncertain,
            "late ownership report erased execution or cancellation evidence");
}
void concurrent_claim_is_exactly_once() {
    HandEventRegistry registry;
    const auto token=registered(registry,0x7777);
    require(registry.mark_submitted(token),"race submit");
    std::barrier begin(3);
    std::array<HandEventClaim,2> claims;
    std::array<std::thread,2> workers;
    for(unsigned i=0;i<2;++i)workers[i]=std::thread([&,i] {
        begin.arrive_and_wait();claims[i]=registry.claim_for_dispatch(token.address,true);
    });
    begin.arrive_and_wait();for(auto& worker:workers)worker.join();
    unsigned evaluations{},suppressed{};
    for(const auto& claim:claims) {
        evaluations+=claim.disposition==HandEventClaimDisposition::owned_evaluate;
        suppressed+=claim.disposition==HandEventClaimDisposition::owned_suppress;
    }
    require(evaluations==1 && suppressed==1,"two logical consumers claimed the same native event");
    require(registry.finish(token,true),"winner could not finish after race");
}

// This fixture models the exact ownership pattern documented for 694200:
// successful pop destroys previous, empty pop preserves last, consumer destroys
// last at exit, queue teardown destroys every pending event.
struct FakeEvent {std::array<unsigned char,16> bytes;unsigned destroyed{};};
struct FakeFifo {
    HandEventRegistry& registry;
    std::deque<FakeEvent*> pending;
    FakeEvent* current{};
    explicit FakeFifo(HandEventRegistry& r):registry(r){}
    void destroy(FakeEvent* event) {
        require(event && !event->destroyed,"native event destroyed twice");
        registry.erase_before_destroy(reinterpret_cast<std::uintptr_t>(event));
        ++event->destroyed; // Native destructor would run here, after registry unlock.
        (void)registry.size(); // Re-entry must remain legal outside the API call.
    }
    bool pop() {
        if(pending.empty())return false;
        if(current)destroy(current);
        current=pending.front();pending.pop_front();return true;
    }
    void close() {
        if(current){destroy(current);current=nullptr;}
        while(!pending.empty()){destroy(pending.front());pending.pop_front();}
    }
};
void fifo_physical_owned_cancel_and_teardown() {
    HandEventRegistry registry(3);FakeFifo fifo(registry);
    FakeEvent physical_before{{0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,20,0,0,0,1,0,0,0}};
    FakeEvent owned_press{{0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,20,0,0,0,1,0,0,0}};
    FakeEvent physical_after{{0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,20,0,0,0,0,0,0,0}};
    FakeEvent owned_waiting{{0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,20,0,0,0,0,0,0,0}};
    const std::array saved{physical_before.bytes,owned_press.bytes,physical_after.bytes,owned_waiting.bytes};
    const auto press=registered(registry,reinterpret_cast<std::uintptr_t>(&owned_press));
    const auto waiting=registered(registry,reinterpret_cast<std::uintptr_t>(&owned_waiting),HandEventPhase::release);
    require(registry.mark_submitted(press) && registry.mark_submitted(waiting),"FIFO submit");
    fifo.pending={&physical_before,&owned_press,&physical_after};
    require(fifo.pop() && fifo.current==&physical_before,"physical FIFO order before own event");
    require(registry.claim_for_dispatch(reinterpret_cast<std::uintptr_t>(fifo.current),true).disposition==
            HandEventClaimDisposition::foreign_passthrough,"physical-before filtered");
    registry.reset(); // Mode/focus cancellation arrives before own queued press.
    require(fifo.pop() && fifo.current==&owned_press && physical_before.destroyed==1,"successful pop must destroy previous");
    const auto claim=registry.claim_for_dispatch(reinterpret_cast<std::uintptr_t>(fifo.current),true);
    require(claim.disposition==HandEventClaimDisposition::owned_suppress && claim.reason==HandEventClaimReason::cancelled,
            "cancelled queued press executed as physical input");
    require(fifo.pop() && fifo.current==&physical_after && owned_press.destroyed==1,"own event destruction/order");
    require(registry.claim_for_dispatch(reinterpret_cast<std::uintptr_t>(fifo.current),false).disposition==
            HandEventClaimDisposition::foreign_passthrough,"physical-after filtered by cancellation");
    require(!fifo.pop() && fifo.current==&physical_after && physical_after.destroyed==0,"empty pop destroyed or cleared last event");
    fifo.pending.push_back(&owned_waiting); // Still queued when the owner is destroyed.
    fifo.close();
    require(physical_after.destroyed==1 && owned_waiting.destroyed==1 && registry.size()==0,"native queue teardown lost owned event metadata");
    const std::array actual{physical_before.bytes,owned_press.bytes,physical_after.bytes,owned_waiting.bytes};
    require(actual==saved,"registry changed native event payload bytes");
}
void invalid_registration_has_no_side_effect() {
    bool rejected{};try {HandEventRegistry registry(0);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"zero capacity accepted");
    HandEventRegistry registry(1);
    require(registry.register_event(0,HandEventPhase::press,certificate()).status==HandEventRegistrationStatus::invalid_address,
            "null native event address accepted");
    require(registry.register_event(1,static_cast<HandEventPhase>(2),certificate()).status==HandEventRegistrationStatus::invalid_phase,
            "unsupported native phase accepted");
    const auto first=registered(registry,1);
    require(first.event_id==1 && registry.size()==1,"failed registrations consumed capacity or ids");
    require(!registry.erase_before_destroy(0) && registry.size()==1,"foreign destructor changed registry");
}
}
int main() {
    try {
        basic_gate_and_immutable_certificate();foreign_unknown_allocated_and_uncertain();
        reset_keeps_tombstones_and_capacity();pointer_reuse_rejects_stale_callbacks();
        reset_while_native_call_is_claimed();concurrent_claim_is_exactly_once();
        fifo_physical_owned_cancel_and_teardown();invalid_registration_has_no_side_effect();
        std::cout<<"hand event registry tests passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
