#include "swvr/gl_hand_overlay.hpp"
#include "swvr/vr_settings_menu.hpp"
#include "swvr/gl_gpu_timer.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
constexpr GLenum framebuffer=0x8D40,draw_fb=0x8CA9,read_fb=0x8CA8,color0=0x8CE0,srgb=0x8DB9,array_buffer=0x8892;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class F>void throws(F&& fn,const char* message) {bool caught{};try{fn();}catch(const std::exception&){caught=true;}require(caught,message);}
template<class T>T function(const char* name) {
    const auto p=wglGetProcAddress(name);require(p && p!=reinterpret_cast<PROC>(-1),name);return reinterpret_cast<T>(p);
}
struct Context {
    HWND window{};HDC dc{};HGLRC gl{};
    Context() {
        window=CreateWindowExW(0,L"STATIC",L"SWVR hidden hand GPU test",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"test window");dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pf{};pf.nSize=sizeof(pf);pf.nVersion=1;pf.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        pf.iPixelType=PFD_TYPE_RGBA;pf.cColorBits=32;
        const int format=ChoosePixelFormat(dc,&pf);require(format && SetPixelFormat(dc,format,&pf),"pixel format");
        gl=wglCreateContext(dc);require(gl && wglMakeCurrent(dc,gl),"bootstrap context");
        using Create=HGLRC(WINAPI*)(HDC,HGLRC,const int*);
        const auto create=function<Create>("wglCreateContextAttribsARB");
        const int attributes[]{0x2091,4,0x2092,1,0x9126,1,0}; // OpenGL 4.1 core profile
        const auto core=create(dc,nullptr,attributes);require(core!=nullptr,"core GL context");
        wglMakeCurrent(nullptr,nullptr);wglDeleteContext(gl);gl=core;require(wglMakeCurrent(dc,gl)!=0,"current core context");
    }
    ~Context(){wglMakeCurrent(nullptr,nullptr);if(gl)wglDeleteContext(gl);if(dc)ReleaseDC(window,dc);if(window)DestroyWindow(window);}
};
struct Gl {
    void(APIENTRY* bind)(GLenum,GLuint)=function<decltype(bind)>("glBindFramebuffer");
    void(APIENTRY* gen)(GLsizei,GLuint*)=function<decltype(gen)>("glGenFramebuffers");
    void(APIENTRY* remove)(GLsizei,const GLuint*)=function<decltype(remove)>("glDeleteFramebuffers");
    void(APIENTRY* attach)(GLenum,GLenum,GLenum,GLuint,GLint)=function<decltype(attach)>("glFramebufferTexture2D");
    GLenum(APIENTRY* status)(GLenum)=function<decltype(status)>("glCheckFramebufferStatus");
    void(APIENTRY* gen_vao)(GLsizei,GLuint*)=function<decltype(gen_vao)>("glGenVertexArrays");
    void(APIENTRY* delete_vao)(GLsizei,const GLuint*)=function<decltype(delete_vao)>("glDeleteVertexArrays");
    void(APIENTRY* bind_vao)(GLuint)=function<decltype(bind_vao)>("glBindVertexArray");
    void(APIENTRY* gen_buffer)(GLsizei,GLuint*)=function<decltype(gen_buffer)>("glGenBuffers");
    void(APIENTRY* delete_buffer)(GLsizei,const GLuint*)=function<decltype(delete_buffer)>("glDeleteBuffers");
    void(APIENTRY* bind_buffer)(GLenum,GLuint)=function<decltype(bind_buffer)>("glBindBuffer");
    void(APIENTRY* enable_i)(GLenum,GLuint)=function<decltype(enable_i)>("glEnablei");
    void(APIENTRY* disable_i)(GLenum,GLuint)=function<decltype(disable_i)>("glDisablei");
    GLboolean(APIENTRY* enabled_i)(GLenum,GLuint)=function<decltype(enabled_i)>("glIsEnabledi");
    void(APIENTRY* get_boolean_i)(GLenum,GLuint,GLboolean*)=function<decltype(get_boolean_i)>("glGetBooleani_v");
    void(APIENTRY* get_float_i)(GLenum,GLuint,GLfloat*)=function<decltype(get_float_i)>("glGetFloati_v");
    void(APIENTRY* get_integer_i)(GLenum,GLuint,GLint*)=function<decltype(get_integer_i)>("glGetIntegeri_v");
    void(APIENTRY* mask_i)(GLuint,GLboolean,GLboolean,GLboolean,GLboolean)=function<decltype(mask_i)>("glColorMaski");
    void(APIENTRY* viewport_i)(GLuint,GLfloat,GLfloat,GLfloat,GLfloat)=function<decltype(viewport_i)>("glViewportIndexedf");
    void(APIENTRY* scissor_i)(GLuint,GLint,GLint,GLsizei,GLsizei)=function<decltype(scissor_i)>("glScissorIndexed");
    void(APIENTRY* blend_func)(GLenum,GLenum,GLenum,GLenum)=function<decltype(blend_func)>("glBlendFuncSeparate");
    void(APIENTRY* blend_equation)(GLenum,GLenum)=function<decltype(blend_equation)>("glBlendEquationSeparate");
    GLuint(APIENTRY* create_shader)(GLenum)=function<decltype(create_shader)>("glCreateShader");
    void(APIENTRY* shader_source)(GLuint,GLsizei,const char* const*,const GLint*)=function<decltype(shader_source)>("glShaderSource");
    void(APIENTRY* compile_shader)(GLuint)=function<decltype(compile_shader)>("glCompileShader");
    void(APIENTRY* delete_shader)(GLuint)=function<decltype(delete_shader)>("glDeleteShader");
    GLuint(APIENTRY* create_program)()=function<decltype(create_program)>("glCreateProgram");
    void(APIENTRY* attach_shader)(GLuint,GLuint)=function<decltype(attach_shader)>("glAttachShader");
    void(APIENTRY* link_program)(GLuint)=function<decltype(link_program)>("glLinkProgram");
    void(APIENTRY* get_program)(GLuint,GLenum,GLint*)=function<decltype(get_program)>("glGetProgramiv");
    void(APIENTRY* delete_program)(GLuint)=function<decltype(delete_program)>("glDeleteProgram");
    void(APIENTRY* use_program)(GLuint)=function<decltype(use_program)>("glUseProgram");
};
struct Image {
    Gl& api;GLuint texture{},fbo{};static constexpr int size=128;
    static std::vector<unsigned char> background() {
        std::vector<unsigned char> pixels(size*size*4);
        for(std::size_t i=0;i<pixels.size();i+=4){pixels[i]=7;pixels[i+1]=11;pixels[i+2]=19;pixels[i+3]=255;}return pixels;
    }
    Image(Gl& api,GLint format):api(api) {
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);const auto pixels=background();
        glTexImage2D(GL_TEXTURE_2D,0,format,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        api.gen(1,&fbo);api.bind(framebuffer,fbo);api.attach(framebuffer,color0,GL_TEXTURE_2D,texture,0);glDrawBuffer(color0);glReadBuffer(color0);
        require(api.status(framebuffer)==0x8CD5,"test image complete");
    }
    ~Image(){api.remove(1,&fbo);glDeleteTextures(1,&texture);}
    void reset(){glBindTexture(GL_TEXTURE_2D,texture);const auto pixels=background();glTexSubImage2D(GL_TEXTURE_2D,0,0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());}
    std::vector<unsigned char> read(){api.bind(read_fb,fbo);glReadBuffer(color0);std::vector<unsigned char> pixels(size*size*4);glReadPixels(0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;}
};
struct Host {
    Gl& api;GLuint vao{},buffer{},program{};
    explicit Host(Gl& api):api(api) {
        api.gen_vao(1,&vao);api.gen_buffer(1,&buffer);
        const char* vertex="#version 330 core\nvoid main(){gl_Position=vec4(0,0,0,1);}";
        const char* fragment="#version 330 core\nout vec4 color;void main(){color=vec4(1);}";
        const GLuint vs=api.create_shader(0x8B31),fs=api.create_shader(0x8B30);
        api.shader_source(vs,1,&vertex,nullptr);api.compile_shader(vs);api.shader_source(fs,1,&fragment,nullptr);api.compile_shader(fs);
        program=api.create_program();api.attach_shader(program,vs);api.attach_shader(program,fs);api.link_program(program);
        GLint ok{};api.get_program(program,0x8B82,&ok);require(ok!=0,"host program link");api.delete_shader(vs);api.delete_shader(fs);
    }
    ~Host(){api.use_program(0);api.delete_program(program);api.bind_vao(0);api.delete_vao(1,&vao);api.bind_buffer(array_buffer,0);api.delete_buffer(1,&buffer);}
    void set(Image& read,Image& draw) {
        api.bind(read_fb,read.fbo);glReadBuffer(GL_NONE);api.bind(draw_fb,draw.fbo);glDrawBuffer(color0);
        glBindTexture(GL_TEXTURE_2D,read.texture);api.bind_vao(vao);api.bind_buffer(array_buffer,buffer);api.use_program(program);
        api.viewport_i(0,3,4,7,8);api.viewport_i(1,11,12,13,14);
        api.scissor_i(0,1,2,1,1);api.scissor_i(1,5,6,7,8);api.enable_i(GL_SCISSOR_TEST,0);api.disable_i(GL_SCISSOR_TEST,1);
        api.enable_i(GL_BLEND,0);api.disable_i(GL_BLEND,1);api.blend_func(GL_DST_COLOR,GL_SRC_COLOR,GL_ONE_MINUS_SRC_ALPHA,GL_ONE);api.blend_equation(0x800A,0x800B);
        api.mask_i(0,GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);api.mask_i(1,GL_TRUE,GL_FALSE,GL_TRUE,GL_FALSE);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glEnable(srgb);glEnable(0x8C89);glEnable(GL_COLOR_LOGIC_OP);glEnable(GL_LINE_SMOOTH);
        glEnable(GL_STENCIL_TEST);glStencilFunc(GL_NEVER,1,255);glEnable(0x3000);glEnable(0x864F);
        glEnable(GL_CULL_FACE);glCullFace(GL_FRONT);glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);glEnable(0x809E);glEnable(0x80A0);glEnable(0x8E51);
        GLfloat range[2]{};glGetFloatv(0x846E,range);glLineWidth(range[1]>=2?2.f:1.f);
    }
};
struct State {
    std::array<GLint,16> integers{};
    std::array<GLfloat,8> viewports{};
    std::array<GLint,8> scissor_boxes{};
    std::array<GLboolean,8> masks{};
    std::array<GLboolean,17> flags{};std::array<GLint,2> polygon{};
    GLfloat line_width{};
    explicit State(Gl& gl) {
        constexpr std::array<GLenum,16> names{0x8CAA,0x8CA6,GL_READ_BUFFER,0x8825,0x8826,GL_TEXTURE_BINDING_2D,0x8B8D,0x85B5,0x8894,0x80C9,0x80C8,0x80CB,0x80CA,0x8009,0x883D,GL_DEPTH_FUNC};
        for(std::size_t i=0;i<names.size();++i)glGetIntegerv(names[i],&integers[i]);
        for(unsigned i=0;i<2;++i){gl.get_float_i(GL_VIEWPORT,i,viewports.data()+i*4);gl.get_integer_i(GL_SCISSOR_BOX,i,scissor_boxes.data()+i*4);gl.get_boolean_i(GL_COLOR_WRITEMASK,i,masks.data()+i*4);flags[i]=gl.enabled_i(GL_BLEND,i);flags[i+2]=gl.enabled_i(GL_SCISSOR_TEST,i);}
        flags[4]=glIsEnabled(GL_DEPTH_TEST);glGetBooleanv(GL_DEPTH_WRITEMASK,&flags[5]);flags[6]=glIsEnabled(srgb);flags[7]=glIsEnabled(0x8C89);flags[8]=glIsEnabled(GL_COLOR_LOGIC_OP);flags[9]=glIsEnabled(GL_LINE_SMOOTH);glGetFloatv(GL_LINE_WIDTH,&line_width);
        flags[10]=glIsEnabled(GL_STENCIL_TEST);flags[11]=glIsEnabled(0x3000);flags[12]=glIsEnabled(0x864F);
        flags[13]=glIsEnabled(GL_CULL_FACE);flags[14]=glIsEnabled(0x809E);flags[15]=glIsEnabled(0x80A0);flags[16]=glIsEnabled(0x8E51);glGetIntegerv(GL_POLYGON_MODE,polygon.data());
    }
    bool operator==(const State&) const=default;
};
struct Pixels {int count{};double x{},y{};};
Pixels cyan(const std::vector<unsigned char>& pixels) {
    Pixels result;
    for(int y=0;y<Image::size;++y)for(int x=0;x<Image::size;++x){const auto i=(y*Image::size+x)*4;if(pixels[i]<50 && pixels[i+1]>220 && pixels[i+2]>220){++result.count;result.x+=x;result.y+=y;}}
    if(result.count){result.x/=result.count;result.y/=result.count;}return result;
}
struct RayPixels {int count{},rightmost{-1};};
RayPixels green_ray(const std::vector<unsigned char>& pixels) {
    RayPixels result;
    for(int y=0;y<Image::size;++y)for(int x=0;x<Image::size;++x){const auto i=(y*Image::size+x)*4;
        if(pixels[i]<70 && pixels[i+1]>240 && pixels[i+2]>50 && pixels[i+2]<120){++result.count;result.rightmost=std::max(result.rightmost,x);}}
    return result;
}
int rightmost_geometry(const std::vector<unsigned char>& pixels) {
    int result=-1;
    for(int y=0;y<Image::size;++y)for(int x=0;x<Image::size;++x){const auto i=(y*Image::size+x)*4;
        if(pixels[i]!=7 || pixels[i+1]!=11 || pixels[i+2]!=19)result=std::max(result,x);}
    return result;
}
}
int main(int argc,char** argv) {
    try {
        Context context;Gl gl;Image left(gl,GL_RGBA8),right(gl,0x8C43),host_read(gl,GL_RGBA8),host_draw(gl,GL_RGBA8);Host host(gl);
        swvr::XrRuntimeFrame frame{};frame.id=1;frame.focused=frame.running=frame.should_render=frame.position_valid=frame.orientation_valid=true;
        for(unsigned i=0;i<2;++i){auto& eye=frame.eyes[i];eye.texture=i==0?left.texture:right.texture;eye.width=eye.height=Image::size;eye.pose.position.x=i==0?-.032f:.032f;eye.fov={-.785398163f,.785398163f,.785398163f,-.785398163f};}
        const auto joint=[&](unsigned hand,unsigned index,swvr::Vec3 point){auto& j=frame.hands[hand].joints[index];frame.hands[hand].active=true;j.pose.position=point;j.position_valid=j.position_tracked=true;};
        joint(0,0,{0,-.2f,-1});joint(0,1,{0,.2f,-1});
        {
            swvr::GlHandOverlay overlay;host.set(host_read,host_draw);const State before(gl);overlay.draw(frame);
            require(State(gl)==before,"overlay changed host GL state");
            const auto a=cyan(left.read()),b=cyan(right.read());require(a.count>=20 && b.count>=20,"tracked hand pixels missing");
            require(a.x>b.x+3 && std::abs((a.x-b.x)-4.096)<1.5,"eye parallax wrong");
            require(std::abs(a.x-65.548)<1 && std::abs(b.x-61.452)<1 && std::abs(a.y-63.5)<1,"LOCAL-to-eye projection wrong");
            // A dashboard may keep reporting tracked hands after taking focus.
            left.reset();right.reset();frame.focused=false;host.set(host_read,host_draw);const State unfocused(gl);overlay.draw(frame,true);
            require(State(gl)==unfocused&&left.read()==Image::background()&&right.read()==Image::background(),"unfocused tracked hands remained visible");
            frame.focused=true;overlay.draw(frame);require(cyan(left.read()).count>=20,"hands failed to return with focus");
            {
                auto blank=frame;blank.hands={};left.reset();right.reset();host.set(host_read,host_draw);
                overlay.comfort({0,1,25});const State vignette_state(gl);swvr::GlGpuTimer timer;timer.begin(42,11.111);overlay.draw(blank);timer.end();
                require(State(gl)==vignette_state,"comfort or GPU queries changed host state");
                const auto l=left.read(),r=right.read();const auto centre=(Image::size/2*Image::size+Image::size/2)*4;
                require(l==r&&l[0]==0&&l[centre]==7&&l[centre+1]==11,"angular vignette differs across eyes or covers centre");
                blank.eyes[0].pose.orientation={0,std::sin(.1f),0,std::cos(.1f)};blank.eyes[1].pose.orientation={0,-std::sin(.1f),0,std::cos(.1f)};
                left.reset();right.reset();overlay.draw(blank);const auto canted_left=left.read(),canted_right=right.read();
                for(int y=0;y<Image::size;++y)for(int x=0;x<Image::size;++x)for(int c=0;c<3;++c)require(std::abs(int(canted_left[(y*Image::size+x)*4+c])-int(canted_right[(y*Image::size+Image::size-1-x)*4+c]))<=1,"vignette axes diverged for canted eyes");
                blank.eyes[0].pose.orientation=blank.eyes[1].pose.orientation={};
                glFinish(); // Test only: production polls availability without waiting.
                const auto timing=timer.poll();require(timing&&timing->frame==42&&timing->milliseconds>=0&&timing->budget_ms==11.111&&timer.pending()==0,"GPU timer missing or misattributed result");
                for(unsigned n=0;n<17;++n){timer.begin(n,8);timer.end();}require(timer.pending()==16&&timer.skipped()==1,"full GPU ring did not skip instrumentation");
                glFinish();unsigned samples{};while(timer.poll())++samples;require(samples==16,"GPU pending samples lost");timer.begin(99,8);timer.cancel();require(!timer.pending()&&timer.skipped()==2,"cancelled GPU scope reported as a complete frame");
                left.reset();right.reset();overlay.comfort({1,0,40});host.set(host_read,host_draw);const State fade_state(gl);overlay.draw(blank);require(State(gl)==fade_state,"head fade changed host state");
                const auto faded=left.read();for(std::size_t i=0;i<faded.size();i+=4)require(faded[i]==0&&faded[i+1]==0&&faded[i+2]==0&&faded[i+3]==255,"head fade incomplete or changed alpha");
                overlay.comfort({});left.reset();right.reset();overlay.draw(blank);require(left.read()==Image::background(),"comfort effect did not clear");
            }
            // The right hand's little distal/tip edge proves joint 25 participates.
            left.reset();right.reset();frame.hands={};joint(1,24,{.2f,-.1f,-1});joint(1,25,{.2f,.1f,-1});
            host.set(host_read,host_draw);overlay.draw(frame);const auto orange=left.read();bool found_orange{};
            for(std::size_t i=0;i<orange.size();i+=4)found_orange|=orange[i]>240 && orange[i+1]>140 && orange[i+1]<190 && orange[i+2]<60;
            require(found_orange,"right little fingertip edge missing");
            // Status flash (e.g. T-pose calibration accepted/rejected) recolours both hands, then expires.
            left.reset();right.reset();overlay.flash({1.f,.1f,.1f},std::chrono::milliseconds(80));host.set(host_read,host_draw);overlay.draw(frame);
            {const auto red=left.read();bool found_red{},still_orange{};
             for(std::size_t i=0;i<red.size();i+=4){found_red|=red[i]>240&&red[i+1]<60&&red[i+2]<60;still_orange|=red[i]>240&&red[i+1]>140&&red[i+1]<190&&red[i+2]<60;}
             require(found_red&&!still_orange,"status flash not drawn over both hands");}
            Sleep(120);left.reset();right.reset();host.set(host_read,host_draw);overlay.draw(frame);
            {const auto after=left.read();bool found_orange_again{};
             for(std::size_t i=0;i<after.size();i+=4)found_orange_again|=after[i]>240&&after[i+1]>140&&after[i+1]<190&&after[i+2]<60;
             require(found_orange_again,"status flash did not expire");}
            // Head-locked status frame in the centre of view (visible even when the
            // hands are outside the field of view, e.g. during a T-pose).
            {
                swvr::XrRuntimeFrame empty=frame;empty.hands={};
                const auto green_centre=[&](const std::vector<unsigned char>& p){
                    int count{};for(int y=Image::size/4;y<3*Image::size/4;++y)for(int x=Image::size/4;x<3*Image::size/4;++x){
                        const auto i=(y*Image::size+x)*4;count+=p[i]<80&&p[i+1]>200&&p[i+2]<120;}return count;};
                left.reset();right.reset();overlay.status_frame({.15f,1.f,.25f},std::chrono::milliseconds(80));host.set(host_read,host_draw);
                const State host_state(gl);overlay.draw(empty);
                require(State(gl)==host_state,"status frame changed host GL state");
                require(green_centre(left.read())>=20&&green_centre(right.read())>=20,"status frame missing from the centre of both eyes");
                Sleep(120);left.reset();right.reset();host.set(host_read,host_draw);overlay.draw(empty);
                require(green_centre(left.read())==0,"status frame did not expire");
                const auto yellow=[](const std::vector<unsigned char>& pixels){int count{};for(std::size_t i=0;i<pixels.size();i+=4)count+=pixels[i]>200&&pixels[i+1]>140&&pixels[i+2]<80;return count;};
                left.reset();right.reset();overlay.progress(.25f,std::chrono::milliseconds(80));host.set(host_read,host_draw);const State progress_state(gl);overlay.draw(empty);
                require(State(gl)==progress_state,"progress bar changed host GL state");const auto quarter=yellow(left.read());
                left.reset();right.reset();overlay.progress(.75f,std::chrono::milliseconds(80));host.set(host_read,host_draw);overlay.draw(empty);
                require(yellow(left.read())>quarter&&yellow(right.read())>quarter,"progress fill did not increase in both eyes");
                Sleep(120);left.reset();right.reset();host.set(host_read,host_draw);overlay.draw(empty);
                require(left.read()==Image::background(),"progress bar did not expire");
                swvr::KeypadSnapshot panel;panel.visible=true;panel.space_revision=empty.space_revision;panel.panel.position={0,0,-1.f};panel.value="-12.3";panel.hover=14;
                panel.title="KLAWIATURA - PROTOTYP";for(const auto& key:swvr::numeric_keypad_buttons())panel.buttons.push_back({static_cast<int>(key.key),std::string(key.label),key.x,key.y,key.width,key.height});
                overlay.keypad(panel);left.reset();right.reset();host.set(host_read,host_draw);const State keypad_state(gl);overlay.draw(empty);
                require(State(gl)==keypad_state,"keypad prototype changed host GL state");
                const auto keypad_left=left.read(),keypad_right=right.read();
                require(keypad_left!=Image::background()&&keypad_right!=Image::background(),"keypad panel absent from one eye");
                require(green_ray(keypad_left).count>5&&green_ray(keypad_right).count>5,"hovered keypad button did not highlight");
                left.reset();right.reset();host.set(host_read,host_draw);overlay.draw(empty);
                require(left.read()==keypad_left&&right.read()==keypad_right,"reused text geometry changed an identical panel");
                auto changed_panel=panel;changed_panel.value="98";changed_panel.hover=1;
                changed_panel.panel.orientation={0,.1f,0,.995f};changed_panel.panel.position.x=.08f;
                overlay.keypad(changed_panel);left.reset();right.reset();host.set(host_read,host_draw);
                const State changed_state(gl);overlay.draw(empty);
                require(State(gl)==changed_state,"updated text panel changed host GL state");
                require(left.read()!=keypad_left&&right.read()!=keypad_right,"text cache retained old value, pose or highlight");
                overlay.keypad(panel);left.reset();right.reset();host.set(host_read,host_draw);overlay.draw(empty);
                require(left.read()==keypad_left&&right.read()==keypad_right,"text cache failed to restore the original panel");
                if(argc>1){const auto path=std::filesystem::path(argv[1])/"keypad-prototype-20261104.ppm";std::filesystem::create_directories(path.parent_path());
                    constexpr int extent=1024;std::array<GLuint,2> textures{};glGenTextures(2,textures.data());GLint old_texture{};glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_texture);
                    auto preview=empty;std::vector<unsigned char> pixels(static_cast<std::size_t>(extent)*extent*4);
                    for(unsigned i=0;i<2;++i){glBindTexture(GL_TEXTURE_2D,textures[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,extent,extent,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());preview.eyes[i].texture=textures[i];preview.eyes[i].width=preview.eyes[i].height=extent;}
                    overlay.draw(preview);glBindTexture(GL_TEXTURE_2D,textures[0]);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                    std::vector<unsigned char> rgb(static_cast<std::size_t>(extent)*extent*3);
                    for(int y=0;y<extent;++y)for(int x=0;x<extent;++x)for(unsigned channel=0;channel<3;++channel)
                        rgb[static_cast<std::size_t>((y*extent+x)*3)+channel]=pixels[static_cast<std::size_t>(((extent-1-y)*extent+x)*4)+channel];
                    std::ofstream image(path,std::ios::binary);image<<"P6\n"<<extent<<' '<<extent<<"\n255\n";image.write(reinterpret_cast<const char*>(rgb.data()),static_cast<std::streamsize>(rgb.size()));
                    preview.focused=preview.position_tracked=preview.orientation_tracked=true;
                    swvr::NumericKeypad vehicle_pad;require(vehicle_pad.open_vehicle(preview,{},6,{12.5f,-3.f}),"vehicle keypad snapshot could not open");overlay.keypad(vehicle_pad.snapshot());
                    std::fill(pixels.begin(),pixels.end(),0);for(const auto texture:textures){glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,extent,extent,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());}
                    const State vehicle_state(gl);overlay.draw(preview);require(State(gl)==vehicle_state,"vehicle keypad changed host GL state");glBindTexture(GL_TEXTURE_2D,textures[0]);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                    for(int y=0;y<extent;++y)for(int x=0;x<extent;++x)for(unsigned channel=0;channel<3;++channel)rgb[static_cast<std::size_t>((y*extent+x)*3)+channel]=pixels[static_cast<std::size_t>(((extent-1-y)*extent+x)*4)+channel];
                    std::ofstream vehicle_image(std::filesystem::path(argv[1])/"vehicle-keypad-20261106.ppm",std::ios::binary);vehicle_image<<"P6\n"<<extent<<' '<<extent<<"\n255\n";vehicle_image.write(reinterpret_cast<const char*>(rgb.data()),static_cast<std::streamsize>(rgb.size()));
                    swvr::VrSettingsMenu menu;require(menu.open(preview,{true,true,true},{},false),"GPU menu snapshot could not open");overlay.keypad(menu.snapshot());
                    std::fill(pixels.begin(),pixels.end(),0);for(const auto texture:textures){glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,extent,extent,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());}
                    const State menu_state(gl);overlay.draw(preview);require(State(gl)==menu_state,"menu changed host GL state");glBindTexture(GL_TEXTURE_2D,textures[0]);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                    for(int y=0;y<extent;++y)for(int x=0;x<extent;++x)for(unsigned channel=0;channel<3;++channel)rgb[static_cast<std::size_t>((y*extent+x)*3)+channel]=pixels[static_cast<std::size_t>(((extent-1-y)*extent+x)*4)+channel];
                    std::ofstream menu_image(std::filesystem::path(argv[1])/"vr-settings-menu-20261104.ppm",std::ios::binary);menu_image<<"P6\n"<<extent<<' '<<extent<<"\n255\n";menu_image.write(reinterpret_cast<const char*>(rgb.data()),static_cast<std::streamsize>(rgb.size()));
                    // At 128 px the 5 mm bar inset shares the border's raster
                    // row. Verify progress at representative XR resolution.
                    const auto progress_pixels=[&](float progress){auto snapshot=menu.snapshot();snapshot.hover=0;snapshot.dwell_progress=progress;overlay.keypad(snapshot);
                        std::fill(pixels.begin(),pixels.end(),0);for(const auto texture:textures){glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,extent,extent,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());}
                        const State before_progress(gl);overlay.draw(preview);require(State(gl)==before_progress,"dwell progress changed host GL state");std::array<unsigned,2> count{};
                        for(unsigned eye=0;eye<2;++eye){glBindTexture(GL_TEXTURE_2D,textures[eye]);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());for(std::size_t i=0;i<pixels.size();i+=4)count[eye]+=pixels[i]<70&&pixels[i+1]>240&&pixels[i+2]>50&&pixels[i+2]<120;}
                        return count;};
                    const auto half_progress=progress_pixels(.5f),full_progress=progress_pixels(.9f);
                    require(full_progress[0]>half_progress[0]&&full_progress[1]>half_progress[1],"point-and-hold progress did not fill in both XR eyes");
                    // Open the controls page with an actual fingertip touch, then export it.
                    const auto& controls_button=*std::find_if(menu.snapshot().buttons.begin(),menu.snapshot().buttons.end(),[](const auto& b){return b.id==static_cast<int>(swvr::VrMenuAction::controls);});
                    const auto controls_pose=menu.snapshot().panel;const float controls_x=controls_button.x,controls_y=controls_button.y;
                    auto& touch=preview.hands[1];touch.active=true;touch.joints[10].position_valid=touch.joints[10].position_tracked=true;
                    for(const float z:{.06f,.006f}){touch.joints[10].pose.position=swvr::compose(controls_pose,swvr::Pose{{},{controls_x,controls_y,z}}).position;++preview.id;preview.predicted_display_time_ns+=20'000'000;menu.update(preview);}
                    require(menu.snapshot().title=="OBROT I MENU","controls page touch failed");menu.status({},false);preview.hands={};overlay.keypad(menu.snapshot());
                    std::fill(pixels.begin(),pixels.end(),0);for(const auto texture:textures){glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,extent,extent,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());}
                    const State controls_state(gl);overlay.draw(preview);require(State(gl)==controls_state,"controls page changed host GL state");glBindTexture(GL_TEXTURE_2D,textures[0]);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                    for(int y=0;y<extent;++y)for(int x=0;x<extent;++x)for(unsigned channel=0;channel<3;++channel)rgb[static_cast<std::size_t>((y*extent+x)*3)+channel]=pixels[static_cast<std::size_t>(((extent-1-y)*extent+x)*4)+channel];
                    std::ofstream controls_image(std::filesystem::path(argv[1])/"vr-controls-menu-20261105.ppm",std::ios::binary);controls_image<<"P6\n"<<extent<<' '<<extent<<"\n255\n";controls_image.write(reinterpret_cast<const char*>(rgb.data()),static_cast<std::streamsize>(rgb.size()));
                    const auto navigate=[&](swvr::VrMenuAction action){const auto found=std::find_if(menu.snapshot().buttons.begin(),menu.snapshot().buttons.end(),[&](const auto& b){return b.id==static_cast<int>(action);});require(found!=menu.snapshot().buttons.end(),"GPU navigation target missing");const auto button=*found;const auto p=menu.snapshot().panel;
                        auto& h=preview.hands[1];h.active=true;h.joints[10].position_valid=h.joints[10].position_tracked=true;for(float z:{.06f,.006f}){h.joints[10].pose.position=swvr::compose(p,swvr::Pose{{},{button.x,button.y,z}}).position;++preview.id;preview.predicted_display_time_ns+=20'000'000;menu.update(preview);}preview.hands={};};
                    const auto export_page=[&](const char* name){overlay.keypad(menu.snapshot());std::fill(pixels.begin(),pixels.end(),0);for(const auto texture:textures){glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,extent,extent,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());}
                        const State page_state(gl);overlay.draw(preview);require(State(gl)==page_state,"settings page changed host state");glBindTexture(GL_TEXTURE_2D,textures[0]);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                        const auto expected=pixels;
                        for(unsigned repeat=0;repeat<3;++repeat){std::fill(pixels.begin(),pixels.end(),0);for(const auto texture:textures){glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,extent,extent,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());}
                            const State repeated_state(gl);overlay.draw(preview);require(State(gl)==repeated_state,"repeated settings page changed host state");glBindTexture(GL_TEXTURE_2D,textures[0]);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());require(pixels==expected,"settings page flickered across identical frames");}
                        for(int y=0;y<extent;++y)for(int x=0;x<extent;++x)for(unsigned c=0;c<3;++c)rgb[static_cast<std::size_t>((y*extent+x)*3)+c]=pixels[static_cast<std::size_t>(((extent-1-y)*extent+x)*4)+c];
                        std::ofstream file(std::filesystem::path(argv[1])/name,std::ios::binary);file<<"P6\n"<<extent<<' '<<extent<<"\n255\n";file.write(reinterpret_cast<const char*>(rgb.data()),static_cast<std::streamsize>(rgb.size()));};
                    swvr::GestureSettings accessible;accessible.input.menu_scale=1.4f;accessible.input.dwell_ms=600;accessible.input.click_sound=true;accessible.comfort.seated=true;menu.status(accessible,false);
                    navigate(swvr::VrMenuAction::interface_page);require(menu.snapshot().title=="INTERFEJS"&&menu.snapshot().scale==1.4f,"interface preferences not applied on opening");export_page("vr-interface-menu-20261105.ppm");
                    navigate(swvr::VrMenuAction::controls);navigate(swvr::VrMenuAction::comfort_page);require(menu.snapshot().title=="KOMFORT","comfort page missing");export_page("vr-comfort-menu-20261105.ppm");
                    navigate(swvr::VrMenuAction::help);require(menu.snapshot().help_mode,"help page did not enable");
                    const auto explain=[&](swvr::VrMenuAction action){const auto it=std::find_if(menu.snapshot().buttons.begin(),menu.snapshot().buttons.end(),[&](const auto& b){return b.id==static_cast<int>(action);});require(it!=menu.snapshot().buttons.end(),"help target absent");const auto b=*it;auto& hand=preview.hands[1];hand.active=true;
                        for(unsigned j:{7u,10u}){hand.joints[j].position_valid=hand.joints[j].position_tracked=true;hand.joints[j].pose.position=swvr::compose(menu.snapshot().panel,swvr::Pose{{},{b.x,b.y,j==7?.35f:.25f}}).position;}++preview.id;preview.predicted_display_time_ns+=20'000'000;require(!menu.update(preview),"hover help executed option");preview.hands={};};
                    explain(swvr::VrMenuAction::head_toggle);require(!menu.snapshot().description.empty(),"help description missing");export_page("vr-help-menu-20261105.ppm");
                    overlay.keypad({});swvr::VrMenuSummon launcher;launcher.update(preview);require(launcher.snapshot().visible,"MENU touch tile missing");overlay.menu_button(launcher.snapshot());
                    std::fill(pixels.begin(),pixels.end(),0);for(const auto texture:textures){glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,extent,extent,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());}
                    const State launcher_state(gl);overlay.draw(preview);require(State(gl)==launcher_state,"MENU touch tile changed host GL state");glBindTexture(GL_TEXTURE_2D,textures[0]);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                    require(std::any_of(pixels.begin(),pixels.end(),[](auto value){return value!=0;}),"MENU touch tile was not drawn");
                    for(int y=0;y<extent;++y)for(int x=0;x<extent;++x)for(unsigned channel=0;channel<3;++channel)rgb[static_cast<std::size_t>((y*extent+x)*3)+channel]=pixels[static_cast<std::size_t>(((extent-1-y)*extent+x)*4)+channel];
                    std::ofstream launcher_image(std::filesystem::path(argv[1])/"vr-menu-touch-button-20261104.ppm",std::ios::binary);launcher_image<<"P6\n"<<extent<<' '<<extent<<"\n255\n";launcher_image.write(reinterpret_cast<const char*>(rgb.data()),static_cast<std::streamsize>(rgb.size()));overlay.menu_button({});
                    glDeleteTextures(2,textures.data());glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(old_texture));
                }
                overlay.keypad({});left.reset();right.reset();host.set(host_read,host_draw);overlay.draw(empty);
                require(left.read()==Image::background()&&right.read()==Image::background(),"closed keypad remained visible");
            }
            for(int scenario=0;scenario<5;++scenario) {
                // Text remains visible with both hands lost and preserves GL
                // state; expiration must restore the no-geometry fast path.
                if(scenario==0) {
                    auto no_hands=frame;no_hands.hands={};
                    left.reset();right.reset();overlay.status_text("POKAZ OBIE DLONIE",std::chrono::milliseconds(80));host.set(host_read,host_draw);
                    const State text_state(gl);overlay.draw(no_hands);
                    require(State(gl)==text_state,"headset text changed host GL state");
                    require(left.read()!=Image::background()&&right.read()!=Image::background(),"headset text missing without hands");
                    Sleep(120);left.reset();right.reset();overlay.draw(no_hands);
                    require(left.read()==Image::background()&&right.read()==Image::background(),"headset text did not expire");
                }
                left.reset();right.reset();frame.hands={};joint(0,0,{0,-.2f,-1});joint(0,1,{0,.2f,-1});frame.should_render=true;
                if(scenario==0)frame.hands[0].active=false;
                if(scenario==1)frame.hands[0].joints[1].position_valid=false;
                if(scenario==2)frame.hands[0].joints[1].position_tracked=false;
                if(scenario==3)frame.hands[0].joints[1].pose.position.x=std::numeric_limits<float>::quiet_NaN();
                if(scenario==4)frame.should_render=false;
                host.set(host_read,host_draw);const State unchanged(gl);overlay.draw(frame);require(State(gl)==unchanged,"skipped overlay changed state");
                require(left.read()==Image::background() && right.read()==Image::background(),"inactive/invalid/untracked hand changed images");
            }
            frame.should_render=true;frame.hands={};joint(0,4,{-.2f,-.1f,-1});joint(0,5,{-.2f,.1f,-1});
            host.set(host_read,host_draw);const State invalid(gl);frame.eyes[1].width=127;
            throws([&]{overlay.draw(frame);},"bad borrowed texture dimensions accepted");require(State(gl)==invalid,"failure changed host state");
            require(left.read()==Image::background() && right.read()==Image::background(),"invalid pair partially drawn");
            frame.eyes[1].width=128;host.set(host_read,host_draw);overlay.draw(frame);require(cyan(left.read()).count>5,"thumb tip 5 edge missing");
            // A right index points diagonally forward/right. Its 3 m extension
            // projects beyond the finger bones while remaining inside both eyes.
            frame.hands={};frame.focused=frame.position_tracked=frame.orientation_tracked=true;
            joint(1,5,{-.05f,-.05f,-1});joint(1,7,{0,0,-1});joint(1,8,{.015f,0,-1.015f});
            joint(1,9,{.03f,0,-1.03f});joint(1,10,{.05f,0,-1.05f});
            const auto valid_ray_frame=frame;
            left.reset();right.reset();host.set(host_read,host_draw);overlay.draw(frame);
            const auto bones_left=left.read(),bones_right=right.read();
            require(green_ray(bones_left).count==0 && green_ray(bones_right).count==0,"default overlay unexpectedly drew a ray");
            left.reset();right.reset();host.set(host_read,host_draw);const State ray_state(gl);overlay.draw(frame,true);
            require(State(gl)==ray_state,"ray overlay changed host GL state");
            const auto ray_left=green_ray(left.read()),ray_right=green_ray(right.read());
            require(ray_left.count>=25 && ray_right.count>=25,"enabled pointing ray pixels missing");
            require(ray_left.rightmost>rightmost_geometry(bones_left)+20 && ray_right.rightmost>rightmost_geometry(bones_right)+20,"ray did not extend beyond finger bones");
            // Expected endpoint projection for tip(.05,0,-1.05), a 45-degree
            // direction and a 3 m segment, with the two +/-32 mm eye offsets.
            require(std::abs(ray_left.rightmost-108)<=2 && std::abs(ray_right.rightmost-107)<=2,"ray length/direction or eye projection wrong");
            for(int scenario=0;scenario<10;++scenario) {
                frame=valid_ray_frame;
                if(scenario==0)frame.focused=false;
                if(scenario==1)frame.position_tracked=false;
                if(scenario==2)frame.orientation_tracked=false;
                if(scenario==3)frame.hands[1].joints[5].position_valid=false;
                if(scenario==4)frame.hands[1].joints[5].position_tracked=false;
                if(scenario==5)frame.hands[1].joints[5].pose.position.x=std::numeric_limits<float>::quiet_NaN();
                if(scenario==6)frame.hands[1].joints[7].position_valid=false;
                if(scenario==7)frame.hands[1].joints[10].position_tracked=false;
                if(scenario==8)frame.hands[1].joints[7].pose.position={.045f,0,-1.05f}; // 5 mm, too short.
                if(scenario==9){frame.hands[0]=frame.hands[1];frame.hands[1].active=false;}
                left.reset();right.reset();host.set(host_read,host_draw);overlay.draw(frame,false);
                const auto expected_left=left.read(),expected_right=right.read();
                left.reset();right.reset();host.set(host_read,host_draw);const State no_ray_state(gl);overlay.draw(frame,true);
                require(State(gl)==no_ray_state,"suppressed ray changed host GL state");
                require(left.read()==expected_left && right.read()==expected_right,"invalid/unfocused ray changed existing skeleton policy");
            }
            frame=valid_ray_frame;
            wglMakeCurrent(nullptr,nullptr);throws([&]{overlay.draw(frame);},"absent context accepted");require(wglMakeCurrent(context.dc,context.gl)!=0,"restore context");
        }
        require(glIsTexture(left.texture) && glIsTexture(right.texture),"overlay deleted borrowed textures");
        require(glGetError()==GL_NO_ERROR,"GL error in hand overlay GPU test");
        std::cout<<"PASS hand overlay: core GL, skeletons, stereo parallax, optional 3m right-index ray, validity/focus gates, host state and borrowed lifetime\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
