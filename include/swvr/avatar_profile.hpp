#pragma once
#include "swvr/avatar_pose.hpp"
#include <filesystem>
#include <string>
#include <string_view>
namespace swvr {
// User proportions only. Native actor pointers, LOCAL origins and generations
// are never persisted; each application binds a fresh live local identity.
struct AvatarProfile {
    AvatarVariant variant{AvatarVariant::male};
    double wrist_span{};
    std::array<double,3> shoulder_offset{};
};
void validate_avatar_profile(const AvatarProfile&);
AvatarProfile make_avatar_profile(const AvatarBodyCalibration&);
AvatarProfile parse_avatar_profile(std::string_view);
std::string format_avatar_profile(const AvatarProfile&);
std::optional<AvatarProfile> load_avatar_profile(const std::filesystem::path&);
void save_avatar_profile(const std::filesystem::path&,const AvatarProfile&,const std::filesystem::path& staging_directory);
std::optional<AvatarBodyCalibration> bind_avatar_profile(const AvatarProfile&,AvatarVariant,std::uint64_t actor,std::uint64_t revision,std::uint64_t generation);
}
