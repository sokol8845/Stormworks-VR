#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace swvr {
// Exact x64 layout of c_client_to_server_request_use_vehicle_button for the
// measured executable profile. Slot zero is a SERIALIZER, not a destructor.
struct NativeButtonString {
    std::uintptr_t data{};
    // Capacity counts allocated BYTES including the terminating NUL.
    std::uint32_t length{},capacity{};
};
struct NativeButtonRequestLayout {
    std::uintptr_t vtable{};
    std::uint32_t type{},metadata{};
    std::uint32_t vehicle_id{},section_index{};
    std::array<std::int32_t,3> component_grid{};
    std::uint8_t pressed{};
    std::array<std::uint8_t,3> padding25{};
    std::array<float,3> local_hit{};
    std::uint8_t skip_on_button_press{};
    std::array<std::uint8_t,3> padding35{};
    NativeButtonString payload{};
};
static_assert(sizeof(std::uintptr_t)==8);
static_assert(sizeof(NativeButtonString)==0x10 && sizeof(NativeButtonRequestLayout)==0x48);
static_assert(offsetof(NativeButtonRequestLayout,vehicle_id)==0x10);
static_assert(offsetof(NativeButtonRequestLayout,component_grid)==0x18);
static_assert(offsetof(NativeButtonRequestLayout,pressed)==0x24);
static_assert(offsetof(NativeButtonRequestLayout,local_hit)==0x28);
static_assert(offsetof(NativeButtonRequestLayout,skip_on_button_press)==0x34);
static_assert(offsetof(NativeButtonRequestLayout,payload)==0x38);

struct NativeButtonReleaseData {
    std::uint32_t vehicle_id{},section_index{};
    std::array<std::int32_t,3> component_grid{};
    // Copy the saved press hit, not the new hover hit. Bits, including -0,
    // are preserved. The caller must capture these values in a live scope.
    std::array<float,3> saved_local_hit{};
    bool skip_on_button_press{};
    std::string_view payload;
};
// An adapter binds these operations only after verifying the executable. This
// module resolves no addresses and never uses the DLL's allocator for events.
// assign_payload must own its copy independently of source, including on partial
// failure. A non-null buffer needs capacity >= length+1 and a terminating NUL.
// destroy_unsubmitted must handle a zero-initialized/partial payload;
// it is NEVER request.vtable[0]. No callback may retain source.data().
struct NativeButtonRequestApi {
    void* context{};
    std::uintptr_t request_vtable{};
    void* (*allocate)(void* context,std::size_t bytes){};
    void (*assign_payload)(void* context,NativeButtonString* destination,std::string_view source){};
    void (*destroy_unsubmitted)(void* context,NativeButtonRequestLayout* request){};
    void (*enqueue)(void* context,void* live_queue,NativeButtonRequestLayout* request){};
};
enum class NativeButtonRequestState {
    empty, preparing, prepared, preparation_failed, discarding, discarded,
    transferred, ownership_uncertain, discard_uncertain
};
// One release request per instance. Explicit native operations only; destructor
// never calls the game (including under the Windows DLL loader lock).
class NativeButtonReleaseRequest {
public:
    explicit NativeButtonReleaseRequest(NativeButtonRequestApi api);
    ~NativeButtonReleaseRequest();
    NativeButtonReleaseRequest(const NativeButtonReleaseRequest&)=delete;
    NativeButtonReleaseRequest& operator=(const NativeButtonReleaseRequest&)=delete;
    void prepare(const NativeButtonReleaseData& data);
    // After any attempted enqueue the caller MUST NOT discard or retry. A
    // throwing enqueue has an unknown ownership outcome, even if it freed the
    // request synchronously. The queue pointer is not retained after this call.
    void submit(void* live_queue);
    // Explicit cleanup, only before enqueue. A throwing native cleanup is not
    // retried: doing so could double-free an already partially freed object.
    void discard();
    NativeButtonRequestState state() const noexcept;
    // Identity only. This address is not a lifetime guarantee or a dereference
    // permission; after submit even its allocation may already be destroyed.
    std::uintptr_t address_token() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
