#pragma once
#include "swvr/xr_runtime.hpp"
namespace swvr {
struct StickVisual {
    bool active{},turn{},sprint{},forward_only{};unsigned hand{};
    Vec3 displacement{};float dead_zone{},range{.12f};
    std::uint64_t frame{},revision{};
};
struct WheelGripVisual {
    bool visible{},established{};Pose plane{};float radius{.25f};
    std::array<bool,2> held{};std::uint64_t frame{},revision{};
};
}
