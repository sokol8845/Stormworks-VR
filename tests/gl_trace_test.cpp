// Hardware integration check: shader + uniform + draw pass through the tracer.
#include <windows.h>
#include <GL/gl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include "swvr/gl_eye_targets.hpp"

template<class T>T gl(const char* name) {
    const auto p=wglGetProcAddress(name);if(!p)throw std::runtime_error(name);return reinterpret_cast<T>(p);
}
int main() {
    try {
        wchar_t exe[MAX_PATH];GetModuleFileNameW(nullptr,exe,MAX_PATH);
        const auto dll=std::filesystem::path(exe).parent_path()/"swvr_gl_trace.dll";
        const auto module=LoadLibraryW(dll.c_str());if(!module)throw std::runtime_error("Load trace DLL");
        using Start=DWORD(WINAPI*)(void*);
        const auto start=reinterpret_cast<Start>(GetProcAddress(module,"swvr_trace_start"));
        const auto flush=reinterpret_cast<Start>(GetProcAddress(module,"swvr_trace_flush"));
        wchar_t external[32768]{};const bool preloaded=GetEnvironmentVariableW(L"SWVR_EXTERNAL_TRACE",external,32768)>0;
        const auto path=preloaded?std::filesystem::path(external):std::filesystem::absolute("artifacts/trace-test.tsv");
        std::filesystem::create_directories(path.parent_path());
        if(!start||!flush||(!preloaded && start(const_cast<wchar_t*>(path.c_str()))))throw std::runtime_error("Start trace");
        {
            std::ifstream startup(path);
            std::string text((std::istreambuf_iterator<char>(startup)),{});
            if(text.find("#state\tinitialized")==std::string::npos)
                throw std::runtime_error("Initialization evidence was not flushed before first GL call");
        }
        auto window=CreateWindowW(L"STATIC",L"Trace test",WS_OVERLAPPEDWINDOW,0,0,32,32,nullptr,nullptr,nullptr,nullptr);
        auto dc=GetDC(window);PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;
        pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.cColorBits=32;
        if(!SetPixelFormat(dc,ChoosePixelFormat(dc,&pfd),&pfd))throw std::runtime_error("Pixel format");
        auto rc=wglCreateContext(dc);if(!wglMakeCurrent(dc,rc))throw std::runtime_error("Context");
        auto gen_fbo=gl<void(APIENTRY*)(GLsizei,GLuint*)>("glGenFramebuffers");
        auto bind_fbo=gl<void(APIENTRY*)(GLenum,GLuint)>("glBindFramebuffer");
        auto attach_texture=gl<void(APIENTRY*)(GLenum,GLenum,GLenum,GLuint,GLint)>("glFramebufferTexture2D");
        GLuint fbo{},texture{};gen_fbo(1,&fbo);bind_fbo(0x8D40,fbo);
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        attach_texture(0x8D40,0x8CE0,GL_TEXTURE_2D,texture,0);
        auto create_shader=gl<GLuint(APIENTRY*)(GLenum)>("glCreateShader");
        auto source=gl<void(APIENTRY*)(GLuint,GLsizei,const char*const*,const GLint*)>("glShaderSource");
        auto compile=gl<void(APIENTRY*)(GLuint)>("glCompileShader");
        auto create_program=gl<GLuint(APIENTRY*)()>("glCreateProgram");
        auto attach=gl<void(APIENTRY*)(GLuint,GLuint)>("glAttachShader");
        auto link=gl<void(APIENTRY*)(GLuint)>("glLinkProgram");
        auto get_program=gl<void(APIENTRY*)(GLuint,GLenum,GLint*)>("glGetProgramiv");
        auto use=gl<void(APIENTRY*)(GLuint)>("glUseProgram");
        auto location=gl<GLint(APIENTRY*)(GLuint,const char*)>("glGetUniformLocation");
        auto uniform=gl<void(APIENTRY*)(GLint,GLsizei,GLboolean,const GLfloat*)>("glUniformMatrix4fv");
        const char* vs="#version 120\nuniform mat4 mat_view_proj; void main(){gl_Position=mat_view_proj*gl_Vertex;}";
        const char* fs="#version 120\nvoid main(){gl_FragColor=vec4(0,1,0,1);}";
        auto vertex=create_shader(0x8B31),fragment=create_shader(0x8B30);
        source(vertex,1,&vs,nullptr);compile(vertex);source(fragment,1,&fs,nullptr);compile(fragment);
        auto program=create_program();attach(program,vertex);attach(program,fragment);link(program);
        GLint linked{};get_program(program,0x8B82,&linked);if(!linked)throw std::runtime_error("Shader link");
        use(program);auto loc=location(program,"mat_view_proj");if(loc<0)throw std::runtime_error("Uniform missing");
        float identity[]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};uniform(loc,1,GL_FALSE,identity);
        glViewport(0,0,32,32);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        float vertices[]{-1,-1,0,1,-1,0,0,1,0};glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3,GL_FLOAT,0,vertices);glDrawArrays(GL_TRIANGLES,0,3);
        unsigned char pixel[4]{};glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        auto error=glGetError();
        if(error!=GL_NO_ERROR||pixel[1]<200||pixel[0]>20)
            throw std::runtime_error("Render check failed: GL="+std::to_string(error)+" RGB="+
                std::to_string(pixel[0])+","+std::to_string(pixel[1])+","+std::to_string(pixel[2]));
        {
            swvr::GlEyeTargets targets;
            const auto gen_buffers=gl<void(APIENTRY*)(GLsizei,GLuint*)>("glGenBuffers");
            const auto bind_buffer=gl<void(APIENTRY*)(GLenum,GLuint)>("glBindBuffer");
            const auto delete_buffers=gl<void(APIENTRY*)(GLsizei,const GLuint*)>("glDeleteBuffers");
            GLuint unpack{};gen_buffers(1,&unpack);bind_buffer(0x88EC,unpack);
            targets.prepare(32,32);
            GLint unpack_after{};glGetIntegerv(0x88EF,&unpack_after);
            if(unpack_after!=static_cast<GLint>(unpack)) throw std::runtime_error("Eye allocation lost unpack buffer binding");
            bind_buffer(0x88EC,0);delete_buffers(1,&unpack);
            glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);glEnable(0x8DB9);
            bind_fbo(0x8CA8,0); // Original read and draw FBOs deliberately differ.
            targets.capture(0,{0,0,32,32});
            GLint read{},draw{};glGetIntegerv(0x8CAA,&read);glGetIntegerv(0x8CA6,&draw);
            if(read!=0 || draw!=static_cast<GLint>(fbo) || !glIsEnabled(GL_SCISSOR_TEST) || !glIsEnabled(0x8DB9))
                throw std::runtime_error("Eye capture changed host GL state");
            glDisable(GL_SCISSOR_TEST);glDisable(0x8DB9);
            glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            targets.capture(1,{0,0,32,32});
            glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);glEnable(0x8DB9);
            targets.blit_to_bound_draw(0,{0,0,16,32});
            targets.blit_to_bound_draw(1,{16,0,16,32});
            glGetIntegerv(0x8CAA,&read);glGetIntegerv(0x8CA6,&draw);
            if(read!=0 || draw!=static_cast<GLint>(fbo) || !glIsEnabled(GL_SCISSOR_TEST) || !glIsEnabled(0x8DB9))
                throw std::runtime_error("Eye presentation changed host GL state");
            bind_fbo(0x8CA8,fbo);
            unsigned char left[4]{},right[4]{};
            glReadPixels(8,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left);
            glReadPixels(24,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);
            if(left[1]<200||left[0]>20||right[0]<200||right[1]>20||glGetError()!=GL_NO_ERROR)
                throw std::runtime_error("Eye images were mixed, clipped or overwritten");
            GLuint pack{};gen_buffers(1,&pack);bind_buffer(0x88EB,pack);
            glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,99);
            glPixelStorei(GL_PACK_SKIP_PIXELS,3);glPixelStorei(GL_PACK_SKIP_ROWS,2);
            const auto saved_right=targets.read_rgb(1);
            GLint pack_after{},row_length{},skip_rows{};
            glGetIntegerv(0x88ED,&pack_after);glGetIntegerv(GL_PACK_ROW_LENGTH,&row_length);
            glGetIntegerv(GL_PACK_SKIP_ROWS,&skip_rows);
            if(saved_right.size()!=32*32*3 || saved_right[0]<200 || saved_right[1]>20 ||
               pack_after!=static_cast<GLint>(pack) || row_length!=99 || skip_rows!=2)
                throw std::runtime_error("Eye diagnostic readback corrupted pack state or data");
            bind_buffer(0x88EB,0);delete_buffers(1,&pack);
            glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);
            glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
            glDisable(GL_SCISSOR_TEST);glDisable(0x8DB9);
        }
        SwapBuffers(dc);flush(nullptr);
        std::ifstream stream(path);if(!stream)throw std::runtime_error("Cannot read live trace");
        std::string text((std::istreambuf_iterator<char>(stream)),{});
        for(auto token:{"uniform","mat_view_proj","matrix4","draw_arrays","swap"})
            if(text.find(token)==std::string::npos)throw std::runtime_error(std::string("Missing trace event ")+token);
        // The real capture reached its limit between swaps. The final buffered
        // records must be visible without another swap or an explicit flush.
        for(int i=0;i<250010;++i) glViewport(0,0,32,32);
        std::ifstream capped(path);
        std::string capped_text((std::istreambuf_iterator<char>(capped)),{});
        if(capped_text.find("#state\tlimit_reached\n")==std::string::npos)
            throw std::runtime_error("Trace limit did not flush completion evidence");
        size_t count{};
        std::ifstream rows(path);std::string row;
        std::getline(rows,row);
        while(std::getline(rows,row)) if(!row.empty() && row.front()!='#') ++count;
        if(count!=250000) throw std::runtime_error("Trace limit lost or exceeded records");
        // A cockpit can load new shaders after the startup capture is full.
        // Their names must survive into an on-demand capture without requerying.
        const auto late_program=create_program();attach(late_program,vertex);attach(late_program,fragment);link(late_program);
        get_program(late_program,0x8B82,&linked);if(!linked)throw std::runtime_error("Late shader link");
        const auto late_location=location(late_program,"mat_view_proj");
        if(late_location<0) throw std::runtime_error("Late uniform missing");
        const auto event_name=L"Local\\StormworksVR.Capture."+std::to_wstring(GetCurrentProcessId());
        const auto request=OpenEventW(EVENT_MODIFY_STATE,FALSE,event_name.c_str());
        if(!request) throw std::runtime_error("Capture control unavailable");
        const auto signaled=SetEvent(request);CloseHandle(request);
        if(!signaled) throw std::runtime_error("Capture request failed");
        SwapBuffers(dc);
        use(late_program);uniform(late_location,1,GL_FALSE,identity);
        glDrawArrays(GL_TRIANGLES,0,3);
        glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        if(pixel[1]<200||pixel[0]>20) throw std::runtime_error("Rearming altered rendering");
        SwapBuffers(dc);
        std::ifstream rearmed(path);
        std::string after((std::istreambuf_iterator<char>(rearmed)),{});
        if(after.compare(0,capped_text.size(),capped_text)!=0)
            throw std::runtime_error("Rearm overwrote previous capture");
        const auto tail=after.substr(capped_text.size());
        const auto seed="\tuniform\t"+std::to_string(late_program)+"\t0\t"+
                        std::to_string(late_program)+"\t"+std::to_string(late_location)+"\t";
        if(tail.find("#capture_epoch\t1\n")==std::string::npos || tail.find(seed)==std::string::npos ||
           tail.find("\tmatrix4\t")==std::string::npos || tail.find("\tdraw_arrays\t")==std::string::npos)
            throw std::runtime_error("Rearm lost epoch, late uniform or rendering events");
        wglMakeCurrent(nullptr,nullptr);wglDeleteContext(rc);ReleaseDC(window,dc);DestroyWindow(window);
        // Do not FreeLibrary: resolved driver wrappers remain callable until process exit.
        std::cout<<"PASS: independent eye targets and GL state, trace cap, rearm and late shader names\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
