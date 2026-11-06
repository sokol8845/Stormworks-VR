#pragma once
#include "swvr/camera_bridge.hpp"
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace swvr {
// Geometry comes from the user's installed asset, never from a bundled copy.
inline constexpr std::uint64_t driver_seat_mesh_fingerprint=0x715c44215e6dc24cULL;
inline constexpr std::size_t driver_wheel_point_limit=192;
inline constexpr Vec3 driver_wheel_center{.0000105f,.4817497f,.2528192f};
inline constexpr Vec3 driver_wheel_axis{0,.2588190451f,-.9659258263f};
inline constexpr float driver_wheel_match_m=.00075f;
struct DriverWheelMesh {
    std::vector<Vec3> positions,normals,points;
    std::vector<std::uint16_t> indices;
    std::vector<bool> wheel;
};
DriverWheelMesh read_driver_seat_mesh(std::span<const unsigned char>);
DriverWheelMesh load_driver_seat_mesh(const std::filesystem::path&);
Vec3 rotate_driver_wheel(Vec3 point,float radians,bool direction=false);
bool driver_wheel_contains(std::span<const Vec3> points,Vec3 point) noexcept;
// Rigid matrices may include the native reflected basis. Reject scale/shear.
bool driver_wheel_rigid(const Matrix4d&) noexcept;
Matrix4d driver_wheel_product(const Matrix4d&,const Matrix4d&);
Matrix4d driver_wheel_inverse(const Matrix4d&);
Vec3 driver_wheel_transform(const Matrix4d&,Vec3,bool direction=false);
Matrix4d driver_seat_transform(const Matrix4d& section,const std::array<float,3>& offset,
    const std::array<std::int32_t,3>& grid,const std::array<std::int32_t,9>& rotation);
}
