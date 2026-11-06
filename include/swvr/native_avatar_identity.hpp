#pragma once
#include "swvr/avatar_pose.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
namespace swvr {
struct AvatarIdentity {std::uintptr_t actor{};AvatarVariant variant{};bool valid{};};
// First read step that rejected an observation, in chain order.
enum class AvatarIdentityStage {controller,world_node,node_kind,actor,vtable,local,head,customization,count};
const char* avatar_identity_stage_name(AvatarIdentityStage) noexcept;
struct AvatarIdentityStats {
    std::uint64_t observations{},valid_observations{};
    std::array<std::uint64_t,static_cast<std::size_t>(AvatarIdentityStage::count)> failed{};
    // Snapshots refused because the last observation was invalid, or older than 100ms.
    std::uint64_t invalid_snapshots{},stale_snapshots{};
    // Diagnostics of palettes(): 19-bone colour records classified by whether
    // their mesh belongs to the local actor (tokens from the last observation)
    // and by the head selector. Mesh tokens are shared assets, so other
    // characters with the same outfit also count as "local mesh".
    std::uint64_t local_meshes{},scans{},local_mesh_clip3{},local_mesh_other_clip{},foreign_19_bone{},scans_local_without_clip3{};
};
// Read only, no hooks. Observe only on the current native Selection callback.
// A snapshot contains values, never a pointer for later dereference, and expires
// after 100ms. The controller/world/local-character chain is re-read each time.
class NativeAvatarIdentity {
public:
    NativeAvatarIdentity(std::uintptr_t executable_base,bool full_sha_matched);
    ~NativeAvatarIdentity();
    void observe(void* live_controller) noexcept;
    AvatarIdentity snapshot() const noexcept;
    AvatarIdentityStats stats() const;
    // Borrow the current prepared scene only during its renderer invocation.
    // Returned palette addresses are comparison tokens, never dereferenced later.
    std::vector<AvatarPaletteBinding> palettes(void* live_scene) const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
