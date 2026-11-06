#pragma once
#include <cstdint>
namespace swvr {
// Called only in verified LIVE pre-transition scopes. Registration precedes
// enabling hooks. Tokens are not permission to dereference a retired object.
struct NativeInputLifecycleCallbacks {
    void* context{};
    bool (*begin)(void*,std::uintptr_t owner,std::uintptr_t request_context) noexcept{};
    void (*end)(void*,bool native_succeeded) noexcept{};
};
}
