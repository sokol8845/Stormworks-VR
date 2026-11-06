#pragma once
#include "swvr/tracking.hpp"
#include <span>

namespace swvr {
// Requires the version guard in game_profile.hpp to have succeeded before the
// caller obtains a native camera. Updates only P, float VP and double VP.
// Call AFTER the game's camera update, including for its floating-origin copies.
void apply_game_eye_projection(std::span<unsigned char> camera, Fov fov);
}
