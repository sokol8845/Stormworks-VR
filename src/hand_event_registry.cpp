#include "swvr/hand_event_registry.hpp"
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace swvr {
struct HandEventRegistry::Impl {
    mutable std::mutex mutex;
    std::vector<HandEventRecord> slots;
    std::size_t occupied{};
    std::uint64_t next_id{1},generation{1};
    bool exhausted{};
    explicit Impl(std::size_t capacity):slots(capacity) {
        if(!capacity)throw std::invalid_argument("Hand event registry capacity must be positive");
    }
    HandEventRecord* by_address(std::uintptr_t address) {
        if(!address)return nullptr;
        for(auto& entry:slots)if(entry.token.address==address)return &entry;
        return nullptr;
    }
    HandEventRecord* by_token(HandEventToken token) {
        const auto entry=by_address(token.address);
        return entry && entry->token==token?entry:nullptr;
    }
    std::optional<HandEventRecord> erase(HandEventRecord* entry) {
        if(!entry)return {};
        const auto result=*entry;*entry={};--occupied;return result;
    }
};

HandEventRegistry::HandEventRegistry(std::size_t capacity):impl_(std::make_unique<Impl>(capacity)){}
HandEventRegistry::~HandEventRegistry()=default;
HandEventRegistration HandEventRegistry::register_event(std::uintptr_t address,HandEventPhase phase,
                                                       const HandEventCertificate& certificate) {
    auto& state=*impl_;std::lock_guard lock(state.mutex);
    if(!address)return {HandEventRegistrationStatus::invalid_address,{}};
    if(phase!=HandEventPhase::press && phase!=HandEventPhase::release)
        return {HandEventRegistrationStatus::invalid_phase,{}};
    if(state.by_address(address))return {HandEventRegistrationStatus::address_in_use,{}};
    if(state.exhausted)return {HandEventRegistrationStatus::generation_exhausted,{}};
    if(state.occupied==state.slots.size())return {HandEventRegistrationStatus::capacity_reached,{}};
    for(auto& entry:state.slots)if(!entry.token.address) {
        entry={};entry.token={address,state.next_id,state.generation};
        entry.phase=phase;entry.certificate=certificate;++state.occupied;
        if(state.next_id==std::numeric_limits<std::uint64_t>::max())state.exhausted=true;
        else ++state.next_id;
        return {HandEventRegistrationStatus::registered,entry.token};
    }
    // The fixed storage invariant leaves no other capacity path.
    return {HandEventRegistrationStatus::capacity_reached,{}};
}
bool HandEventRegistry::mark_submitted(HandEventToken token) {
    auto& state=*impl_;std::lock_guard lock(state.mutex);const auto entry=state.by_token(token);
    if(!entry || entry->cancelled || entry->dispatch!=HandEventDispatch::pending ||
       entry->ownership!=HandEventOwnership::allocated)return false;
    entry->ownership=HandEventOwnership::submitted;return true;
}
bool HandEventRegistry::mark_uncertain(HandEventToken token) {
    auto& state=*impl_;std::lock_guard lock(state.mutex);const auto entry=state.by_token(token);
    if(!entry)return false;
    // Preserve dispatch/cancellation evidence even if the push outcome became
    // uncertain only after synchronous consumption or a concurrent reset.
    entry->ownership=HandEventOwnership::uncertain;return true;
}
HandEventClaim HandEventRegistry::claim_for_dispatch(std::uintptr_t address,bool known_caller) {
    auto& state=*impl_;std::lock_guard lock(state.mutex);const auto entry=state.by_address(address);
    if(!entry)return {};
    HandEventClaim result;result.disposition=HandEventClaimDisposition::owned_suppress;
    if(entry->dispatch!=HandEventDispatch::pending)result.reason=HandEventClaimReason::duplicate_dispatch;
    else {
        if(!known_caller)result.reason=HandEventClaimReason::unknown_caller;
        else if(entry->cancelled)result.reason=HandEventClaimReason::cancelled;
        else if(entry->ownership==HandEventOwnership::allocated)result.reason=HandEventClaimReason::not_submitted;
        else if(entry->ownership==HandEventOwnership::uncertain)result.reason=HandEventClaimReason::uncertain_ownership;
        else {
            entry->dispatch=HandEventDispatch::claimed;
            result.disposition=HandEventClaimDisposition::owned_evaluate;
            result.reason=HandEventClaimReason::eligible_for_final_gate;
        }
        if(result.disposition==HandEventClaimDisposition::owned_suppress)
            entry->dispatch=HandEventDispatch::suppressed;
    }
    result.record=*entry;return result;
}
bool HandEventRegistry::finish(HandEventToken token,bool executed) {
    auto& state=*impl_;std::lock_guard lock(state.mutex);const auto entry=state.by_token(token);
    if(!entry || entry->dispatch!=HandEventDispatch::claimed)return false;
    entry->dispatch=executed?HandEventDispatch::executed:HandEventDispatch::not_executed;
    return true;
}
std::uint64_t HandEventRegistry::reset() {
    auto& state=*impl_;std::lock_guard lock(state.mutex);
    if(state.generation==std::numeric_limits<std::uint64_t>::max())state.exhausted=true;
    else ++state.generation;
    for(auto& entry:state.slots)if(entry.token.address) {
        entry.cancelled=true;entry.cancelled_at_generation=state.generation;
    }
    return state.generation;
}
std::optional<HandEventRecord> HandEventRegistry::find(std::uintptr_t address) const {
    auto& state=*impl_;std::lock_guard lock(state.mutex);const auto entry=state.by_address(address);
    return entry?std::optional<HandEventRecord>{*entry}:std::nullopt;
}
std::optional<HandEventRecord> HandEventRegistry::erase_before_destroy(std::uintptr_t address) {
    auto& state=*impl_;std::lock_guard lock(state.mutex);return state.erase(state.by_address(address));
}
std::optional<HandEventRecord> HandEventRegistry::erase_before_destroy(HandEventToken token) {
    auto& state=*impl_;std::lock_guard lock(state.mutex);return state.erase(state.by_token(token));
}
std::size_t HandEventRegistry::size() const {
    auto& state=*impl_;std::lock_guard lock(state.mutex);return state.occupied;
}
std::size_t HandEventRegistry::capacity() const noexcept {return impl_->slots.size();}
std::uint64_t HandEventRegistry::generation() const {
    auto& state=*impl_;std::lock_guard lock(state.mutex);return state.generation;
}
}
