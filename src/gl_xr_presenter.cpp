#include "swvr/gl_xr_presenter.hpp"
#include "swvr/gl_eye_targets.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <stdexcept>
#include <string>

namespace swvr {
namespace {
constexpr GLenum draw_fb=0x8CA9,read_fb=0x8CA8,draw_binding=0x8CA6,read_binding=0x8CAA;
constexpr GLenum color0=0x8CE0,complete=0x8CD5,srgb8_alpha8=0x8C43;
template<class T>T function(const char* name) {
    const auto p=wglGetProcAddress(name);
    if(!p || p==reinterpret_cast<PROC>(1) || p==reinterpret_cast<PROC>(2) ||
       p==reinterpret_cast<PROC>(3) || p==reinterpret_cast<PROC>(-1))
        throw std::runtime_error(std::string("Missing GL function ")+name);
    return reinterpret_cast<T>(p);
}
}
struct GlXrPresenter::Impl {
    HGLRC context{};
    std::array<GLuint,2> framebuffers{};
    void(APIENTRY* bind)(GLenum,GLuint){};
    void(APIENTRY* gen)(GLsizei,GLuint*){};
    void(APIENTRY* remove)(GLsizei,const GLuint*){};
    void(APIENTRY* attach)(GLenum,GLenum,GLenum,GLuint,GLint){};
    GLenum(APIENTRY* status)(GLenum){};
    void initialize() {
        const auto current=wglGetCurrentContext();
        if(!current || (context && context!=current))
            throw std::logic_error("XR presenter requires its owning current GL context");
        if(!context) {
            bind=function<decltype(bind)>("glBindFramebuffer");
            gen=function<decltype(gen)>("glGenFramebuffers");
            remove=function<decltype(remove)>("glDeleteFramebuffers");
            attach=function<decltype(attach)>("glFramebufferTexture2D");
            status=function<decltype(status)>("glCheckFramebufferStatus");
            context=current;
            gen(2,framebuffers.data());
        }
        if(!framebuffers[0] || !framebuffers[1]) throw std::runtime_error("XR presenter FBO allocation failed");
    }
    struct Bindings {
        Impl& api;
        GLint read{},draw{},texture{};
        explicit Bindings(Impl& api):api(api) {
            glGetIntegerv(read_binding,&read);glGetIntegerv(draw_binding,&draw);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
        }
        ~Bindings() {
            // Do not retain references to swapchain textures after their frame.
            for(const auto fbo:api.framebuffers) {
                api.bind(draw_fb,fbo);api.attach(draw_fb,color0,GL_TEXTURE_2D,0,0);
            }
            api.bind(read_fb,read);api.bind(draw_fb,draw);
            glBindTexture(GL_TEXTURE_2D,texture);
        }
    };
    ~Impl() {
        // Explicitly destroy the presenter before releasing the game context.
        // During process teardown, never issue commands on another context.
        if(context && wglGetCurrentContext()==context) remove(2,framebuffers.data());
    }
};
GlXrPresenter::GlXrPresenter():impl_(std::make_unique<Impl>()) {}
GlXrPresenter::~GlXrPresenter()=default;
std::array<bool,2> GlXrPresenter::present(GlEyeTargets& source,const std::array<XrEyeFrame,2>& eyes) {
    auto& s=*impl_;s.initialize();
    for(const auto& eye:eyes) {
        if(!eye.texture || eye.width<=0 || eye.height<=0 || eye.width>16384 || eye.height>16384)
            throw std::invalid_argument("Invalid XR eye image");
        if(!glIsTexture(eye.texture)) throw std::invalid_argument("XR eye image is not a GL texture");
        for(unsigned i=0;i<2;++i) if(source.texture(i)==eye.texture)
            throw std::invalid_argument("XR eye image aliases a captured source");
    }
    if(eyes[0].texture==eyes[1].texture) throw std::invalid_argument("XR eye images alias each other");
    Impl::Bindings bindings(s);
    // Only GL_TEXTURE_2D single-sample RGBA8/sRGB images are accepted, matching
    // XrRuntime's swapchain contract. Never resize a borrowed runtime image.
    for(std::size_t i=0;i<2;++i) {
        const auto& eye=eyes[i];
        glBindTexture(GL_TEXTURE_2D,eye.texture);
        GLint width{},height{},format{};
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
        if(width!=eye.width || height!=eye.height || (format!=GL_RGBA8 && format!=srgb8_alpha8))
            throw std::invalid_argument("XR eye texture storage does not match its frame");
        s.bind(draw_fb,s.framebuffers[i]);
        s.attach(draw_fb,color0,GL_TEXTURE_2D,eye.texture,0);glDrawBuffer(color0);
        if(s.status(draw_fb)!=complete) throw std::runtime_error("XR eye framebuffer is incomplete");
    }
    for(unsigned i=0;i<2;++i) {
        s.bind(draw_fb,s.framebuffers[i]);
        source.blit_to_bound_draw(i,{0,0,eyes[i].width,eyes[i].height});
    }
    return {true,true};
}
}
