#include "driver_wheel_fixture.hpp"
#include "swvr/driver_wheel_gl.hpp"
#include "swvr/driver_wheel_shader.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace swvr;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class T>T proc(const char* name){auto p=wglGetProcAddress(name);check(p&&reinterpret_cast<std::uintptr_t>(p)>3&&p!=reinterpret_cast<PROC>(-1),name);return reinterpret_cast<T>(p);}
void* resolver(const char* name){return reinterpret_cast<void*>(wglGetProcAddress(name));}
Matrix4 fm(const Matrix4d& m){Matrix4 out;for(int i=0;i<16;++i)out[i]=static_cast<float>(m[i]);return out;}
void check_vec(Vec3 a,Vec3 b,const char* why){check(std::abs(a.x-b.x)<.0001f&&std::abs(a.y-b.y)<.0001f&&std::abs(a.z-b.z)<.0001f,why);}
struct Gl {
    HWND window{};HDC dc{};HGLRC context{};
    Gl(){window=CreateWindowW(L"STATIC",L"Driver wheel GPU fixture",WS_OVERLAPPEDWINDOW,0,0,32,32,nullptr,nullptr,nullptr,nullptr);dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.cColorBits=32;
        check(SetPixelFormat(dc,ChoosePixelFormat(dc,&pfd),&pfd)!=0,"pixel format");context=wglCreateContext(dc);check(wglMakeCurrent(dc,context)!=0,"GL context");}
    ~Gl(){wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);}
};
struct Api {
    decltype(proc<GLuint(APIENTRY*)(GLenum)>("")) create=proc<decltype(create)>("glCreateShader");
    void(APIENTRY* source)(GLuint,GLsizei,const char*const*,const GLint*)=proc<decltype(source)>("glShaderSource");
    void(APIENTRY* compile)(GLuint)=proc<decltype(compile)>("glCompileShader");
    void(APIENTRY* shader_int)(GLuint,GLenum,GLint*)=proc<decltype(shader_int)>("glGetShaderiv");
    void(APIENTRY* shader_log)(GLuint,GLsizei,GLsizei*,char*)=proc<decltype(shader_log)>("glGetShaderInfoLog");
    GLuint(APIENTRY* program)()=proc<decltype(program)>("glCreateProgram");
    void(APIENTRY* attach)(GLuint,GLuint)=proc<decltype(attach)>("glAttachShader");
    void(APIENTRY* link)(GLuint)=proc<decltype(link)>("glLinkProgram");
    void(APIENTRY* program_int)(GLuint,GLenum,GLint*)=proc<decltype(program_int)>("glGetProgramiv");
    void(APIENTRY* program_log)(GLuint,GLsizei,GLsizei*,char*)=proc<decltype(program_log)>("glGetProgramInfoLog");
    void(APIENTRY* use)(GLuint)=proc<decltype(use)>("glUseProgram");
    GLint(APIENTRY* location)(GLuint,const char*)=proc<decltype(location)>("glGetUniformLocation");
    void(APIENTRY* upload)(GLint,GLsizei,GLboolean,const GLfloat*)=proc<decltype(upload)>("glUniformMatrix4fv");
    void(APIENTRY* get_int)(GLuint,GLint,GLint*)=proc<decltype(get_int)>("glGetUniformiv");
    void(APIENTRY* varyings)(GLuint,GLsizei,const char*const*,GLenum)=proc<decltype(varyings)>("glTransformFeedbackVaryings");
    void(APIENTRY* buffers)(GLsizei,GLuint*)=proc<decltype(buffers)>("glGenBuffers");
    void(APIENTRY* bind)(GLenum,GLuint)=proc<decltype(bind)>("glBindBuffer");
    void(APIENTRY* data)(GLenum,std::ptrdiff_t,const void*,GLenum)=proc<decltype(data)>("glBufferData");
    void(APIENTRY* bind_base)(GLenum,GLuint,GLuint)=proc<decltype(bind_base)>("glBindBufferBase");
    void(APIENTRY* read)(GLenum,std::ptrdiff_t,std::ptrdiff_t,void*)=proc<decltype(read)>("glGetBufferSubData");
    void(APIENTRY* begin)(GLenum)=proc<decltype(begin)>("glBeginTransformFeedback");
    void(APIENTRY* end)()=proc<decltype(end)>("glEndTransformFeedback");
    GLint(APIENTRY* attribute)(GLuint,const char*)=proc<decltype(attribute)>("glGetAttribLocation");
    void(APIENTRY* pointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*)=proc<decltype(pointer)>("glVertexAttribPointer");
    void(APIENTRY* enable)(GLuint)=proc<decltype(enable)>("glEnableVertexAttribArray");
    void(APIENTRY* disable)(GLuint)=proc<decltype(disable)>("glDisableVertexAttribArray");
    void(APIENTRY* delete_program)(GLuint)=proc<decltype(delete_program)>("glDeleteProgram");
    void(APIENTRY* delete_shader)(GLuint)=proc<decltype(delete_shader)>("glDeleteShader");
    void(APIENTRY* delete_buffers)(GLsizei,const GLuint*)=proc<decltype(delete_buffers)>("glDeleteBuffers");
    GLuint make(const std::string& original,bool depth){
        auto patch=patch_driver_wheel_vertex(original);check(patch.patched,"GPU shader not recognized");
        auto sh=create(0x8B31);const char* text=patch.source.c_str();source(sh,1,&text,nullptr);compile(sh);GLint ok{};shader_int(sh,0x8B81,&ok);
        char log[4096]{};if(!ok){shader_log(sh,sizeof(log),nullptr,log);throw std::runtime_error(log);}
        auto p=program();attach(p,sh);
        const char* outputs[]{"vertex_world_position_out","vertex_normal_out","vertex_position_prev_out","vertex_position_next_out"};const char* clip="gl_Position";
        varyings(p,depth?1:4,depth?&clip:outputs,0x8C8C);link(p);program_int(p,0x8B82,&ok);
        if(!ok){program_log(p,sizeof(log),nullptr,log);throw std::runtime_error(log);}delete_shader(sh);return p;
    }
};
}
int main(int argc,char** argv){try{
    Gl gl;Api api;auto mesh=argc>1?load_driver_seat_mesh(std::filesystem::path(argv[1])/"meshes/component_seat_racing.mesh"):wheel_fixture::mesh();
    const auto source=[&](const char* name,bool depth){return argc>1?"#version 330 core\n"+wheel_fixture::shader(std::filesystem::path(argv[1])/"graphics/shaders"/name):(depth?wheel_fixture::depth():wheel_fixture::opaque());};
    std::ofstream dump;if(argc>2)dump.open(argv[2]);
    GlDriverWheel wheel;wheel.configure(resolver,mesh.points);
    auto component=driver_seat_transform(wheel_fixture::identity(),{.5f,.25f,-.5f},{8,-4,12},{0,0,1,0,1,0,-1,0,0});
    Matrix4d global{0,0,-1,0,0,1,0,0,-1,0,0,0,10000,18,-20000,1};auto model=global;model[12]=model[14]=0;
    auto expected=wheel_fixture::identity();expected[12]=10000;expected[14]=-20000;auto actual=wheel_fixture::identity();
    DriverSeatSnapshot seat{42,driver_wheel_product(global,component),std::chrono::steady_clock::now(),true};
    SteeringCommand command{.5f,{true,true},true,.7f,42};
    std::vector<Vec3> input;for(std::size_t i=0;i<mesh.positions.size();++i){input.push_back(driver_wheel_transform(component,mesh.positions[i]));input.push_back(driver_wheel_transform(component,mesh.normals[i],true));}
    GLuint buffers[2]{};api.buffers(2,buffers);api.bind(0x8892,buffers[0]);api.data(0x8892,static_cast<std::ptrdiff_t>(input.size()*sizeof(Vec3)),input.data(),0x88E4);
    glEnable(0x8C89);std::uint64_t frame{};
    for(int kind=0;kind<4;++kind){
        const bool depth=kind==1||kind==2;const auto p=api.make(source(kind==0?"opaque.glslv":kind==1?"depth.glslv":kind==2?"depth_cheap.glslv":"opaque_damaged.glslv",depth),depth);api.use(p);
        auto originals=[&]{for(const char* name:{"mat_world","mat_world_prev","mat_world_next","mat_view_proj","mat_view_proj_prev","mat_view_proj_next"}){
            const auto loc=api.location(p,name);if(loc>=0){const auto m=fm(std::string_view(name).starts_with("mat_world")?model:wheel_fixture::identity());api.upload(loc,1,GL_FALSE,m.data());wheel.matrix_uploaded(loc,1,false,m.data());}}};originals();
        api.bind(0x8892,buffers[0]);const auto position=api.attribute(p,depth?"vertexPosition_in":"vertex_position_in"),normal=api.attribute(p,"vertex_normal_in");check(position>=0,"position attribute missing");
        api.pointer(position,3,GL_FLOAT,GL_FALSE,2*sizeof(Vec3),nullptr);api.enable(position);
        if(normal>=0){api.pointer(normal,3,GL_FLOAT,GL_FALSE,2*sizeof(Vec3),reinterpret_cast<void*>(sizeof(Vec3)));api.enable(normal);}
        const int stride=depth?4:14;std::vector<float> output(mesh.positions.size()*stride);
        api.bind(0x8C8E,buffers[1]);api.data(0x8C8E,static_cast<std::ptrdiff_t>(output.size()*sizeof(float)),nullptr,0x88E1);api.bind_base(0x8C8E,0,buffers[1]);
        auto render=[&](float angle,float previous,bool enabled){
            {auto draw=wheel.before_draw();GLint active{};api.get_int(p,api.location(p,"swvr_wheel_active"),&active);check((active!=0)==enabled,"wheel draw activation mismatch");
                api.begin(GL_POINTS);glDrawArrays(GL_POINTS,0,static_cast<GLsizei>(mesh.positions.size()));api.end();}
            GLint restored{1};api.get_int(p,api.location(p,"swvr_wheel_active"),&restored);check(restored==0,"wheel uniforms leaked after draw");
            api.read(0x8C8E,0,static_cast<std::ptrdiff_t>(output.size()*sizeof(float)),output.data());check(glGetError()==GL_NO_ERROR,"GL error during wheel deformation");
            for(std::size_t i=0;i<mesh.positions.size();++i){
                const auto transform=driver_wheel_product(model,component);
                const auto pos=mesh.wheel[i]?rotate_driver_wheel(mesh.positions[i],angle):mesh.positions[i];
                check_vec({output[i*stride],output[i*stride+1],output[i*stride+2]},driver_wheel_transform(transform,pos),"GPU wheel position or stationary seat mismatch");
                if(!depth){
                    const auto norm=mesh.wheel[i]?rotate_driver_wheel(mesh.normals[i],angle,true):mesh.normals[i];
                    check_vec({output[i*stride+3],output[i*stride+4],output[i*stride+5]},driver_wheel_transform(transform,norm,true),"GPU wheel normal mismatch");
                    const auto old=mesh.wheel[i]?rotate_driver_wheel(mesh.positions[i],previous):mesh.positions[i];
                    check_vec({output[i*stride+6],output[i*stride+7],output[i*stride+8]},driver_wheel_transform(transform,old),"previous-frame wheel motion mismatch");
                    check_vec({output[i*stride+10],output[i*stride+11],output[i*stride+12]},driver_wheel_transform(transform,pos),"next wheel motion mismatch");
                }
            }
        };
        check(wheel.interest(gl.context,p)==GlProgramInterest::active,"patched wheel program interest");
        auto eye=[&]{seat.observed=std::chrono::steady_clock::now();wheel.begin_eye(seat,command,expected,++frame);wheel.camera_rebased(actual);};
        // Start with neutral history regardless of the preceding program.
        command.active=false;eye();wheel.end_eye();eye();render(0,0,false);wheel.end_eye();
        command.active=true;command.angle_radians=.7f;eye();render(.7f,0,true);
        if(dump&&kind==0)for(std::size_t i=0;i<mesh.positions.size();++i)dump<<"right,"<<i<<','<<output[i*stride]<<','<<output[i*stride+1]<<','<<output[i*stride+2]<<'\n';
        wheel.end_eye();wheel.begin_eye(seat,command,expected,frame);wheel.camera_rebased(actual);render(.7f,0,true);wheel.end_eye(); // right eye shares previous angle
        command.angle_radians=-.7f;eye();render(-.7f,.7f,true);
        if(dump&&kind==0)for(std::size_t i=0;i<mesh.positions.size();++i)dump<<"left,"<<i<<','<<output[i*stride]<<','<<output[i*stride+1]<<','<<output[i*stride+2]<<'\n';
        wheel.end_eye();command.active=false;eye();render(0,-.7f,true);
        if(dump&&kind==0)for(std::size_t i=0;i<mesh.positions.size();++i)dump<<"neutral,"<<i<<','<<output[i*stride]<<','<<output[i*stride+1]<<','<<output[i*stride+2]<<'\n';
        wheel.end_eye();eye();render(0,0,false);wheel.end_eye();
        command.active=true;command.angle_radians=2.8f;eye();render(2.8f,0,true);wheel.end_eye();
        command.active=false;eye();render(0,2.8f,true);wheel.end_eye();eye();render(0,0,false);wheel.end_eye();
        command.active=true;command.angle_radians=.7f;seat.observed=std::chrono::steady_clock::now();wheel.begin_eye(seat,command,expected,++frame);render(0,0,false);wheel.end_eye(); // no camera rebase
        seat.observed-=std::chrono::seconds(1);wheel.begin_eye(seat,command,expected,++frame);wheel.camera_rebased(actual);render(0,0,false);wheel.end_eye();
        command.seat=43;eye();render(0,0,false);wheel.end_eye();command.seat=42;
        eye();const auto loc=api.location(p,"mat_world");wheel.matrix_uploaded(loc,1,true,fm(model).data());render(0,0,false);originals();render(.7f,0,true);wheel.end_eye();
        wheel.program_linked(p);eye();render(0,0,false);originals();render(.7f,.7f,true);wheel.end_eye();
        api.disable(position);if(normal>=0)api.disable(normal);api.delete_program(p);wheel.program_linked(p);check(wheel.interest(gl.context,p)==GlProgramInterest::unknown,"link did not reset wheel program interest");
    }
    {const char* text="#version 330 core\nuniform mat4 mat_world;void main(){gl_Position=mat_world*vec4(0,0,0,1);}";const auto sh=api.create(0x8B31);api.source(sh,1,&text,nullptr);api.compile(sh);
     const auto plain=api.program();api.attach(plain,sh);api.link(plain);GLint linked{};api.program_int(plain,0x8B82,&linked);check(linked!=0,"plain program link");api.use(plain);
     check(wheel.interest(gl.context,plain)==GlProgramInterest::unknown,"unseen program interest");
     const auto plain_world=api.location(plain,"mat_world");const auto plain_model=fm(model);api.upload(plain_world,1,GL_FALSE,plain_model.data());wheel.matrix_uploaded(plain_world,1,false,plain_model.data());
     check(wheel.interest(gl.context,plain)==GlProgramInterest::none,"unpatched program not reported as skippable");
     api.use(0);api.delete_shader(sh);api.delete_program(plain);wheel.program_linked(plain);}
    wheel.context_destroyed(gl.context);api.delete_buffers(2,buffers);
    std::cout<<"PASS GPU wheel positions/normals, depth, motion history, stereo, release, foreign/stale seat, rebase and GL cleanup; vertices="<<mesh.positions.size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
