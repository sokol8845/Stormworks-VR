#pragma once
#include <cstdint>
#include <filesystem>

namespace swvr {
enum class InteractionProbeStatus {
    disabled, installed, already_installed, unsupported_profile,
    signature_mismatch, io_failure, hook_failure
};
// Call once outside DllMain, after MH_Initialize and before enabling any hooks.
// The caller must have compared the executable's SHA to game_profile::sha256.
// SWVR_INTERACTION_PROBE=1 opts in. Writes trace_path + ".interaction.tsv",
// refusing to replace an existing file. Creates disabled hooks only; no global
// MinHook lifecycle operations. Keep this module loaded for process lifetime.
InteractionProbeStatus install_interaction_probe(std::uintptr_t exe_base,
    bool sha_matched,const std::filesystem::path& trace_path) noexcept;
const char* interaction_probe_status_name(InteractionProbeStatus status) noexcept;
void flush_interaction_probe() noexcept;
}
