#pragma once
#include "swvr/xr_runtime.hpp"
#include <array>
#include <memory>

namespace swvr {
class GlEyeTargets;
// Copies completed eye images into the CURRENT frame's borrowed OpenXR textures.
// Owns only FBOs, bound to the first current context. Never owns XR textures.
class GlXrPresenter {
public:
    GlXrPresenter();
    ~GlXrPresenter();
    GlXrPresenter(const GlXrPresenter&)=delete;
    GlXrPresenter& operator=(const GlXrPresenter&)=delete;
    // Both destinations are checked before either is written. Returns {true,true}
    // only after both copies. Exceptions require the caller to cancel the XR frame.
    // Input/output are encoded RGBA8 values: sRGB conversion is disabled.
    // Different sizes linearly resample those encoded values, not linear light.
    std::array<bool,2> present(GlEyeTargets& source,const std::array<XrEyeFrame,2>& eyes);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
