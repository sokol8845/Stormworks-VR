#pragma once
#include <memory>
#include <vector>

namespace swvr {
struct PixelRect { int x{},y{},width{},height{}; };
// Two independent color targets owned by the current OpenGL context.
// Copies restore framebuffer bindings, source read selection, scissor and sRGB.
class GlEyeTargets {
public:
    GlEyeTargets();
    ~GlEyeTargets();
    GlEyeTargets(const GlEyeTargets&)=delete;
    GlEyeTargets& operator=(const GlEyeTargets&)=delete;
    void prepare(int width,int height);
    void capture(unsigned eye,PixelRect source);
    void blit_to_bound_draw(unsigned eye,PixelRect destination);
    unsigned texture(unsigned eye) const;
    // Diagnostic readback, bottom row first; preserves pixel-pack state/PBO.
    std::vector<unsigned char> read_rgb(unsigned eye);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
