#pragma once
#include "swvr/hand_event_registry.hpp"
#include "swvr/native_button_request.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace swvr {
// Caller must supply a recognized, currently LIVE native logic scope. Tokens
// establish equality only; setting this flag does not create native lifetimes.
struct NativeButtonCaptureScope {
    std::uint64_t scene_generation{};
    std::uintptr_t owner_token{};
    void* controller_live{};
    bool recognized_live_scope{};
};
struct NativeButtonCapturedRelease {
    std::uint32_t vehicle_id{},section_index{};
    std::array<std::int32_t,3> component_grid{};
    std::array<float,3> saved_local_hit{};
    std::string payload;
    // Borrowed view into this owned value; the builder immediately makes its own
    // native copy. Choosing skip is the caller's policy, not capture permission.
    NativeButtonReleaseData release_data(bool skip_on_button_press) const noexcept {
        return {vehicle_id,section_index,component_grid,saved_local_hit,skip_on_button_press,payload};
    }
};
enum class NativeButtonCaptureStatus {
    captured, unrecognized_scope, context_mismatch, held_mismatch, unreadable_native_memory,
    wrong_button_class, unsupported_button_type, invalid_ring, section_not_found,
    nonfinite_saved_hit, getter_failed, getter_return_mismatch, invalid_payload,
    payload_copy_failed, held_changed, payload_cleanup_failed
};
struct NativeButtonCaptureResult {
    NativeButtonCaptureStatus status{NativeButtonCaptureStatus::unrecognized_scope};
    NativeButtonCapturedRelease data;
    explicit operator bool() const noexcept {return status==NativeButtonCaptureStatus::captured;}
};
const char* native_button_capture_status_name(NativeButtonCaptureStatus status) noexcept;
}
