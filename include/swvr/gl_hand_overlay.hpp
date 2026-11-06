#pragma once
#include "swvr/xr_runtime.hpp"
#include "swvr/numeric_keypad.hpp"
#include "swvr/comfort.hpp"
#include <array>
#include <chrono>
#include <memory>
#include <string_view>

namespace swvr {
// Diagnostic LOCAL-space hand skeletons, rendered into the current XR images.
// Owns GL program/VAO/VBO/FBOs only; never owns the borrowed eye textures.
class GlHandOverlay {
public:
    GlHandOverlay();
    ~GlHandOverlay();
    GlHandOverlay(const GlHandOverlay&)=delete;
    GlHandOverlay& operator=(const GlHandOverlay&)=delete;
    // Call after eye image copies and before XrRuntime::end_frame. No-op for
    // nonrenderable frames or absent valid tracked hand segments. Exceptions
    // require cancelling the XR frame. Uses the first current GL 4.1+ context.
    // Optional green 3 m diagnostic ray from the RIGHT index tip, along
    // proximal-to-tip direction. Requires focus and fully tracked head/joints.
    // It is not a hit marker or a claim about native interaction range.
    void draw(const XrRuntimeFrame& frame,bool pointing_ray=false,unsigned pointing_hand=1);
    // Recolour both hand skeletons for `duration` (status feedback visible in
    // the headset, e.g. green = T-pose calibration accepted, red = rejected).
    void flash(std::array<float,3> rgb,std::chrono::milliseconds duration);
    // Head-locked square outline ~1 m ahead in the centre of view, for status
    // that must be visible while the hands are out of sight (T-pose: yellow =
    // pose detected/hold, green = accepted, red = rejected).
    void status_frame(std::array<float,3> rgb,std::chrono::milliseconds duration);
    void status_text(std::string_view text,std::chrono::milliseconds duration);
    void progress(float fraction,std::chrono::milliseconds duration);
    void keypad(const KeypadSnapshot&);
    void menu_button(const PanelSnapshot&);
    void comfort(ComfortVisual);
    void diagnostics(std::vector<std::string> lines);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
