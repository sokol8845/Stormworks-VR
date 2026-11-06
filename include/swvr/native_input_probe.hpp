#pragma once
#include <cstdint>
#include <filesystem>

namespace swvr {
enum class NativeInputProbeStatus {
    disabled, installed, already_installed, unsupported_profile,
    signature_mismatch, io_failure, hook_failure
};
// Read-only SWVR_INPUT_PROBE=1 diagnostics. Call outside DllMain, after
// MH_Initialize and before enabling hooks. sha_matched must identify the current
// executable mapping. Creates only DISABLED pop/dispatch hooks; no allocator,
// queue push, input emission, global hook lifecycle, or native memory writes.
// Output path must be absolute; creates path + ".input-native.tsv" exclusively.
// Keep module loaded while hooks can run. Can coexist with native_hand_ray.
NativeInputProbeStatus install_native_input_probe(std::uintptr_t exe_base,
    bool sha_matched,const std::filesystem::path& trace_path) noexcept;
const char* native_input_probe_status_name(NativeInputProbeStatus status) noexcept;
void flush_native_input_probe() noexcept;
// Append another 10000-row epoch, preserving seq and previous records. No
// tracking/input state changes. False before install or after an I/O failure.
bool rearm_native_input_probe_log() noexcept;
}
