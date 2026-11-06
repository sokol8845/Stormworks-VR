#include "swvr/gl_eye_targets.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <array>
#include <stdexcept>
#include <string>

namespace swvr {
namespace {
constexpr GLenum draw_fb=0x8CA9,read_fb=0x8CA8,draw_binding=0x8CA6,read_binding=0x8CAA;
constexpr GLenum color0=0x8CE0,complete=0x8CD5,srgb=0x8DB9,unpack_buffer=0x88EC;
template<class T> T function(const char* name) {
    const auto p=wglGetProcAddress(name);
    if(!p || p==reinterpret_cast<PROC>(1) || p==reinterpret_cast<PROC>(2) ||
       p==reinterpret_cast<PROC>(3) || p==reinterpret_cast<PROC>(-1))
        throw std::runtime_error(std::string("Missing GL function ")+name);
    return reinterpret_cast<T>(p);
}
void check_eye(unsigned eye) {if(eye>1) throw std::invalid_argument("Eye index");}
void check_rect(PixelRect r) {
    if(r.x<0||r.y<0||r.width<=0||r.height<=0||r.width>16384||r.height>16384||
       r.x>16384-r.width||r.y>16384-r.height) throw std::invalid_argument("Eye rectangle");
}
}
struct GlEyeTargets::Impl {
    HGLRC context{};
    std::array<GLuint,2> textures{},framebuffers{};
    std::array<bool,2> captured{};
    int width{},height{};
    using Bind=void(APIENTRY*)(GLenum,GLuint);
    Bind bind_fb{},bind_buffer{};
    void(APIENTRY* gen_fb)(GLsizei,GLuint*){};
    void(APIENTRY* delete_fb)(GLsizei,const GLuint*){};
    void(APIENTRY* attach)(GLenum,GLenum,GLenum,GLuint,GLint){};
    GLenum(APIENTRY* status)(GLenum){};
    void(APIENTRY* blit)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum){};
    void current() const {
        if(!context || wglGetCurrentContext()!=context) throw std::runtime_error("Eye targets require their owning GL context");
    }
    void initialize() {
        if(context) {current();return;}
        const auto active=wglGetCurrentContext();
        if(!active) throw std::runtime_error("No current GL context");
        bind_fb=function<Bind>("glBindFramebuffer");
        bind_buffer=function<Bind>("glBindBuffer");
        gen_fb=function<decltype(gen_fb)>("glGenFramebuffers");
        delete_fb=function<decltype(delete_fb)>("glDeleteFramebuffers");
        attach=function<decltype(attach)>("glFramebufferTexture2D");
        status=function<decltype(status)>("glCheckFramebufferStatus");
        blit=function<decltype(blit)>("glBlitFramebuffer");
        context=active;
    }
    struct Bindings {
        Impl& api;
        GLint read{},draw{};
        GLboolean scissor{},color_srgb{};
        explicit Bindings(Impl& api):api(api) {
            glGetIntegerv(read_binding,&read);glGetIntegerv(draw_binding,&draw);
            scissor=glIsEnabled(GL_SCISSOR_TEST);color_srgb=glIsEnabled(srgb);
            glDisable(GL_SCISSOR_TEST);glDisable(srgb);
        }
        ~Bindings() {
            api.bind_fb(read_fb,read);api.bind_fb(draw_fb,draw);
            if(scissor) glEnable(GL_SCISSOR_TEST);
            if(color_srgb) glEnable(srgb);
        }
    };
    ~Impl() {
        // Context destruction releases its resources. Never issue GL commands
        // from DLL teardown when another/no context is current.
        if(context && wglGetCurrentContext()==context) {
            delete_fb(2,framebuffers.data());glDeleteTextures(2,textures.data());
        }
    }
};
GlEyeTargets::GlEyeTargets():impl_(std::make_unique<Impl>()) {}
GlEyeTargets::~GlEyeTargets()=default;
void GlEyeTargets::prepare(int width,int height) {
    check_rect({0,0,width,height});auto& s=*impl_;s.initialize();
    if(s.width==width && s.height==height) return;
    Impl::Bindings bindings(s);
    GLint texture{},unpack{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(0x88EF,&unpack);
    struct Restore {
        Impl& s;GLint texture,unpack;
        ~Restore(){glBindTexture(GL_TEXTURE_2D,texture);s.bind_buffer(unpack_buffer,unpack);}
    } restore{s,texture,unpack};
    s.bind_buffer(unpack_buffer,0);
    if(!s.textures[0]) glGenTextures(2,s.textures.data());
    if(!s.framebuffers[0]) s.gen_fb(2,s.framebuffers.data());
    s.width=s.height=0;s.captured={false,false};
    for(unsigned eye=0;eye<2;++eye) {
        if(!s.textures[eye]||!s.framebuffers[eye]) throw std::runtime_error("Eye target allocation failed");
        glBindTexture(GL_TEXTURE_2D,s.textures[eye]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812F);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812F);
        s.bind_fb(draw_fb,s.framebuffers[eye]);
        s.attach(draw_fb,color0,GL_TEXTURE_2D,s.textures[eye],0);
        glDrawBuffer(color0);
        if(s.status(draw_fb)!=complete) throw std::runtime_error("Eye framebuffer incomplete");
    }
    s.width=width;s.height=height;
}
void GlEyeTargets::capture(unsigned eye,PixelRect source) {
    check_eye(eye);check_rect(source);auto& s=*impl_;s.current();
    if(s.width!=source.width||s.height!=source.height) throw std::invalid_argument("Capture size differs from targets");
    GLint source_fb{},source_buffer{};
    glGetIntegerv(draw_binding,&source_fb);glGetIntegerv(0x8825,&source_buffer); // DRAW_BUFFER0
    if(!source_buffer || source_fb==static_cast<GLint>(s.framebuffers[eye]))
        throw std::runtime_error("Invalid eye capture source");
    Impl::Bindings bindings(s);
    s.bind_fb(read_fb,source_fb);
    GLint previous_read{};glGetIntegerv(GL_READ_BUFFER,&previous_read);
    glReadBuffer(source_buffer);
    s.bind_fb(draw_fb,s.framebuffers[eye]);
    s.blit(source.x,source.y,source.x+source.width,source.y+source.height,
           0,0,s.width,s.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glReadBuffer(previous_read);
    s.captured[eye]=true;
}
void GlEyeTargets::blit_to_bound_draw(unsigned eye,PixelRect destination) {
    check_eye(eye);check_rect(destination);auto& s=*impl_;s.current();
    if(!s.captured[eye]) throw std::runtime_error("Eye has not been captured");
    Impl::Bindings bindings(s);
    if(bindings.draw==static_cast<GLint>(s.framebuffers[eye])) throw std::runtime_error("Eye blit feedback");
    s.bind_fb(read_fb,s.framebuffers[eye]);
    s.blit(0,0,s.width,s.height,destination.x,destination.y,
           destination.x+destination.width,destination.y+destination.height,GL_COLOR_BUFFER_BIT,GL_LINEAR);
}
unsigned GlEyeTargets::texture(unsigned eye) const {check_eye(eye);return impl_->textures[eye];}
std::vector<unsigned char> GlEyeTargets::read_rgb(unsigned eye) {
    check_eye(eye);auto& s=*impl_;s.current();
    if(!s.captured[eye]) throw std::runtime_error("Eye has not been captured");
    const auto size=static_cast<std::size_t>(s.width)*s.height*3;
    if(size>256*1024*1024) throw std::runtime_error("Diagnostic image exceeds size limit");
    std::vector<unsigned char> pixels(size);
    Impl::Bindings bindings(s);
    constexpr GLenum pack_buffer=0x88EB;
    GLint pbo{};glGetIntegerv(0x88ED,&pbo);
    constexpr std::array<GLenum,4> names{GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS};
    std::array<GLint,4> old{};
    for(std::size_t i=0;i<names.size();++i) glGetIntegerv(names[i],&old[i]);
    s.bind_buffer(pack_buffer,0);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
    s.bind_fb(read_fb,s.framebuffers[eye]);
    glReadPixels(0,0,s.width,s.height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    for(std::size_t i=0;i<names.size();++i) glPixelStorei(names[i],old[i]);
    s.bind_buffer(pack_buffer,pbo);
    return pixels;
}
}
