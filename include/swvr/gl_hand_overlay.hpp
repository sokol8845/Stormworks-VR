#pragma once
#include "swvr/xr_runtime.hpp"
#include "swvr/numeric_keypad.hpp"
#include "swvr/comfort.hpp"
#include "swvr/input_visuals.hpp"
#include "swvr/target_feedback.hpp"
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
    // Optional 3 m ray from the pointing hand's index tip, along the
    // proximal-to-tip direction. Requires focus and fully tracked head/joints.
    // Grey by default; target_feedback() turns it green for a button the game
    // will accept, or amber with a short reason while a click is blocked or held.
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
    // Head-locked joystick rings and the wheel grip zone. A visual from another
    // XR frame or reference space is drawn idle (sticks) or not at all (wheel).
    void input_guides(bool sticks,std::array<StickVisual,2>,WheelGripVisual);
    // Colours the pointing ray until the confirmation is 100 ms old.
    void target_feedback(TargetFeedback);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
