#pragma once
#include "swvr/native_keypad_request.hpp"
#include <optional>
namespace swvr {
bool install_native_vehicle_keypad(std::uintptr_t image,bool full_sha) noexcept;
// Render-thread mailbox only. All native access is confined to a recognized
// frontend tick; the render thread never follows native object pointers.
std::optional<NativeKeypadSession> vehicle_keypad_poll(bool focused) noexcept;
bool vehicle_keypad_reply(std::uint64_t generation,std::optional<std::array<float,2>> values) noexcept;
struct VehicleKeypadStats {std::uint64_t opened{},submitted{},cancelled{},rejected{},faults{};};
VehicleKeypadStats vehicle_keypad_stats() noexcept;
}
