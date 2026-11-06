#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string_view>
namespace swvr {
struct NativeKeypadRequest {
    std::uintptr_t vtable{};std::uint32_t type{11},metadata{},vehicle_id{},section_index{};
    std::array<std::int32_t,3> grid{};float value{};std::int32_t channel{};std::uint32_t padding{};
};
static_assert(sizeof(NativeKeypadRequest)==0x30&&offsetof(NativeKeypadRequest,value)==0x24&&offsetof(NativeKeypadRequest,channel)==0x28);
inline constexpr std::uintptr_t keypad_request_vtable_rva=0xb11460,keypad_serialize_rva=0x231e00;
inline constexpr std::array<unsigned char,24> keypad_serialize_prefix{0x80,0x7a,0x14,0x00,0x4c,0x8b,0xc1,0x8b,0x42,0x10,0x75,0x11,0x83,0xf8,0x04,0x72,0x27,0x48,0x8b,0x42,0x08,0x8b,0x08,0x41};
struct NativeKeypadSession {
    using Clock=std::chrono::steady_clock;
    std::uint64_t generation{};std::uintptr_t owner{},world{},actor{},ui{},target{};
    std::uint32_t vehicle{},section{};std::array<std::int32_t,3> grid{};
    unsigned button_type{};std::array<float,2> current{};
    Clock::time_point observed_at{};bool recognized{},visible{},focused{};
};
struct NativeKeypadApi {
    void* context{};std::uintptr_t image_base{},request_vtable{};
    void* (*allocate)(void*,std::size_t){};
    void (*destroy_unsubmitted)(void*,NativeKeypadRequest*){};
    void (*enqueue)(void*,void* live_queue,NativeKeypadRequest*){};
    // Called inside the recognized native live scope after allocations and
    // before each handoff. A pointer/token alone is never an authorization.
    bool (*validate_live)(void*,const NativeKeypadSession&){};
};
float parse_keypad_decimal(std::string_view); // Deliberately finite ASCII subset.
enum class KeypadSubmissionState { empty,prepared,submitted,partial,uncertain,discarded };
// Prepared integration port. No native session hook is installed by this class.
// Caller supplies a FULL profile proof and live-scope callbacks. Destruction
// performs no game calls. An attempted handoff may never be retried or freed.
class NativeKeypadSubmission {
public:
    NativeKeypadSubmission(bool full_sha_matched,NativeKeypadApi);
    ~NativeKeypadSubmission()=default;
    NativeKeypadSubmission(const NativeKeypadSubmission&)=delete;
    NativeKeypadSubmission& operator=(const NativeKeypadSubmission&)=delete;
    void prepare(const NativeKeypadSession&,std::string_view first,std::optional<std::string_view> second,
                 NativeKeypadSession::Clock::time_point now=NativeKeypadSession::Clock::now());
    void prepare_values(const NativeKeypadSession&,std::array<float,2>,NativeKeypadSession::Clock::time_point now=NativeKeypadSession::Clock::now());
    void submit(const NativeKeypadSession&,void* current_live_queue,NativeKeypadSession::Clock::time_point now=NativeKeypadSession::Clock::now());
    void discard_remaining();
    KeypadSubmissionState state() const noexcept{return state_;}
private:
    NativeKeypadApi api_;NativeKeypadSession certificate_;
    std::array<NativeKeypadRequest*,2> requests_{};
    KeypadSubmissionState state_{KeypadSubmissionState::empty};
};
}
