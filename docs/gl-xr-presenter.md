# Copying captured game eyes into OpenXR images

`GlXrPresenter::present(GlEyeTargets&, frame.eyes)` copies the two completed game
images into the current OpenXR frame's borrowed textures. It returns
`{true,true}` only after both copies; if it throws, cancel the OpenXR frame rather
than submitting a partial pair. This helper does not start/end an XR frame or
render another game view.

Both destinations are checked before either is copied: nonzero texture names,
separate images, no alias of either source, matching dimensions, supported
RGBA8/sRGB RGBA8 storage and complete FBO attachments. The caller must supply the
single-sample `GL_TEXTURE_2D` images returned by `XrRuntime`. Other texture targets,
array images and multisample images are outside this contract.

The helper owns two FBOs in the context current on its first call. It never owns,
resizes or deletes a swapchain texture. Attachments are removed before returning,
including on exceptions, so no borrowed image reference remains in its FBOs.
Destroy the presenter on its owning context before that context is destroyed.
If the context has already gone, destruction issues no GL commands.

Read/draw FBO bindings and active-unit 2D texture binding are restored. Host
read/draw buffer selectors are never modified, since all selector changes apply
to private FBOs. Viewport and scissor rectangle remain untouched. The source
copy helper saves/restores scissor enable and `GL_FRAMEBUFFER_SRGB`.

The current policy copies encoded RGBA8 channel values with sRGB conversion
disabled. Equal-size copies preserve those values for either RGBA8 or
SRGB8_ALPHA8 destinations. Scaling uses GL_LINEAR on encoded values; it does not
perform linear-light resampling. This is suitable for experimentally forwarding
the final encoded game color image, but correctness of the game's final color
encoding and compositor appearance still requires headset validation. This
module does not prove those properties or add alpha/environment blending.

`tests/gl_xr_presenter_tests.cpp` creates a hidden native WGL context and uses real
GPU textures. Distinct patterned eyes verify image order, pixel values, sRGB
behavior and 2x resampling. Tests also exercise separate host read/draw bindings,
non-default read selection, viewport, scissor, sRGB state, invalid destination
dimensions, source feedback, same-image alias, zero image, uncaptured source,
missing current context, and survival of borrowed textures after destruction.

Build `src/gl_xr_presenter.cpp` with C++20, `include`, `NOMINMAX` and `opengl32`;
link the library containing `GlEyeTargets`. The test additionally links
`user32` and `gdi32`. No OpenXR loader or headset is used by this test.

Verified on the development machine with MSVC x64 `/W4 /WX /permissive-`:
the real GPU test passes. This confirms the copying helper, not rendering the
game from XR poses or displaying these images in a headset.
