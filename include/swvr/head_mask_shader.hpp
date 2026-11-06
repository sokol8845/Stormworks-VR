#pragma once
#include <string>
#include <string_view>

namespace swvr {
inline constexpr std::string_view head_mask_uniform_name="swvr_vr_head_mask";
struct HeadMaskShaderPatch {
    bool patched{}; // False means unsupported/already injected; source is unchanged.
    std::string source;
};
// In-memory, fail-closed extension of the known animated fragment head predicate.
// Accepts known headerless game templates or complete desktop GLSL 130..460.
// No file access, GL calls, shader compilation or game state changes.
HeadMaskShaderPatch patch_head_mask_fragment(std::string_view source);
}
