// Real tracer-DLL integration. No patch helper is linked or called here.
#include <windows.h>
#include <GL/gl.h>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class T>T function(const char* name) {
    const auto p=wglGetProcAddress(name);
    require(p && p!=reinterpret_cast<PROC>(1) && p!=reinterpret_cast<PROC>(2) && p!=reinterpret_cast<PROC>(3) && p!=reinterpret_cast<PROC>(-1),name);
    return reinterpret_cast<T>(p);
}
using Entry=DWORD(WINAPI*)(void*);
struct Trace {
    Entry flush{},mode{};std::filesystem::path path;
    explicit Trace(bool enabled) {
        require(SetEnvironmentVariableW(L"SWVR_HEAD_MASK_TEST",enabled?L"1":nullptr)!=0,"fixture environment");
        require(SetEnvironmentVariableW(L"SWVR_TRACE_EVENTS",L"1")!=0,"fixture trace evidence environment");
        std::array<wchar_t,32768> executable{};
        const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
        require(length && length<executable.size(),"fixture executable path");
        const std::filesystem::path exe(executable.data());
        require(exe.filename()==L"swvr_head_mask_injection_tests.exe","fixture must have its exact guarded executable name");
        const auto module=LoadLibraryW((exe.parent_path()/L"swvr_gl_trace.dll").c_str());require(module!=nullptr,"sibling trace DLL unavailable");
        const auto start=reinterpret_cast<Entry>(GetProcAddress(module,"swvr_trace_start"));
        flush=reinterpret_cast<Entry>(GetProcAddress(module,"swvr_trace_flush"));
        mode=reinterpret_cast<Entry>(GetProcAddress(module,"swvr_trace_test_head_mask"));
        require(start && flush && mode,"trace/test export missing");
        path=exe.parent_path()/(L"head-mask-injection-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".tsv");
        require(path.is_absolute() && !std::filesystem::exists(path),"trace path must be absolute and unique");
        require(start(const_cast<wchar_t*>(path.c_str()))==0,"trace start failed");
        // Keep module loaded until process exit: active hooks must never outlive it.
    }
    void set(bool enabled){require(mode(enabled?reinterpret_cast<void*>(1):nullptr)==1,"guarded fixture mode toggle rejected");}
};
struct Context {
    HWND window{};HDC dc{};HGLRC gl{};
    Context() {
        window=CreateWindowExW(0,L"STATIC",L"SWVR hidden injection fixture",WS_POPUP,0,0,16,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"hidden test window");dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pf{};pf.nSize=sizeof(pf);pf.nVersion=1;pf.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pf.iPixelType=PFD_TYPE_RGBA;pf.cColorBits=32;
        const int format=ChoosePixelFormat(dc,&pf);require(format && SetPixelFormat(dc,format,&pf),"pixel format");
        gl=wglCreateContext(dc);require(gl && wglMakeCurrent(dc,gl),"bootstrap WGL context");
        using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);const auto create=function<Create>("wglCreateContextAttribsARB");
        const int attributes[]{0x2091,4,0x2092,1,0x9126,1,0};const auto core=create(dc,nullptr,attributes);require(core!=nullptr,"GL4.1 core context");
        wglMakeCurrent(nullptr,nullptr);wglDeleteContext(gl);gl=core;require(wglMakeCurrent(dc,gl)!=0,"current core context");
    }
    ~Context(){wglMakeCurrent(nullptr,nullptr);if(gl)wglDeleteContext(gl);if(dc)ReleaseDC(window,dc);if(window)DestroyWindow(window);}
};
struct Gl {
    GLuint(APIENTRY* create_shader)(GLenum)=function<decltype(create_shader)>("glCreateShader");
    void(APIENTRY* source)(GLuint,GLsizei,const char* const*,const GLint*)=function<decltype(source)>("glShaderSource");
    void(APIENTRY* compile)(GLuint)=function<decltype(compile)>("glCompileShader");
    void(APIENTRY* shader_value)(GLuint,GLenum,GLint*)=function<decltype(shader_value)>("glGetShaderiv");
    void(APIENTRY* shader_log)(GLuint,GLsizei,GLsizei*,char*)=function<decltype(shader_log)>("glGetShaderInfoLog");
    void(APIENTRY* delete_shader)(GLuint)=function<decltype(delete_shader)>("glDeleteShader");
    GLuint(APIENTRY* create_program)()=function<decltype(create_program)>("glCreateProgram");
    void(APIENTRY* attach)(GLuint,GLuint)=function<decltype(attach)>("glAttachShader");
    void(APIENTRY* link)(GLuint)=function<decltype(link)>("glLinkProgram");
    void(APIENTRY* program_value)(GLuint,GLenum,GLint*)=function<decltype(program_value)>("glGetProgramiv");
    void(APIENTRY* delete_program)(GLuint)=function<decltype(delete_program)>("glDeleteProgram");
    void(APIENTRY* use)(GLuint)=function<decltype(use)>("glUseProgram");
    GLint(APIENTRY* location)(GLuint,const char*)=function<decltype(location)>("glGetUniformLocation");
    void(APIENTRY* integer)(GLint,GLint)=function<decltype(integer)>("glUniform1i");
    void(APIENTRY* scalar)(GLint,GLfloat)=function<decltype(scalar)>("glUniform1f");
    void(APIENTRY* read_integer)(GLuint,GLint,GLint*)=function<decltype(read_integer)>("glGetUniformiv");
    void(APIENTRY* gen_fbo)(GLsizei,GLuint*)=function<decltype(gen_fbo)>("glGenFramebuffers");
    void(APIENTRY* delete_fbo)(GLsizei,const GLuint*)=function<decltype(delete_fbo)>("glDeleteFramebuffers");
    void(APIENTRY* bind_fbo)(GLenum,GLuint)=function<decltype(bind_fbo)>("glBindFramebuffer");
    void(APIENTRY* texture)(GLenum,GLenum,GLenum,GLuint,GLint)=function<decltype(texture)>("glFramebufferTexture2D");
    GLenum(APIENTRY* status)(GLenum)=function<decltype(status)>("glCheckFramebufferStatus");
    void(APIENTRY* gen_vao)(GLsizei,GLuint*)=function<decltype(gen_vao)>("glGenVertexArrays");
    void(APIENTRY* delete_vao)(GLsizei,const GLuint*)=function<decltype(delete_vao)>("glDeleteVertexArrays");
    void(APIENTRY* bind_vao)(GLuint)=function<decltype(bind_vao)>("glBindVertexArray");
    void(APIENTRY* gen_buffer)(GLsizei,GLuint*)=function<decltype(gen_buffer)>("glGenBuffers");
    void(APIENTRY* delete_buffer)(GLsizei,const GLuint*)=function<decltype(delete_buffer)>("glDeleteBuffers");
    void(APIENTRY* bind_buffer)(GLenum,GLuint)=function<decltype(bind_buffer)>("glBindBuffer");
    void(APIENTRY* buffer_data)(GLenum,std::ptrdiff_t,const void*,GLenum)=function<decltype(buffer_data)>("glBufferData");
};
const char* vertex=R"(#version 330 core
uniform int bone_clip_index;
uniform float bone_clip_weight;
uniform float test_depth;
uniform float test_alpha;
out float log_z;
out vec4 vertex_color_out;
void main() {
    vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    gl_Position=vec4(p[gl_VertexID],0,1);
    log_z=test_depth;
    vertex_color_out=vec4(0.8,0.2,0.1,test_alpha);
    if(bone_clip_index==2 && 0.8>bone_clip_weight) vertex_color_out.a=0;
}
)";
const char* fragment_body=R"(in float log_z;
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
    color_out=vec4(vertex_color_out.rgb*additive_factor*TEST_SCALE,1);
}
)";
struct Program {
    Gl& gl;GLuint id{},vs{},fs{};
    GLuint shader(GLenum type,const std::string& text,bool multipart,bool reject_injected_declaration=false) {
        const auto object=gl.create_shader(type);
        if(multipart) {
            std::string prefix="#version 330 core\n#define TEST_SCALE 1.0\n";
            // The original fragment uses no int token. This is valid before
            // injection, but makes the injected scalar int uniform a vec2.
            // Its initializer/comparison cannot compile, exercising fallback.
            if(reject_injected_declaration) prefix+="#define int vec2\n";
            const auto match=text.rfind("decode_depth_linear(gl_FragDepth)");
            require(match!=std::string::npos,"multipart split missing");const auto split=match+10;
            std::array<std::string,4> chunks{prefix,text.substr(0,split),"",text.substr(split)};
            std::array<GLint,4> lengths{};std::array<const char*,4> pointers{};
            for(std::size_t i=0;i<chunks.size();++i){lengths[i]=static_cast<GLint>(chunks[i].size());chunks[i]+="\n@INVALID_TRAILING_BYTES@\n";pointers[i]=chunks[i].data();}
            gl.source(object,static_cast<GLsizei>(pointers.size()),pointers.data(),lengths.data());
        } else {const char* data=text.c_str();gl.source(object,1,&data,nullptr);}
        gl.compile(object);GLint ok{};gl.shader_value(object,0x8B81,&ok);
        if(!ok){std::array<char,2048> log{};gl.shader_log(object,static_cast<GLsizei>(log.size()),nullptr,log.data());gl.delete_shader(object);throw std::runtime_error(std::string("Shader compile: ")+log.data());}
        return object;
    }
    Program(Gl& gl,bool supported,bool reject_injected_declaration=false):gl(gl) {
        vs=shader(0x8B31,vertex,false);
        const std::string plain="#version 330 core\nin float log_z;in vec4 vertex_color_out;out vec4 color_out;uniform float additive_factor;void main(){color_out=vec4(vertex_color_out.rgb*additive_factor,1);}";
        fs=shader(0x8B30,supported?fragment_body:plain,supported,reject_injected_declaration);
        id=gl.create_program();gl.attach(id,vs);gl.attach(id,fs);gl.link(id);GLint ok{};gl.program_value(id,0x8B82,&ok);require(ok!=0,"hooked program link failed");
    }
    ~Program(){gl.use(0);gl.delete_program(id);gl.delete_shader(vs);gl.delete_shader(fs);}
    void values(int index,float weight,float depth,float alpha=1) {
        gl.use(id);gl.integer(gl.location(id,"bone_clip_index"),index);gl.scalar(gl.location(id,"bone_clip_weight"),weight);
        gl.scalar(gl.location(id,"test_depth"),depth);gl.scalar(gl.location(id,"test_alpha"),alpha);gl.scalar(gl.location(id,"additive_factor"),1);
    }
};
struct Target {
    Gl& gl;GLuint image{},fbo{},vao{},indices{};
    explicit Target(Gl& gl):gl(gl) {
        glGenTextures(1,&image);glBindTexture(GL_TEXTURE_2D,image);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl.gen_fbo(1,&fbo);gl.bind_fbo(0x8D40,fbo);gl.texture(0x8D40,0x8CE0,GL_TEXTURE_2D,image,0);glDrawBuffer(0x8CE0);glReadBuffer(0x8CE0);require(gl.status(0x8D40)==0x8CD5,"test FBO incomplete");
        gl.gen_vao(1,&vao);gl.bind_vao(vao);gl.gen_buffer(1,&indices);gl.bind_buffer(0x8893,indices);const GLuint data[]{0,1,2};gl.buffer_data(0x8893,sizeof(data),data,0x88E4);
        glViewport(0,0,8,8);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(0x8DB9);glClearColor(0,0,0,1);
    }
    ~Target(){gl.bind_vao(0);gl.delete_vao(1,&vao);gl.delete_buffer(1,&indices);gl.delete_fbo(1,&fbo);glDeleteTextures(1,&image);}
    bool visible(bool indexed=false){glClear(GL_COLOR_BUFFER_BIT);if(indexed)glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_INT,nullptr);else glDrawArrays(GL_TRIANGLES,0,3);std::array<unsigned char,4> pixel{};glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());return pixel[0]>150 && pixel[1]>30;}
};
void verify_disabled_guard() {
    std::array<wchar_t,32768> path{};require(GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()))!=0,"guard child path");
    std::wstring command=L"\""+std::wstring(path.data())+L"\" --guard-disabled";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    require(CreateProcessW(path.data(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0,"start disabled-guard fixture child");
    CloseHandle(process.hThread);const auto wait=WaitForSingleObject(process.hProcess,30000);DWORD status=1;
    if(wait==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&status);CloseHandle(process.hProcess);
    require(wait==WAIT_OBJECT_0 && status==0,"disabled opt-in must reject the test export");
}
}
int main(int argc,char** argv) {
    try {
        const bool disabled=argc==2 && std::string_view(argv[1])=="--guard-disabled";require(argc==1 || disabled,"unknown fixture argument");
        Trace trace(!disabled);
        if(disabled){require(trace.mode(reinterpret_cast<void*>(1))==0 && trace.mode(nullptr)==0,"test export was available without opt-in");trace.flush(nullptr);return 0;}
        trace.set(false);
        {
            Context context;Gl gl;Target target(gl);Program supported(gl,true);
            const auto flag=gl.location(supported.id,"swvr_vr_head_mask");require(flag>=0,"glShaderSource hook did not inject the mask uniform");
            supported.values(2,.25f,.75f);require(target.visible(),"normal far head unexpectedly hidden");
            trace.set(true);require(!target.visible(),"VR draw-arrays head was not clipped");require(!target.visible(true),"VR draw-elements head was not clipped");
            trace.set(false);require(target.visible(),"normal head did not return after VR flag cleared");GLint value{-1};gl.read_integer(supported.id,flag,&value);require(value==0,"normal draw left the mask enabled");
            supported.values(2,.25f,.1f);require(!target.visible(),"native near-head behavior changed");
            trace.set(true);supported.values(-1,.25f,.75f,0);require(target.visible(),"negative bone index activated masking for authored alpha-zero vertices");
            supported.values(2,.5f,.75f);require(target.visible(),"non-head weight activated masking");
            supported.values(2,.25f,.75f);require(!target.visible(),"candidate head mask did not reactivate");
            Program plain(gl,false);require(gl.location(plain.id,"swvr_vr_head_mask")==-1,"unsupported shader was modified");plain.values(-1,.25f,.75f,0);require(target.visible(),"unpatched shader was affected");
            trace.set(false);supported.values(2,.25f,.75f);require(target.visible(),"normal state did not recover after another program");
            Program fallback(gl,true,true);
            require(gl.location(fallback.id,"swvr_vr_head_mask")==-1,"failed injected declaration was not replaced with original source");
            trace.set(true);fallback.values(2,.25f,.75f);require(target.visible(),"original far behavior lost after compile fallback");
            fallback.values(2,.25f,.1f);require(!target.visible(),"original near behavior lost after compile fallback");
            trace.set(false);
            require(glGetError()==GL_NO_ERROR,"GL error during DLL hook integration");
            // Program tracking: the hooks learn the bound program from glUseProgram
            // instead of asking the driver on every draw and uniform write.
            trace.set(true);supported.values(2,.25f,.75f);
            // A rejected glUseProgram leaves the patched program bound.
            gl.use(0x7fffffffu);require(glGetError()==GL_INVALID_VALUE,"fixture expected a rejected glUseProgram");
            require(!target.visible() && !target.visible(true),"head mask lost after a rejected glUseProgram");
            // One long binding crosses the periodic comparison with the driver.
            const auto alpha=gl.location(supported.id,"test_alpha"),weight=gl.location(supported.id,"bone_clip_weight");
            for(int i=0;i<600;++i){gl.scalar(alpha,1);require(!target.visible(i%2!=0),"head mask lost during a long binding");}
            // The head selection is still read per draw within one binding.
            gl.scalar(weight,.5f);require(target.visible(),"mask kept after the selection changed within one binding");
            gl.scalar(weight,.25f);require(!target.visible(),"mask not restored within one binding");
            plain.values(-1,.25f,.75f,0);for(int i=0;i<300;++i)require(target.visible(i%2!=0),"unpatched program affected during a long binding");
            supported.values(2,.25f,.75f);require(!target.visible(),"mask not applied after returning from an unpatched program");
            // Relinking the bound program invalidates what the hooks knew about it.
            gl.link(supported.id);supported.values(2,.25f,.75f);require(!target.visible(),"mask lost after relinking the bound program");
            gl.use(0);require(glGetError()==GL_NO_ERROR,"GL error during program tracking scenarios");
            trace.set(false);supported.values(2,.25f,.75f);require(target.visible(),"normal head did not return after the tracking scenarios");
        }
        require(trace.flush(nullptr)>0,"trace flush returned no captured events");
        std::ifstream log(trace.path);require(log.good(),"trace log missing");const std::string text((std::istreambuf_iterator<char>(log)),{});
        for(const char* token:{"#state\tinitialized","swvr_vr_head_mask","draw_arrays","draw_elements"})require(text.find(token)!=std::string::npos,"integration trace evidence missing");
        // SWVR_PROGRAM_TRACKING=0 runs the same scenarios on the driver-query path.
        std::array<wchar_t,8> tracking{};const bool tracked=!(GetEnvironmentVariableW(L"SWVR_PROGRAM_TRACKING",tracking.data(),8)>0 && std::wstring_view(tracking.data())==L"0");
        require(text.find(tracked?"#gl_program_tracking\ton":"#gl_program_tracking\toff")!=std::string::npos,"program tracking mode not recorded");
        require(text.find("mismatch_reverted")==std::string::npos,"program tracking diverged from the driver and reverted");
        require(text.find("compile_failed_restored_original")!=std::string::npos,"compile fallback evidence missing");
        verify_disabled_guard();
        std::cout<<"PASS head-mask DLL injection: multipart explicit lengths, real shader/draw hooks, far/near pixels, signature gates, normal restore and guarded export\nTrace: "<<trace.path.string()<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
