#pragma once
#include "swvr/hand_interaction.hpp"
#include <cstdint>
#include <filesystem>

namespace swvr {
enum class NativeHandRayStatus {
    disabled, installed, already_installed, unsupported_profile,
    signature_mismatch, io_failure, hook_failure
};
struct NativeHandSelection {
    void* controller_live{};
    std::uint64_t a2{},a3{},a4{},a5{};
    const int* a6_live{};
    HandInteractionSnapshot sample{}; // The snapshot bound at Selection entry.
    std::uintptr_t target_token{}; // Value from live C+520; zero if incomplete.
    bool used_hand_ray{},completed{};
    bool nested_selection{}; // Inner Selection may have replaced the final target.
    unsigned origin_calls{};
};
struct NativeHandRayCallbacks {
    void* context{};
    void (*selection_complete)(void*,const NativeHandSelection&) noexcept{};
};
// Opt-in SWVR_HAND_RAY=1. Creates TWO DISABLED hooks, never enables MinHook or
// dispatches any button/event/action. Parent must choose this OR interaction_probe.
// Call after MH_Initialize, before enabling hooks, outside DllMain. The SHA bool
// must describe the current executable profile; base must be its valid mapping.
// Bridge must outlive enabled hooks and use the default 100 ms freshness policy.
// Optional observer runs only for known top-level Selection, after forward has
// been restored and while arguments are still live. Fallback also reports with
// used_hand_ray=false; a native exception reports completed=false/target_token=0.
// Nested Selection is marked separately; the bound sample is never substituted.
// The callback
// must not throw; its context/function must outlive enabled hooks. Native live
// pointers must not be kept for later dereferencing. No gesture is consumed by
// this module. An already-installed result does not replace its callbacks.
// Writes a new trace_path + ".hand-ray-native.tsv"; never replaces an existing log.
NativeHandRayStatus install_native_hand_ray(std::uintptr_t exe_base,bool sha_matched,
    const std::filesystem::path& trace_path,HandInteractionBridge& bridge,
    NativeHandRayCallbacks callbacks={}) noexcept;
const char* native_hand_ray_status_name(NativeHandRayStatus status) noexcept;
void flush_native_hand_ray() noexcept;
// Append a new diagnostic epoch with a fresh 10000-row allowance; keeps seq
// monotonic and never changes pointing, snapshots or controller state. Returns
// false before installation or after an I/O failure; does not revive failed I/O.
bool rearm_native_hand_ray_log() noexcept;
}
