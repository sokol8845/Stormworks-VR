#include "swvr/gl_xr_presenter.hpp"
#include "swvr/gl_eye_targets.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr GLenum framebuffer=0x8D40,draw_fb=0x8CA9,read_fb=0x8CA8;
constexpr GLenum draw_binding=0x8CA6,read_binding=0x8CAA,color0=0x8CE0,srgb=0x8DB9;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class F>void throws(F&& fn,const char* message) {
    bool caught{};try {fn();}catch(const std::exception&){caught=true;}require(caught,message);
}
template<class T>T function(const char* name) {
    const auto p=wglGetProcAddress(name);require(p && p!=reinterpret_cast<PROC>(-1),name);
    return reinterpret_cast<T>(p);
}
struct Context {
    HWND window{};HDC dc{};HGLRC gl{};
    Context() {
        window=CreateWindowExW(0,L"STATIC",L"SWVR presenter GPU test",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"test window");dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pf{};pf.nSize=sizeof(pf);pf.nVersion=1;
        pf.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        pf.iPixelType=PFD_TYPE_RGBA;pf.cColorBits=32;
        const int format=ChoosePixelFormat(dc,&pf);require(format && SetPixelFormat(dc,format,&pf),"pixel format");
        gl=wglCreateContext(dc);require(gl && wglMakeCurrent(dc,gl),"current GL context");
    }
    ~Context() {wglMakeCurrent(nullptr,nullptr);if(gl)wglDeleteContext(gl);if(dc)ReleaseDC(window,dc);if(window)DestroyWindow(window);}
};
struct Gl {
    void(APIENTRY* bind)(GLenum,GLuint)=function<decltype(bind)>("glBindFramebuffer");
    void(APIENTRY* gen)(GLsizei,GLuint*)=function<decltype(gen)>("glGenFramebuffers");
    void(APIENTRY* remove)(GLsizei,const GLuint*)=function<decltype(remove)>("glDeleteFramebuffers");
    void(APIENTRY* attach)(GLenum,GLenum,GLenum,GLuint,GLint)=function<decltype(attach)>("glFramebufferTexture2D");
    GLenum(APIENTRY* status)(GLenum)=function<decltype(status)>("glCheckFramebufferStatus");
};
struct Image {
    Gl& gl;GLuint texture{},fbo{};int width{},height{};
    Image(Gl& gl,int width,int height,GLint format,const std::vector<unsigned char>& pixels={}):gl(gl),width(width),height(height) {
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,format,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.empty()?nullptr:pixels.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl.gen(1,&fbo);gl.bind(framebuffer,fbo);
        gl.attach(framebuffer,color0,GL_TEXTURE_2D,texture,0);glDrawBuffer(color0);glReadBuffer(color0);
        require(gl.status(framebuffer)==0x8CD5,"test framebuffer complete");
    }
    ~Image(){gl.remove(1,&fbo);glDeleteTextures(1,&texture);}
    std::vector<unsigned char> read() {
        gl.bind(read_fb,fbo);glReadBuffer(color0);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height*4);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;
    }
};
struct State {
    GLint read{},draw{},read_buffer{},draw_buffer{},texture{};
    std::array<GLint,4> viewport{},scissor_box{};
    GLboolean scissor{},color_srgb{};
    State() {
        glGetIntegerv(read_binding,&read);glGetIntegerv(draw_binding,&draw);
        glGetIntegerv(GL_READ_BUFFER,&read_buffer);glGetIntegerv(GL_DRAW_BUFFER,&draw_buffer);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(GL_VIEWPORT,viewport.data());
        glGetIntegerv(GL_SCISSOR_BOX,scissor_box.data());scissor=glIsEnabled(GL_SCISSOR_TEST);color_srgb=glIsEnabled(srgb);
    }
    bool operator==(const State&) const=default;
};
std::vector<unsigned char> pattern(unsigned eye) {
    std::vector<unsigned char> pixels(4*4*4);
    for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
        const auto p=(y*4+x)*4;
        pixels[p]=static_cast<unsigned char>(20+eye*110+x*13);
        pixels[p+1]=static_cast<unsigned char>(30+y*25);
        pixels[p+2]=static_cast<unsigned char>(150-eye*90+x*7+y*3);pixels[p+3]=255;
    }
    return pixels;
}
void scaled_matches(const std::vector<unsigned char>& actual,const std::vector<unsigned char>& source) {
    // Pixel centers for a 4x4 -> 8x8 bilinear blit, clamped to source edge.
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) for(int channel=0;channel<4;++channel) {
        const float sx=std::clamp((x+.5f)/2-.5f,0.f,3.f),sy=std::clamp((y+.5f)/2-.5f,0.f,3.f);
        const int ax=static_cast<int>(sx),ay=static_cast<int>(sy),bx=std::min(ax+1,3),by=std::min(ay+1,3);
        const float fx=sx-ax,fy=sy-ay;
        const auto sample=[&](int xx,int yy){return source[(yy*4+xx)*4+channel];};
        const float expected=(1-fy)*((1-fx)*sample(ax,ay)+fx*sample(bx,ay))+fy*((1-fx)*sample(ax,by)+fx*sample(bx,by));
        require(std::abs(actual[(y*8+x)*4+channel]-expected)<=1.1f,"scaled encoded pixel differs");
    }
}
}
int main() {
    try {
        Context context;Gl gl;
        const auto left_pixels=pattern(0),right_pixels=pattern(1);
        Image input_left(gl,4,4,GL_RGBA8,left_pixels),input_right(gl,4,4,GL_RGBA8,right_pixels);
        Image output_left(gl,4,4,GL_RGBA8),output_right(gl,4,4,0x8C43);
        Image scaled_left(gl,8,8,GL_RGBA8),scaled_right(gl,8,8,0x8C43);
        swvr::GlEyeTargets source;source.prepare(4,4);
        gl.bind(draw_fb,input_left.fbo);source.capture(0,{0,0,4,4});
        gl.bind(draw_fb,input_right.fbo);source.capture(1,{0,0,4,4});
        std::array<swvr::XrEyeFrame,2> eyes{};
        const auto destinations=[&](Image& left,Image& right){
            eyes[0].texture=left.texture;eyes[0].width=left.width;eyes[0].height=left.height;
            eyes[1].texture=right.texture;eyes[1].width=right.width;eyes[1].height=right.height;
        };
        const auto host_state=[&]{
            gl.bind(draw_fb,input_right.fbo);glDrawBuffer(color0);
            gl.bind(read_fb,input_left.fbo);glReadBuffer(GL_NONE);
            glBindTexture(GL_TEXTURE_2D,input_right.texture);
            glViewport(3,4,9,10);glScissor(7,8,1,1);glEnable(GL_SCISSOR_TEST);glEnable(srgb);
        };
        {
            swvr::GlXrPresenter presenter;
            destinations(output_left,output_right);host_state();const State initial;
            require(presenter.present(source,eyes)==std::array<bool,2>{true,true},"both eyes completed");
            require(State{}==initial,"host GL state changed");
            require(output_left.read()==left_pixels,"left RGBA8 pixels differ");
            require(output_right.read()==right_pixels,"right sRGB encoded pixels differ");
            destinations(scaled_left,scaled_right);host_state();const State scaled_state;
            presenter.present(source,eyes);require(State{}==scaled_state,"scaled copy changed state");
            scaled_matches(scaled_left.read(),left_pixels);scaled_matches(scaled_right.read(),right_pixels);
            // A failed second destination must leave the first untouched.
            const std::vector<unsigned char> sentinel(4*4*4,77);
            glBindTexture(GL_TEXTURE_2D,output_left.texture);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,sentinel.data());
            destinations(output_left,output_right);host_state();const State bad_state;
            eyes[1].width=3;
            throws([&]{presenter.present(source,eyes);},"wrong destination dimensions accepted");
            require(State{}==bad_state,"validation failure changed state");
            require(output_left.read()==sentinel,"invalid pair changed first destination");
            destinations(output_left,output_right);presenter.present(source,eyes);
            destinations(output_left,output_right);eyes[1].texture=source.texture(0);host_state();const State alias_state;
            throws([&]{presenter.present(source,eyes);},"source feedback accepted");
            require(State{}==alias_state,"alias failure changed state");
            destinations(output_left,output_right);eyes[1].texture=eyes[0].texture;
            throws([&]{presenter.present(source,eyes);},"same image for both eyes accepted");
            destinations(output_left,output_right);eyes[1].texture=0;
            throws([&]{presenter.present(source,eyes);},"zero destination accepted");
            swvr::GlEyeTargets uncaptured;uncaptured.prepare(4,4);destinations(output_left,output_right);host_state();const State empty_state;
            throws([&]{presenter.present(uncaptured,eyes);},"uncaptured eyes accepted");
            require(State{}==empty_state,"uncaptured failure changed state");
            wglMakeCurrent(nullptr,nullptr);
            throws([&]{presenter.present(source,eyes);},"missing current context accepted");
            require(wglMakeCurrent(context.dc,context.gl)!=0,"restore test context");
        }
        require(glIsTexture(output_left.texture) && glIsTexture(output_right.texture),"presenter deleted borrowed images");
        require(output_left.read()==left_pixels && output_right.read()==right_pixels,"borrowed images altered during destruction");
        require(glGetError()==GL_NO_ERROR,"GL error during presenter test");
        std::cout<<"PASS OpenXR image presenter: stereo pixels, RGBA8/sRGB encoding, scaling, host state, failure and borrowed lifetime\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
