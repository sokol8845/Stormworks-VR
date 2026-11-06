# OpenXR runtime embedded in the game renderer

`swvr::XrRuntime` owns the OpenXR loader reference, instance, session, LOCAL space,
two color swapchains and optional left/right hand trackers. It uses the existing
game HDC/HGLRC. It does not create a window, GL context, framebuffer, depth target,
camera hook or render thread. Public types do not expose OpenXR SDK headers.

## Frame contract

Initialize on the game render thread with an absolute loader DLL path and the
currently bound HDC/HGLRC. Keep that context current on this thread for every
runtime call, including shutdown, and shut down before destroying the context.
The class rejects calls on a different thread/context. Its destructor cannot
repair a lost GL context: in that programming-error case it avoids runtime calls
and leaves resources for process teardown. Use explicit `shutdown()` while the
context is still alive.

Call `begin_frame()` once per game frame. It polls lifecycle events, starts a
READY session, stops a STOPPING session, and returns `id == 0` when no frame can
run. For a nonzero ID, always call `end_frame(id, completed_eyes)` or
`cancel_frame()`, even when `should_render` is false. Session/instance loss marks
the runtime terminal; shut it down and explicitly initialize a new session.

Each begun frame performs wait/begin, then locates eyes and both hands at the
same predicted display time. Its timestamp belongs to the OpenXR clock domain;
do not compare it directly with Windows or C++ wall/steady clocks. Eye poses and
hand joints use OpenXR LOCAL coordinates and meters. FOV angles are radians.

When `should_render` is true, both eyes have successfully acquired and waited
images. The texture IDs are borrowed single-sample `GL_TEXTURE_2D` objects. Attach
them to caller-owned FBOs, render into them or blit completed eye targets into
them, and restore the game's GL state. Never delete, resize, or keep these
textures for a later frame. Their widths/heights are the runtime's recommended
sizes. Swapchains declare color-attachment and transfer-destination usage. The
caller must check FBO completeness and report an eye as completed only after all
render/copy operations for that eye have succeeded.

`end_frame` flushes queued GL commands, releases waited images, and submits a
projection layer only if both eyes are completed and the current views are valid.
A partially completed pair, invalid head pose, or `shouldRender == false` ends
with zero layers. Texture acquisition is skipped entirely when rendering is
suppressed. Failed acquisition/wait makes the runtime terminal; successfully
waited images are released and the begun frame is discarded. In particular,
`XR_TIMEOUT_EXPIRED` is not treated as a successful image wait. These ownership
and context rules follow the [OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html).

```cpp
swvr::XrRuntime xr;
xr.initialize({absolute_loader_path, wglGetCurrentDC(), wglGetCurrentContext()});

// At the game renderer's controlled per-frame entry point:
const auto frame = xr.begin_frame();
if (frame.id != 0) {
    std::array<bool, 2> completed{};
    try {
        if (frame.should_render) {
            for (std::size_t eye = 0; eye != 2; ++eye) {
                // Apply frame.eyes[eye].pose/FOV to the game's camera bridge.
                // Render and copy into its texture at its width/height.
                // Restore the game's camera and GL state.
                completed[eye] = render_and_copy_eye(frame.eyes[eye]);
            }
        }
        xr.end_frame(frame.id, completed);
    } catch (...) {
        xr.cancel_frame();
        throw;
    }
}
// Before destroying the game's GL context:
xr.shutdown();
```

The snippet's `render_and_copy_eye` is an integration placeholder, not a library
function. `GlEyeTargets` may provide private game render targets; attach each XR
texture to a temporary draw FBO before copying. `info().srgb` and
`info().color_format` identify the selected format (sRGB RGBA8 preferred, RGBA8
fallback). The integration must deliberately match source encoding and
`GL_FRAMEBUFFER_SRGB`; this module performs no color conversion.

## Tracking and interaction

The frame exposes separate position/orientation valid and tracked bits. Rendering
requires both valid bits, while input policy may require both tracked bits too.
`focused` controls whether gameplay gestures should be accepted; visibility alone
does not grant input focus. Each hand has `active` and all 26 joints with pose,
radius and independent validity/tracking flags. Inactive hands have cleared
joint data, preventing stale pinches. Joint indices follow
`XR_HAND_JOINT_SET_DEFAULT_EXT`; constants identify thumb tip (5) and index tip
(10). Compile-time assertions compare these indices to the SDK definitions.
The caller should cancel active interactions immediately on focus loss,
inactive hands or invalid required joints. Runtime hand support is optional and
does not imply that the current PC streaming path provides usable hand poses.

`space_revision` increments when a LOCAL reference-space change reaches the
frame's display time. The camera bridge must invalidate/re-establish its
game-to-LOCAL calibration and cancel in-flight gestures on a revision change.
This module deliberately does not guess the user's seated origin or convert a
hand position into a Stormworks cockpit control.

## Build and verification

Add `src/xr_runtime.cpp` to a Windows C++20 static library, publish `include`, add
`vendor/OpenXR/include` privately, define `NOMINMAX`, and link `opengl32`. No OpenXR
import library is needed; the loader path is supplied at initialization. Link
the game integration to this library. The SDK macros are confined to the source.

The deterministic protocol tests use a separately built fake loader DLL and a
hidden test GL context. They exercise lifecycle, complete/partial/empty frame
submission, the shared prediction time, hand validity, focus loss, space changes,
thread ownership, stop/exit, failure after one acquired eye, a positive timeout
result, and cancellation during shutdown. They neither install nor select a
system OpenXR runtime.

```cmake
add_library(swvr_xr STATIC src/xr_runtime.cpp)
target_include_directories(swvr_xr PUBLIC include PRIVATE vendor/OpenXR/include)
target_compile_features(swvr_xr PUBLIC cxx_std_20)
target_compile_definitions(swvr_xr PRIVATE NOMINMAX)
target_link_libraries(swvr_xr PUBLIC opengl32)

add_library(swvr_xr_fake SHARED tests/xr_runtime_fake_loader.cpp)
target_include_directories(swvr_xr_fake PRIVATE vendor/OpenXR/include)
target_compile_features(swvr_xr_fake PRIVATE cxx_std_20)
target_compile_definitions(swvr_xr_fake PRIVATE NOMINMAX)
target_link_libraries(swvr_xr_fake PRIVATE opengl32)
add_executable(swvr_xr_runtime_tests tests/xr_runtime_tests.cpp)
target_link_libraries(swvr_xr_runtime_tests PRIVATE swvr_xr opengl32 gdi32 user32)
target_compile_definitions(swvr_xr_runtime_tests PRIVATE NOMINMAX)
add_dependencies(swvr_xr_runtime_tests swvr_xr_fake)
add_test(NAME xr_runtime COMMAND swvr_xr_runtime_tests $<TARGET_FILE:swvr_xr_fake>)
```

Verified with MSVC x64 C++20, `/W4 /WX /permissive-`, using the fake loader. No
headset or compositor validation has been performed for this integration yet.
Tests do not prove Quest image quality, timing, color correctness, pose latency,
hand delivery over Link/Air Link, or the correctness of rendering the actual game
twice. Shared game history/culling and cockpit input routing remain integration
work outside this runtime.

## Explicit fake GPU integration mode

The test loader supports `SWVR_FAKE_GL_IMAGES=1` for a bounded native integration
test without a headset. It allocates real single-sample OpenGL RGBA8/sRGB images,
provides deliberately asymmetric FOVs and two tracked poses separated by 64 mm,
and validates submitted projection poses, FOVs, image rectangles and release
order. It is still a fake runtime and reports `headset_test: false`.

Optional `SWVR_FAKE_WIDTH`/`SWVR_FAKE_HEIGHT` set dimensions from 16 to 4096
(default 640x480). `SWVR_FAKE_REPORT` must be an absolute path; at instance
destruction the loader writes aggregate frame/error counts and the first valid
projection's RGB checksums, nonblack flags and eye-image difference. Explicitly
shut down the runtime before stopping the process to obtain this report. No
per-frame images or unbounded logs are retained. Allocation/readback preserve
host texture bindings, pixel buffer bindings and changed pixel-pack settings.
The fake loader now links `opengl32`; with the mode absent, existing deterministic
protocol tests retain their previous behavior and synthetic texture names.
