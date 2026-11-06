#pragma once
#include <cstddef>
#include <span>
#include <vector>

namespace swvr {
struct RgbImage {int width{},height{};std::vector<unsigned char> pixels;}; // Tightly packed RGB8.
// Box-filter downscale by an integer factor; trailing rows/columns that do not
// fill a whole block are dropped. Throws std::invalid_argument on bad input.
RgbImage downscale_rgb(std::span<const unsigned char> rgb,int width,int height,int factor);
}
