#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace swvr {
inline constexpr std::size_t native_target_package_size=0xf0;
// A 1-pixel source is a transient mono target. Do not recreate/render stereo
// packages until both dimensions recover; the desktop mirror splits width.
bool native_target_has_stereo_extent(std::span<const unsigned char> source);
// Windows x64 integer/pointer ABI; caller must verify the executable profile.
using NativePackageInitialize=void(*)(void*,void*,std::uint32_t,std::uint32_t,
    std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,
    std::uint8_t,std::uint8_t,std::uint8_t,std::uint32_t,std::uint32_t,
    std::uint8_t,std::uint8_t,std::uint32_t,std::uint32_t,std::uint8_t);
using NativePackageResize=NativePackageInitialize;
using NativeResourceRelease=void(*)(void* manager_pending_list,void* resource);
using NativePackageDestroy=void(*)(void* package);
struct NativeResourceApi {
    NativePackageInitialize initialize{};
    NativePackageResize resize{}; // Same-size 3→2 attachment conversion only.
    NativeResourceRelease release{};
    NativePackageDestroy destroy_containers{};
};
class NativeEyeResources {
public:
    NativeEyeResources();
    // No native callbacks here. Call close explicitly while manager/context live.
    // Missing close leaves native resources for process teardown.
    ~NativeEyeResources();
    NativeEyeResources(const NativeEyeResources&)=delete;
    NativeEyeResources& operator=(const NativeEyeResources&)=delete;
    // Local in-process, profile-verified memory only. Bounds checks cannot prove
    // that arbitrary native pointers are readable. No address derivation occurs.
    // Accepts E0=2, E8=16 and two/three G-buffer attachments. Two attachments
    // use the measured same-size native reconfigure; no general resizing.
    void create(std::span<const unsigned char> source,void* manager,NativeResourceApi api);
    // False while native manager has not realized every required FBO. Throws
    // on changed source configuration or ownership/layout violations.
    bool ready(std::span<const unsigned char> source);
    // A supported configuration change can be handled by close/create before
    // calling ready. This check does not poison or mutate the owned packages.
    bool matches_configuration(std::span<const unsigned char> source) const;
    void* package(unsigned eye);
    bool created() const noexcept;
    bool failed() const noexcept;
    // Explicit owner-thread operation. Never call after manager/context death.
    // A throwing native callback is not retried, to avoid double release.
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
