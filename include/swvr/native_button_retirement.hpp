#pragma once
#include "swvr/native_button_request.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace swvr {
enum class NativeButtonRetirementStatus {
    disabled, installed, already_installed, unsupported_profile, signature_mismatch,
    io_failure, hook_failure
};
struct NativeButtonRetirementStats {
    std::uint64_t registrations{},write_retired{},localhost_retired{},quarantined{};
    std::uint64_t unknown_deletes{},reset_generation{},log_rows{};
    std::uint64_t source_reset_retired{},source_destroy_retired{};
    // retained_records==0 is idle. live_records excludes quarantines and is
    // therefore insufficient to declare all ownership resolved. Completed
    // records are recycled; unresolved quarantine is retained by allocation ID.
    std::size_t live_records{},retained_records{},capacity{};
};
// Opt-in SWVR_HAND_BUTTONS=1. Creates disabled hooks after MH_Initialize and
// before the host's MH_EnableHook. Never enables/disables hooks, starts the game,
// or calls native helpers during installation/global destruction. Full-SHA bool
// must describe this exact executable mapping. Failed creation is rolled back.
// trace_path is absolute; a new .button-retirement.tsv file is created.
NativeButtonRetirementStatus install_native_button_retirement(
    std::uintptr_t exe_base,bool full_sha_matched,const std::filesystem::path& trace_path) noexcept;
const char* native_button_retirement_status_name(NativeButtonRetirementStatus status) noexcept;
// Construct the wrapper after successful installation, before the host enables
// every required hook. Only USING its callbacks requires all hooks to be enabled.
// Source context must outlive its wrapper and all operations using that API.
// Returned context is process-lifetime metadata. Single owner serializes each
// particular request; independent builders may share the wrapped API.
// Enqueue registers copied ownership BEFORE source.enqueue, and never reads the
// request afterwards. Exceptions retain uncertainty; no retry/guessed cleanup.
NativeButtonRequestApi wrap_native_button_request_api(NativeButtonRequestApi source);
// Focus/reset cancels logical intent only. Live ownership records and quarantine
// survive until a proven terminal path; logging limits never discard records.
void note_native_button_retirement_reset() noexcept;
NativeButtonRetirementStats native_button_retirement_stats() noexcept;
void flush_native_button_retirement() noexcept;
}
