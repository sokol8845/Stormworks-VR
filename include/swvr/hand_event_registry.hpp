#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace swvr {

enum class HandEventPhase : unsigned { release=0, press=1 };
enum class HandAction : int { q=19,e=20 };

// Values copied while the relevant native context is live. Pointer-sized tokens
// are identities for comparison only; the registry never dereferences them.
struct HandEventCertificate {
    std::uint64_t selected_sequence{},xr_frame_id{},space_revision{};
    std::uint64_t cancellation_generation{},pinch_gesture_id{},scene_generation{};
    std::chrono::steady_clock::time_point selected_at{};
    std::uintptr_t owner_token{},controller_token{},target_token{};
    HandAction action{HandAction::e};
    bool operator==(const HandEventCertificate&) const=default;
};
// Measured Action19/20 fields in the current executable (78C7A0).
constexpr unsigned hand_held_offset(HandAction action){return action==HandAction::q?0x560u:0x558u;}
constexpr unsigned hand_down_offset(HandAction action){return action==HandAction::q?0x4c0u:0x4bfu;}
constexpr unsigned hand_hit_offset(HandAction action){return action==HandAction::q?0x574u:0x568u;}
struct HandEventToken {
    std::uintptr_t address{};
    std::uint64_t event_id{},registration_generation{};
    bool operator==(const HandEventToken&) const=default;
};
enum class HandEventOwnership { allocated, submitted, uncertain };
enum class HandEventDispatch { pending, claimed, suppressed, executed, not_executed };
struct HandEventRecord {
    HandEventToken token{};
    HandEventPhase phase{HandEventPhase::release};
    HandEventCertificate certificate{};
    HandEventOwnership ownership{HandEventOwnership::allocated};
    HandEventDispatch dispatch{HandEventDispatch::pending};
    bool cancelled{};
    std::uint64_t cancelled_at_generation{};
};
enum class HandEventRegistrationStatus {
    registered, invalid_address, invalid_phase, address_in_use, capacity_reached,
    generation_exhausted
};
struct HandEventRegistration {
    HandEventRegistrationStatus status{HandEventRegistrationStatus::invalid_address};
    HandEventToken token{};
    explicit operator bool() const noexcept {return status==HandEventRegistrationStatus::registered;}
};
enum class HandEventClaimDisposition { foreign_passthrough, owned_evaluate, owned_suppress };
enum class HandEventClaimReason {
    foreign_event, eligible_for_final_gate, cancelled, not_submitted,
    uncertain_ownership, unknown_caller, duplicate_dispatch
};
struct HandEventClaim {
    HandEventClaimDisposition disposition{HandEventClaimDisposition::foreign_passthrough};
    HandEventClaimReason reason{HandEventClaimReason::foreign_event};
    std::optional<HandEventRecord> record;
};

// Metadata ledger only: no allocation/destruction of native events, callbacks,
// native memory reads/writes, or authorization of the final interaction gate.
// Fixed slot storage is allocated by the constructor; records are retained until
// erase_before_destroy, including after reset and unsuccessful dispatch.
class HandEventRegistry {
public:
    explicit HandEventRegistry(std::size_t capacity=128);
    ~HandEventRegistry();
    HandEventRegistry(const HandEventRegistry&)=delete;
    HandEventRegistry& operator=(const HandEventRegistry&)=delete;

    HandEventRegistration register_event(std::uintptr_t address,HandEventPhase phase,
                                         const HandEventCertificate& certificate);
    // Call immediately BEFORE handing the event to native push. If native push
    // fails without a proven ownership outcome, retain it and mark_uncertain.
    bool mark_submitted(HandEventToken token);
    bool mark_uncertain(HandEventToken token);
    HandEventClaim claim_for_dispatch(std::uintptr_t address,bool known_caller);
    // Only a prior owned_evaluate claim may finish. This records actual execution
    // even if reset cancelled the claim while its native call was in progress.
    bool finish(HandEventToken token,bool executed);
    // Advance the ledger generation and cancel every existing record. Tombstones
    // remain owned until destruction; capacity exhaustion never evicts a record.
    std::uint64_t reset();
    std::optional<HandEventRecord> find(std::uintptr_t address) const;
    // Address form is for the immediate LIVE native destructor hook only. Use the
    // token form for deferred work, so a stale id cannot erase an address reuse.
    std::optional<HandEventRecord> erase_before_destroy(std::uintptr_t address);
    std::optional<HandEventRecord> erase_before_destroy(HandEventToken token);
    std::size_t size() const;
    std::size_t capacity() const noexcept;
    std::uint64_t generation() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
