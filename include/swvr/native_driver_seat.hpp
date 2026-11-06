#pragma once
#include "swvr/driver_wheel.hpp"
#include <chrono>

namespace swvr {
struct DriverSeatSnapshot {
    std::uintptr_t seat{};
    Matrix4d world{};
    std::chrono::steady_clock::time_point observed{};
    bool valid{};
};
// Disabled hook; the injector owns MinHook initialization and activation.
bool install_native_driver_seat(std::uintptr_t base,bool full_sha) noexcept;
DriverSeatSnapshot native_driver_seat(std::uintptr_t local_seat) noexcept;
}
