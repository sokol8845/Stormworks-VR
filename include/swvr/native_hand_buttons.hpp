#pragma once
#include "swvr/native_hand_ray.hpp"
#include "swvr/native_input_lifecycle.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace swvr {
enum class NativeHandButtonsStatus {
    disabled, installed, already_installed, unsupported_profile,
    signature_mismatch, io_failure, hook_failure, bindings_failure
};
struct NativeHandButtonsStats {
    std::uint64_t selections{},certified{},events_queued{},presses_accepted{},
        presses_rejected{},release_requests{},faults{},scene_generation{},log_rows{};
    std::size_t pending_events{};
    bool own_hold{},blocked{};
    // Static diagnostic string for the first unresolved fault; "none" while
    // healthy. A blocked hold is retained metadata, not permission to retry.
    const char* last_fault{"none"};
    std::uint64_t lifecycle_releases{};
    // Button types (0..6, 7=other) of newly selected Button targets, counted
    // once per target change; non_button_targets counts other components.
    std::array<std::uint64_t,8> targeted_types{};
    std::uint64_t non_button_targets{};
};
// Opt-in SWVR_HAND_BUTTONS=1. Creates disabled hooks after MH_Initialize, before
// the host enables hooks. No launch, native input, or helper calls on install.
// Bridge/callbacks/bindings metadata must outlive all enabled hooks. The full-SHA
// flag must describe this exact EXE mapping; entry prefixes are checked too.
NativeHandButtonsStatus install_native_hand_buttons(std::uintptr_t exe_base,
    bool full_sha_matched,const std::filesystem::path& trace_path,
    HandInteractionBridge& bridge) noexcept;
const char* native_hand_buttons_status_name(NativeHandButtonsStatus) noexcept;
// Pass to install_native_hand_ray after successful buttons installation.
NativeHandRayCallbacks native_hand_buttons_callbacks() noexcept;
NativeHandButtonsStats native_hand_buttons_stats() noexcept;
// Metadata-only cancellation. Pending event tombstones, own hold, ownership
// records and uncertainty survive until a proven live native cleanup path.
void reset_native_hand_buttons() noexcept;
void flush_native_hand_buttons() noexcept;
// Register after installation and before the host enables any hooks.
bool set_native_hand_lifecycle_callbacks(NativeInputLifecycleCallbacks) noexcept;
void enable_native_keypad_opening(bool) noexcept;
}
