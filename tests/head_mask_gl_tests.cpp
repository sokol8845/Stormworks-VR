#include "swvr/head_mask_gl.hpp"
#include "swvr/head_mask_shader.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void throws(F&& fn,const char* message){bool caught{};try{fn();}catch(const std::exception&){caught=true;}require(caught,message);}
template<class T>T function(const char* name) {
    const auto p=wglGetProcAddress(name);
    require(p && p!=reinterpret_cast<PROC>(1) && p!=reinterpret_cast<PROC>(2) && p!=reinterpret_cast<PROC>(3) && p!=reinterpret_cast<PROC>(-1),name);
    return reinterpret_cast<T>(p);
}
struct Context {
    HWND window{};HDC dc{};HGLRC gl{};
    Context() {
        window=CreateWindowExW(0,L"STATIC",L"SWVR hidden head-mask test",WS_POPUP,0,0,16,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"test window");dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pf{};pf.nSize=sizeof(pf);pf.nVersion=1;pf.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pf.iPixelType=PFD_TYPE_RGBA;pf.cColorBits=32;
        const int format=ChoosePixelFormat(dc,&pf);require(format && SetPixelFormat(dc,format,&pf),"pixel format");
        gl=wglCreateContext(dc);require(gl && wglMakeCurrent(dc,gl),"bootstrap context");
        using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);const auto create=function<Create>("wglCreateContextAttribsARB");
        const int attributes[]{0x2091,4,0x2092,1,0x9126,1,0};const auto core=create(dc,nullptr,attributes);require(core!=nullptr,"GL4.1 core context");
        wglMakeCurrent(nullptr,nullptr);wglDeleteContext(gl);gl=core;require(wglMakeCurrent(dc,gl)!=0,"current core context");
    }
    ~Context(){wglMakeCurrent(nullptr,nullptr);if(gl)wglDeleteContext(gl);if(dc)ReleaseDC(window,dc);if(window)DestroyWindow(window);}
};
struct Gl {
    GLuint(APIENTRY* create_shader)(GLenum)=function<decltype(create_shader)>("glCreateShader");
    void(APIENTRY* shader_source)(GLuint,GLsizei,const char* const*,const GLint*)=function<decltype(shader_source)>("glShaderSource");
    void(APIENTRY* compile_shader)(GLuint)=function<decltype(compile_shader)>("glCompileShader");
    void(APIENTRY* get_shader)(GLuint,GLenum,GLint*)=function<decltype(get_shader)>("glGetShaderiv");
    void(APIENTRY* shader_log)(GLuint,GLsizei,GLsizei*,char*)=function<decltype(shader_log)>("glGetShaderInfoLog");
    void(APIENTRY* delete_shader)(GLuint)=function<decltype(delete_shader)>("glDeleteShader");
    GLuint(APIENTRY* create_program)()=function<decltype(create_program)>("glCreateProgram");
    void(APIENTRY* attach_shader)(GLuint,GLuint)=function<decltype(attach_shader)>("glAttachShader");
    void(APIENTRY* detach_shader)(GLuint,GLuint)=function<decltype(detach_shader)>("glDetachShader");
    void(APIENTRY* link_program)(GLuint)=function<decltype(link_program)>("glLinkProgram");
    void(APIENTRY* get_program)(GLuint,GLenum,GLint*)=function<decltype(get_program)>("glGetProgramiv");
    void(APIENTRY* delete_program)(GLuint)=function<decltype(delete_program)>("glDeleteProgram");
    void(APIENTRY* use_program)(GLuint)=function<decltype(use_program)>("glUseProgram");
    GLint(APIENTRY* location)(GLuint,const char*)=function<decltype(location)>("glGetUniformLocation");
    void(APIENTRY* set_integer)(GLint,GLint)=function<decltype(set_integer)>("glUniform1i");
    void(APIENTRY* set_scalar)(GLint,GLfloat)=function<decltype(set_scalar)>("glUniform1f");
    void(APIENTRY* get_integer)(GLuint,GLint,GLint*)=function<decltype(get_integer)>("glGetUniformiv");
    void(APIENTRY* get_scalar)(GLuint,GLint,GLfloat*)=function<decltype(get_scalar)>("glGetUniformfv");
    void(APIENTRY* gen_fbo)(GLsizei,GLuint*)=function<decltype(gen_fbo)>("glGenFramebuffers");
    void(APIENTRY* delete_fbo)(GLsizei,const GLuint*)=function<decltype(delete_fbo)>("glDeleteFramebuffers");
    void(APIENTRY* bind_fbo)(GLenum,GLuint)=function<decltype(bind_fbo)>("glBindFramebuffer");
    void(APIENTRY* attach_texture)(GLenum,GLenum,GLenum,GLuint,GLint)=function<decltype(attach_texture)>("glFramebufferTexture2D");
    GLenum(APIENTRY* fbo_status)(GLenum)=function<decltype(fbo_status)>("glCheckFramebufferStatus");
    void(APIENTRY* gen_vao)(GLsizei,GLuint*)=function<decltype(gen_vao)>("glGenVertexArrays");
    void(APIENTRY* delete_vao)(GLsizei,const GLuint*)=function<decltype(delete_vao)>("glDeleteVertexArrays");
    void(APIENTRY* bind_vao)(GLuint)=function<decltype(bind_vao)>("glBindVertexArray");
    void(APIENTRY* gen_buffer)(GLsizei,GLuint*)=function<decltype(gen_buffer)>("glGenBuffers");
    void(APIENTRY* delete_buffer)(GLsizei,const GLuint*)=function<decltype(delete_buffer)>("glDeleteBuffers");
    void(APIENTRY* bind_buffer)(GLenum,GLuint)=function<decltype(bind_buffer)>("glBindBuffer");
};
void(APIENTRY* original_uniform1i)(GLint,GLint){};
std::uint64_t original_writes{};
void APIENTRY observed_original(GLint location,GLint value){++original_writes;original_uniform1i(location,value);}
void* original_resolver(const char* name) {
    if(std::string_view(name)=="glUniform1i")return reinterpret_cast<void*>(&observed_original);
    return reinterpret_cast<void*>(wglGetProcAddress(name));
}
const char* vertex=R"(#version 330 core
uniform int bone_clip_index;
uniform float bone_clip_weight;
uniform float test_depth;
out float log_z;
out vec4 vertex_color_out;
void main() {
    vec2 points[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(points[gl_VertexID],0,1);
    log_z=test_depth;
    vertex_color_out=vec4(0.8,0.2,0.1,1);
    // A test mesh vertex with head bone 2 and influence weight 0.8.
    if(bone_clip_index==2 && 0.8>bone_clip_weight) vertex_color_out.a=0;
}
)";
const char* fragment=R"(#version 330 core
in float log_z;
in vec4 vertex_color_out;
out vec4 color_out;
uniform float additive_factor;
float log_z_to_frag_depth(float value) { return value; }
float decode_depth_linear(float value) { return value; }
void main()
{
    gl_FragDepth = log_z_to_frag_depth(log_z);
    if(vertex_color_out.a <= 0 && decode_depth_linear(gl_FragDepth) < 0.25)
    {
        discard;
    }
    color_out=vec4(vertex_color_out.rgb*additive_factor,1);
}
)";
struct Program {
    Gl& gl;GLuint id{},vs{},fs{};
    GLuint compile(GLenum type,const std::string& source) {
        const auto shader=gl.create_shader(type);const char* data=source.c_str();gl.shader_source(shader,1,&data,nullptr);gl.compile_shader(shader);
        GLint ok{};gl.get_shader(shader,0x8B81,&ok);
        if(!ok){std::array<char,1024> log{};gl.shader_log(shader,static_cast<GLsizei>(log.size()),nullptr,log.data());gl.delete_shader(shader);throw std::runtime_error(log.data());}
        return shader;
    }
    Program(Gl& gl,bool patched):gl(gl){vs=compile(0x8B31,vertex);id=gl.create_program();gl.attach_shader(id,vs);relink(patched);}
    void relink(bool patched) {
        if(fs){gl.detach_shader(id,fs);gl.delete_shader(fs);}
        std::string source=fragment;
        if(patched){auto result=swvr::patch_head_mask_fragment(source);require(result.patched,"GPU shader fixture unsupported");source=std::move(result.source);}
        fs=compile(0x8B30,source);gl.attach_shader(id,fs);gl.link_program(id);GLint ok{};gl.get_program(id,0x8B82,&ok);require(ok!=0,"head mask test link failed");
    }
    ~Program(){gl.use_program(0);gl.delete_program(id);gl.delete_shader(vs);gl.delete_shader(fs);}
    void values(int index,float weight,float depth){gl.use_program(id);gl.set_integer(gl.location(id,"bone_clip_index"),index);gl.set_scalar(gl.location(id,"bone_clip_weight"),weight);gl.set_scalar(gl.location(id,"test_depth"),depth);gl.set_scalar(gl.location(id,"additive_factor"),1);}
    int flag(){GLint value{-1};const auto location=gl.location(id,swvr::head_mask_uniform_name.data());if(location>=0)gl.get_integer(id,location,&value);return value;}
};
struct Target {
    Gl& gl;GLuint texture{},fbo{},vao{},buffer{};
    explicit Target(Gl& gl):gl(gl) {
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl.gen_fbo(1,&fbo);gl.bind_fbo(0x8D40,fbo);gl.attach_texture(0x8D40,0x8CE0,GL_TEXTURE_2D,texture,0);glDrawBuffer(0x8CE0);glReadBuffer(0x8CE0);
        require(gl.fbo_status(0x8D40)==0x8CD5,"test framebuffer incomplete");gl.gen_vao(1,&vao);gl.gen_buffer(1,&buffer);gl.bind_vao(vao);gl.bind_buffer(0x8892,buffer);
        glViewport(0,0,8,8);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(0x8DB9);glClearColor(0,0,0,1);
    }
    ~Target(){gl.bind_vao(0);gl.bind_buffer(0x8892,0);gl.delete_vao(1,&vao);gl.delete_buffer(1,&buffer);gl.delete_fbo(1,&fbo);glDeleteTextures(1,&texture);}
    bool visible(){glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);std::array<unsigned char,4> pixel{};glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());return pixel[0]>150 && pixel[1]>30;}
};
struct State {
    std::array<GLint,13> values{};std::array<GLint,4> viewport{},scissor{};std::array<GLboolean,4> mask{};GLboolean blend{},depth{};
    State() {
        constexpr std::array<GLenum,13> names{0x8B8D,0x8CAA,0x8CA6,0x85B5,0x8894,GL_TEXTURE_BINDING_2D,0x84E0,GL_READ_BUFFER,GL_DRAW_BUFFER,0x80C9,0x80C8,0x8009,0x883D};
        for(std::size_t i=0;i<names.size();++i)glGetIntegerv(names[i],&values[i]);glGetIntegerv(GL_VIEWPORT,viewport.data());glGetIntegerv(GL_SCISSOR_BOX,scissor.data());glGetBooleanv(GL_COLOR_WRITEMASK,mask.data());blend=glIsEnabled(GL_BLEND);depth=glIsEnabled(GL_DEPTH_TEST);
    }
    bool operator==(const State&) const=default;
};
}
int main() {
    try {
        Context context;Gl gl;original_uniform1i=gl.set_integer;Target target(gl);Program program(gl,true);const GLuint first_program_name=program.id;
        swvr::GlHeadMask mask;
        program.values(2,.25f,.75f);throws([&]{mask.before_draw(true);},"missing original resolver accepted");
        mask.configure(original_resolver);require(program.flag()==0,"patched uniform must default to zero");
        const auto check=[&](bool vr,bool expected_mask,bool visible) {
            const State previous;GLint index_before{};GLfloat weight_before{};
            gl.get_integer(program.id,gl.location(program.id,"bone_clip_index"),&index_before);gl.get_scalar(program.id,gl.location(program.id,"bone_clip_weight"),&weight_before);
            require(mask.before_draw(vr)==expected_mask,"incorrect head signature activation");require(program.flag()==static_cast<int>(expected_mask),"wrong fragment mask value");require(State{}==previous,"head mask helper modified host GL state");
            GLint index_after{};GLfloat weight_after{};gl.get_integer(program.id,gl.location(program.id,"bone_clip_index"),&index_after);gl.get_scalar(program.id,gl.location(program.id,"bone_clip_weight"),&weight_after);
            require(index_after==index_before && (weight_after==weight_before || (std::isnan(weight_after)&&std::isnan(weight_before))),"native head selection uniforms changed");require(target.visible()==visible,"GPU head visibility wrong");
        };
        check(false,false,true);check(true,true,false);check(false,false,true);
        program.values(2,.25f,.1f);check(false,false,false);check(true,true,false);
        program.values(-1,.25f,.75f);check(true,false,true);
        program.values(2,.5f,.75f);check(true,false,true);
        program.values(2,std::numeric_limits<float>::quiet_NaN(),.75f);check(true,false,true);
        program.values(2,.2500005f,.75f);check(true,true,false);
        program.values(1,.25f,.75f);check(true,true,true); // Native selected bone differs from this test mesh vertex.
        program.values(2,.25f,.75f);check(false,false,true);
        const auto cached=mask.stats();mask.before_draw(false);require(mask.stats().program_cache_misses==cached.program_cache_misses,"program locations were not cached");
        require(mask.interest(wglGetCurrentContext(),program.id)==swvr::GlProgramInterest::active && mask.interest(wglGetCurrentContext(),program.id+1000)==swvr::GlProgramInterest::unknown && mask.stats().calls==cached.calls+1,"patched program interest wrong or not side-effect free");
        const auto before_relink=mask.stats();program.relink(false);mask.program_linked(program.id);require(mask.interest(wglGetCurrentContext(),program.id)==swvr::GlProgramInterest::unknown,"link did not reset program interest");program.values(2,.25f,.75f);const State unpatched_state;
        require(!mask.before_draw(true) && program.flag()==-1,"unpatched program modified");require(State{}==unpatched_state && target.visible(),"unpatched shader behavior or state changed");
        require(mask.interest(wglGetCurrentContext(),program.id)==swvr::GlProgramInterest::none,"unpatched program not reported as skippable");
        require(mask.stats().uniform_writes==before_relink.uniform_writes && mask.stats().program_cache_misses==before_relink.program_cache_misses+1,"link invalidation failed");
        program.relink(true);mask.program_linked(program.id);program.values(2,.25f,.75f);require(program.flag()==0,"relink default not zero");check(true,true,false);check(false,false,true);
        // A fresh unshared context deliberately uses the same numeric program
        // name. Its unpatched shader must not inherit locations from context 1.
        {
            Context second;Gl second_gl;Program second_program(second_gl,false);
            require(second_program.id==first_program_name,"test requires same program name in independent context");second_program.values(2,.25f,.75f);
            require(!mask.before_draw(true) && second_program.flag()==-1,"program cache leaked between HGLRCs");mask.context_destroyed(second.gl);
        }
        require(wglMakeCurrent(context.dc,context.gl)!=0,"restore first GL context");gl.use_program(program.id);
        // Do not consume a GL error that already belonged to the host.
        require(glGetError()==GL_NO_ERROR,"unexpected prior GL error");glEnable(0xDEADBEEF);mask.before_draw(false);require(glGetError()==GL_INVALID_ENUM,"helper consumed host GL error");
        gl.use_program(0);require(!mask.before_draw(true),"program zero should be skipped");gl.use_program(program.id);
        const auto stats=mask.stats();require(stats.candidate_calls>0 && stats.masked_calls>0 && stats.masked_calls<stats.candidate_calls,"candidate/masked statistics wrong");
        require(stats.uniform_writes==original_writes,"helper bypassed configured original uniform resolver");require(glGetError()==GL_NO_ERROR,"GL error in head-mask GPU tests");
        mask.context_destroyed(context.gl);wglMakeCurrent(nullptr,nullptr);require(!mask.before_draw(true),"missing context should be skipped");require(wglMakeCurrent(context.dc,context.gl)!=0,"restore context before cleanup");
        std::cout<<"PASS head-mask GPU: far/near pixels, native signature gates, disable restore, host state, no GL-error consumption, relink/context cache and original resolver\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
