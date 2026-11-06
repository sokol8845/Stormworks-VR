#pragma once
#include "swvr/hand_locomotion.hpp"
#include "swvr/native_input_lifecycle.hpp"
#include "swvr/hand_steering.hpp"
#include <filesystem>

namespace swvr {
// Creates a disabled hook; the host owns MH_Initialize/EnableHook and lifetime.
bool install_native_locomotion(std::uintptr_t base,bool full_sha,HandLocomotion&,bool shared_lifecycle=false) noexcept;
// Metadata only. Serializes with request handoff, then inhibits source frames.
// Caller must keep paused until stats.holding==false; no native helper here.
bool pause_native_locomotion(bool paused) noexcept;
// Read-only revalidation of the last verified local actor and native UI gates.
bool native_locomotion_allows_turn() noexcept;
struct NativeLocomotionStats {std::uint64_t packets{},walk{},sprint{},releases{},faults{},lifecycle_releases{},steering_packets{},steering_releases{},steering_nonzero_packets{};float steering_max_axis{};bool holding{};const char* last_fault{"none"};};
NativeLocomotionStats native_locomotion_stats() noexcept;
NativeInputLifecycleCallbacks native_locomotion_lifecycle_callbacks() noexcept;
bool enable_native_steering(HandSteering&) noexcept;
std::uintptr_t native_steering_seat() noexcept; // Fresh, copied identity token only.
}
