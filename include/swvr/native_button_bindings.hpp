#pragma once
#include "swvr/native_button_capture.hpp"
#include "swvr/native_button_request.hpp"
#include <cstdint>
#include <memory>

namespace swvr {
// Windows x64 binding for executable profile c38c03ff...22a338d. The caller
// supplies the result of a full executable SHA-256 comparison; false/null/memory
// or signature mismatch throws before any game helper is called. This does not
// install hooks, start the game, or enable a production input backend.
class NativeButtonRequestBindings {
public:
    NativeButtonRequestBindings(std::uintptr_t exe_base,bool full_sha_matched);
    ~NativeButtonRequestBindings(); // Metadata only; NEVER calls native cleanup.
    NativeButtonRequestBindings(const NativeButtonRequestBindings&)=delete;
    NativeButtonRequestBindings& operator=(const NativeButtonRequestBindings&)=delete;
    // Context is stable until this bindings object is destroyed. Every builder
    // using it must finish explicit pre-handoff disposal or handoff first. Native
    // queue ownership after handoff is not managed by this bindings object.
    // One owner serializes all operations on a particular request (including
    // assign/discard); different builders may share these bindings concurrently.
    // The metadata mutex is not a lifetime lock against misuse of one request.
    NativeButtonRequestApi api() noexcept;
    // The certificate MUST describe a press actually accepted by the game, not
    // merely a selected/queued target. Captures copied values only; it never
    // clears native held flags, enqueues a request, or authorizes release.
    NativeButtonCaptureResult capture_release(const NativeButtonCaptureScope& current,
                                              const HandEventCertificate& accepted_press) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
