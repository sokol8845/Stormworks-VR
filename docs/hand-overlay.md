# Tracked hand skeleton overlay

`swvr::GlHandOverlay::draw(const XrRuntimeFrame&, bool pointing_ray = false)` draws the current frame's hand
positions into both borrowed OpenXR eye textures. Call it after
`GlXrPresenter::present` and before `XrRuntime::end_frame`. It uses each eye's
LOCAL pose and FOV, together with the hands sampled at that frame's predicted
display time. It creates no window, context or render thread.

```cpp
const auto completed = presenter.present(captured_eyes, frame.eyes);
try {
    hand_overlay.draw(frame);
    xr.end_frame(frame.id, completed);
} catch (...) {
    xr.cancel_frame();
    throw;
}
```

The caller must keep the current frame's images acquired until drawing finishes.
The overlay never deletes or resizes those textures. It detaches them from its
private FBOs before returning, including exceptional exits. Both image sizes,
formats and framebuffer completeness are checked before either image is drawn.
An exception requires discarding the XR frame; callers must not submit a partial
eye pair.

The implementation requires OpenGL 4.1+ on its initial owner thread and context.
It uses GLSL 330 core shaders, a private VAO/VBO, and private framebuffer objects.
Destroy the overlay while that context is still current, before destroying the
game context. If the context has already gone away, its destructor avoids GL
calls and leaves cleanup to context/process teardown.

## What is drawn

Left-hand segments are cyan; right-hand segments are orange. The skeleton has 25
segments connecting all 26 standard OpenXR joints. The palm (0) connects to wrist
(1) and the five metacarpals. Each finger then follows its anatomical chain:

| Finger | Joint chain after palm |
| --- | --- |
| Thumb | 2 → 3 → 4 → 5 |
| Index | 6 → 7 → 8 → 9 → 10 |
| Middle | 11 → 12 → 13 → 14 → 15 |
| Ring | 16 → 17 → 18 → 19 → 20 |
| Little | 21 → 22 → 23 → 24 → 25 |

These indices were checked against `XrHandJointEXT` in the vendored
`vendor/OpenXR/include/openxr/openxr.h`. Thumb tip is 5 and index tip is 10.

An active hand contributes a segment only when both endpoints have valid,
tracked, finite positions. Joint orientations and radii are not needed for line
segments. Missing endpoints are skipped individually. Inactive hands never draw;
no previous joint data, predicted replacement hand, or synthetic production pose
is retained. Nonrenderable, terminal, invalid-eye-tracking or zero-ID frames are
skipped. Head and hand coordinates remain in LOCAL space, with meters and the
OpenXR convention of -Z forward. The near/far clip distances are 0.01/20 meters.

Visibility of hands does not authorize gameplay interaction. This visual helper
does not accept gestures or change cockpit controls. Input code must separately
check focus, tracking flags and its calibration/gesture state.

## Optional pointing direction

Calling `draw(frame, true)` adds a green diagnostic segment starting at the
**right** index fingertip (10). Its direction is the normalized vector from the
right index proximal joint (7) to that tip, and its length is 3 meters in LOCAL
space. The same frame's eye poses/FOV project it independently into both eyes.
The default call `draw(frame)` retains the skeleton-only behavior.

The ray requires a running, focused, renderable frame, valid and tracked head
position/orientation, an active right hand, and finite, valid, tracked positions
for the thumb tip (5), index proximal joint (7) and index tip (10). A proximal-to-tip
distance below 1 cm suppresses it, matching the hand interaction bridge's default
minimum. Joint orientation flags are unnecessary for a position-derived direction.
No stale ray is retained. Losing these stricter ray requirements does not change
the existing skeleton visibility policy: for example, skeletons may remain
visible while focus is lost, but the ray disappears.

This segment indicates direction only. It has no scene-depth occlusion, does not
stop at cockpit surfaces and does not mark a confirmed hit. Three meters is its
visual length, not a guaranteed interaction limit; existing native selection
branches may query farther. The integration should enable it only when the
optional native hand-ray path is installed. Showing it does not establish that
selection or cockpit actions succeeded.

## Rendering limits and state ownership

This is a diagnostic skeleton, not a hand mesh. Lines are one pixel wide and
opaque. There is no scene-depth occlusion: fingers remain visible through the
cockpit and other geometry. Scene depth integration and more readable hand shapes
are future work. Colors are written as encoded values with framebuffer sRGB
conversion disabled, matching the current presenter's encoded-image copy mode.

The helper restores read/draw FBO bindings, texture binding, viewport 0, program,
VAO, array-buffer binding, blend and scissor enables for index 0, color mask 0,
depth test/write mask, stencil test, sRGB, rasterizer discard, color logic op,
line smoothing/width, depth clamp and all user clip-distance enables. It uses
indexed viewport/scissor/blend/color-mask state so other viewports and MRT slots
are unaffected. Host read/draw-buffer selectors and blend parameters are never
modified. Host vertex attributes belong to its own VAO and are untouched.

## Build and validation

Add `src/gl_hand_overlay.cpp` to the Windows GL library, with public `include`,
C++20, `NOMINMAX`, and dependencies on `swvr_core` and `opengl32`. The core library
provides the shared projection and view-matrix math. No OpenXR loader/import
library or additional SDK include path is needed by this helper.

```cmake
target_sources(swvr_gl PRIVATE src/gl_hand_overlay.cpp)
target_link_libraries(swvr_gl PUBLIC swvr_core)
add_executable(swvr_gl_hand_overlay_tests tests/gl_hand_overlay_tests.cpp)
target_compile_definitions(swvr_gl_hand_overlay_tests PRIVATE NOMINMAX)
target_link_libraries(swvr_gl_hand_overlay_tests PRIVATE swvr_gl opengl32 gdi32 user32)
add_test(NAME gl_hand_overlay COMMAND swvr_gl_hand_overlay_tests)
```

The hidden WGL GPU test requests an OpenGL 4.1 core context. It reads actual
rendered pixels to verify LOCAL projection and stereo parallax, left/right colors,
thumb-tip and little-tip connections, suppression of inactive/invalid/untracked
and non-finite samples, and unchanged images on failed second-eye validation. It
also checks host state restoration with a bound host shader, VAO/VBO, different
viewport/MRT state, enabled clipping and stencil, and enabled rasterizer discard.
Borrowed textures must survive overlay destruction.

The optional ray test checks actual green pixels beyond the finger bones and the
expected projected endpoint for a 3 m segment in both eyes. It confirms default-off
behavior and compares enabled versus skeleton-only images byte-for-byte for ten
focus/tracking/hand-validity/short-finger cases, while checking restored GL state.

The test passed on the local GPU with MSVC x64 `/W4 /WX /permissive-`. Its hand
poses are deliberately synthetic test fixtures. This does not validate real Quest
hand delivery, alignment, appearance in the headset, or tracking latency. No game
was launched for these tests.
