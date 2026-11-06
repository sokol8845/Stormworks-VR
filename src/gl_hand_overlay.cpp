#include "swvr/gl_hand_overlay.hpp"
#include <chrono>
#include <windows.h>
#include <GL/gl.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace swvr {
namespace {
constexpr GLenum draw_fb=0x8CA9,read_fb=0x8CA8,draw_binding=0x8CA6,read_binding=0x8CAA;
constexpr GLenum color0=0x8CE0,complete=0x8CD5,srgb=0x8DB9,array_buffer=0x8892;
constexpr GLenum vertex_shader=0x8B31,fragment_shader=0x8B30,compile_status=0x8B81,link_status=0x8B82;
constexpr GLenum raster_discard=0x8C89;
// XR_HAND_JOINT_SET_DEFAULT_EXT, verified against vendor/OpenXR/include/openxr/openxr.h.
// Palm(0) is a hub; wrist(1), thumb(2..5), index(6..10), middle(11..15),
// ring(16..20), little(21..25). A segment needs two tracked valid positions.
constexpr std::array<std::array<unsigned,2>,25> bones{{
    {0,1},{0,2},{2,3},{3,4},{4,5},
    {0,6},{6,7},{7,8},{8,9},{9,10},
    {0,11},{11,12},{12,13},{13,14},{14,15},
    {0,16},{16,17},{17,18},{18,19},{19,20},
    {0,21},{21,22},{22,23},{23,24},{24,25}
}};
static_assert(xr_hand_joint_count==26);
constexpr std::size_t index_proximal=7; // XR_HAND_JOINT_INDEX_PROXIMAL_EXT in vendored SDK.
static_assert(xr_thumb_tip==5 && xr_index_tip==10);
struct Vertex {Vec3 position;std::array<float,3> color;};
std::array<unsigned char,7> glyph(char c) {
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/-.?+:%";
    constexpr std::array<std::array<unsigned char,7>,43> rows{{
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14},{1,2,2,4,8,8,16},{0,0,0,31,0,0,0},{0,0,0,0,0,0,4},{14,17,1,2,4,0,4},{0,4,4,31,4,4,0},{0,4,4,0,4,4,0},{25,26,2,4,8,11,19}
    }};
    const auto at=alphabet.find(c);return at==alphabet.npos?std::array<unsigned char,7>{}:rows[at];
}
const std::vector<Vec3>& glyph_vertices(unsigned char character){
    // Unit-size line geometry is immutable across labels, frames and poses.
    static const auto cache=[] {
        std::array<std::vector<Vec3>,128> shapes;
        for(unsigned c=0;c<shapes.size();++c){const auto rows=glyph(static_cast<char>(c));auto& shape=shapes[c];
            for(unsigned row=0;row<7;++row)for(unsigned col=0;col<5;++col)if(rows[row]&(16u>>col))
                for(unsigned stroke=0;stroke<3;++stroke){const float y=-static_cast<float>(row)-static_cast<float>(stroke)*.28f;
                    shape.push_back({static_cast<float>(col),y,0});shape.push_back({static_cast<float>(col)+.85f,y,0});}
        }
        return shapes;
    }();
    return cache[character<cache.size()?character:0];
}
void text_vertices(std::vector<Vertex>& vertices,const Pose& origin,std::string_view text,float x,float y,float z,float cell,std::array<float,3> color){
    if(text.empty())return;
    if(!std::isfinite(origin.position.x)||!std::isfinite(origin.position.y)||!std::isfinite(origin.position.z))throw std::invalid_argument("Invalid text origin");
    // Normalize the label's orientation once instead of calling compose()
    // (and normalizing quaternions) for every stroke endpoint.
    const auto rotation=view_matrix(inverse(Pose{origin.orientation,{}}));
    for(std::size_t i=0;i<text.size();++i)for(const auto& point:glyph_vertices(static_cast<unsigned char>(text[i]))){
        const float px=x+(static_cast<float>(i)*6.f+point.x)*cell,py=y+point.y*cell;
        vertices.push_back({{origin.position.x+rotation[0]*px+rotation[4]*py+rotation[8]*z,
                             origin.position.y+rotation[1]*px+rotation[5]*py+rotation[9]*z,
                             origin.position.z+rotation[2]*px+rotation[6]*py+rotation[10]*z},color});
    }
}
void rectangle(std::vector<Vertex>& vertices,const Pose& origin,float x,float y,float z,float width,float height,std::array<float,3> color){
    const std::array<Vec3,4> corners{Vec3{x-width/2,y-height/2,z},Vec3{x+width/2,y-height/2,z},Vec3{x+width/2,y+height/2,z},Vec3{x-width/2,y+height/2,z}};
    for(unsigned i=0;i<4;++i){vertices.push_back({compose(origin,Pose{{},corners[i]}).position,color});vertices.push_back({compose(origin,Pose{{},corners[(i+1)%4]}).position,color});}
}
bool usable(const XrTrackedJoint& joint) {
    const auto p=joint.pose.position;
    return joint.position_valid && joint.position_tracked &&
        std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
template<class T>T function(const char* name) {
    const auto p=wglGetProcAddress(name);
    if(!p || p==reinterpret_cast<PROC>(1) || p==reinterpret_cast<PROC>(2) ||
       p==reinterpret_cast<PROC>(3) || p==reinterpret_cast<PROC>(-1))
        throw std::runtime_error(std::string("Missing overlay GL function ")+name);
    return reinterpret_cast<T>(p);
}
Matrix4 multiply(const Matrix4& a,const Matrix4& b) {
    Matrix4 result{};
    for(unsigned col=0;col<4;++col) for(unsigned row=0;row<4;++row)
        for(unsigned k=0;k<4;++k) result[col*4+row]+=a[k*4+row]*b[col*4+k];
    return result;
}
}
struct GlHandOverlay::Impl {
    HGLRC context{};DWORD thread{};
    std::array<float,3> flash_color{};std::chrono::steady_clock::time_point flash_until{};
    std::array<float,3> status_color{};std::chrono::steady_clock::time_point status_until{};
    std::string text;std::chrono::steady_clock::time_point text_until{};
    float progress{};std::chrono::steady_clock::time_point progress_until{};
    KeypadSnapshot keypad;
    PanelSnapshot menu_button;
    ComfortVisual comfort;
    std::vector<Vertex> vertices; // Reuse the largest frame's allocation.
    GLuint program{},vao{},vbo{};GLint matrix_location{-1};
    GLint pass_location{-1},effect_location{-1},tangents_location{-1},eye_to_head_location{-1};
    std::array<GLuint,2> fbos{};
    void(APIENTRY* bind_fb)(GLenum,GLuint){};
    void(APIENTRY* gen_fb)(GLsizei,GLuint*){};
    void(APIENTRY* delete_fb)(GLsizei,const GLuint*){};
    void(APIENTRY* attach)(GLenum,GLenum,GLenum,GLuint,GLint){};
    GLenum(APIENTRY* status)(GLenum){};
    void(APIENTRY* gen_vao)(GLsizei,GLuint*){};
    void(APIENTRY* delete_vao)(GLsizei,const GLuint*){};
    void(APIENTRY* bind_vao)(GLuint){};
    void(APIENTRY* gen_buffer)(GLsizei,GLuint*){};
    void(APIENTRY* delete_buffer)(GLsizei,const GLuint*){};
    void(APIENTRY* bind_buffer)(GLenum,GLuint){};
    void(APIENTRY* buffer_data)(GLenum,std::ptrdiff_t,const void*,GLenum){};
    void(APIENTRY* attribute)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*){};
    void(APIENTRY* enable_attribute)(GLuint){};
    GLuint(APIENTRY* create_shader)(GLenum){};
    void(APIENTRY* shader_source)(GLuint,GLsizei,const char* const*,const GLint*){};
    void(APIENTRY* compile_shader)(GLuint){};
    void(APIENTRY* get_shader)(GLuint,GLenum,GLint*){};
    void(APIENTRY* shader_log)(GLuint,GLsizei,GLsizei*,char*){};
    void(APIENTRY* delete_shader)(GLuint){};
    GLuint(APIENTRY* create_program)(){};
    void(APIENTRY* attach_shader)(GLuint,GLuint){};
    void(APIENTRY* link_program)(GLuint){};
    void(APIENTRY* get_program)(GLuint,GLenum,GLint*){};
    void(APIENTRY* program_log)(GLuint,GLsizei,GLsizei*,char*){};
    void(APIENTRY* delete_program)(GLuint){};
    void(APIENTRY* use_program)(GLuint){};
    GLint(APIENTRY* uniform_location)(GLuint,const char*){};
    void(APIENTRY* uniform_matrix)(GLint,GLsizei,GLboolean,const GLfloat*){};
    void(APIENTRY* enable_i)(GLenum,GLuint){};
    void(APIENTRY* disable_i)(GLenum,GLuint){};
    GLboolean(APIENTRY* enabled_i)(GLenum,GLuint){};
    void(APIENTRY* get_boolean_i)(GLenum,GLuint,GLboolean*){};
    void(APIENTRY* mask_i)(GLuint,GLboolean,GLboolean,GLboolean,GLboolean){};
    void(APIENTRY* viewport_i)(GLuint,GLfloat,GLfloat,GLfloat,GLfloat){};
    void(APIENTRY* integer_i)(GLenum,GLuint,GLint*){};
    void(APIENTRY* blend_func_i)(GLuint,GLenum,GLenum,GLenum,GLenum){};
    void(APIENTRY* blend_equation_i)(GLuint,GLenum,GLenum){};
    void(APIENTRY* uniform_int)(GLint,GLint){};
    void(APIENTRY* uniform3)(GLint,GLfloat,GLfloat,GLfloat){};
    void(APIENTRY* uniform4)(GLint,GLfloat,GLfloat,GLfloat,GLfloat){};
    void initialize() {
        const auto current=wglGetCurrentContext();
        if(!current || (context && (current!=context || thread!=GetCurrentThreadId())))
            throw std::logic_error("Hand overlay requires its owner thread and current GL context");
        if(context) return;
        GLint major{},minor{};glGetIntegerv(0x821B,&major);glGetIntegerv(0x821C,&minor);
        if(major<4 || (major==4 && minor<1)) throw std::runtime_error("Hand overlay requires OpenGL 4.1+");
#define LOAD(member,name) member=function<decltype(member)>(name)
        LOAD(bind_fb,"glBindFramebuffer");LOAD(gen_fb,"glGenFramebuffers");LOAD(delete_fb,"glDeleteFramebuffers");
        LOAD(attach,"glFramebufferTexture2D");LOAD(status,"glCheckFramebufferStatus");
        LOAD(gen_vao,"glGenVertexArrays");LOAD(delete_vao,"glDeleteVertexArrays");LOAD(bind_vao,"glBindVertexArray");
        LOAD(gen_buffer,"glGenBuffers");LOAD(delete_buffer,"glDeleteBuffers");LOAD(bind_buffer,"glBindBuffer");LOAD(buffer_data,"glBufferData");
        LOAD(attribute,"glVertexAttribPointer");LOAD(enable_attribute,"glEnableVertexAttribArray");
        LOAD(create_shader,"glCreateShader");LOAD(shader_source,"glShaderSource");LOAD(compile_shader,"glCompileShader");
        LOAD(get_shader,"glGetShaderiv");LOAD(shader_log,"glGetShaderInfoLog");LOAD(delete_shader,"glDeleteShader");
        LOAD(create_program,"glCreateProgram");LOAD(attach_shader,"glAttachShader");LOAD(link_program,"glLinkProgram");
        LOAD(get_program,"glGetProgramiv");LOAD(program_log,"glGetProgramInfoLog");LOAD(delete_program,"glDeleteProgram");
        LOAD(use_program,"glUseProgram");LOAD(uniform_location,"glGetUniformLocation");LOAD(uniform_matrix,"glUniformMatrix4fv");
        LOAD(enable_i,"glEnablei");LOAD(disable_i,"glDisablei");LOAD(enabled_i,"glIsEnabledi");
        LOAD(get_boolean_i,"glGetBooleani_v");LOAD(mask_i,"glColorMaski");LOAD(viewport_i,"glViewportIndexedf");
        LOAD(integer_i,"glGetIntegeri_v");LOAD(blend_func_i,"glBlendFuncSeparatei");LOAD(blend_equation_i,"glBlendEquationSeparatei");
        LOAD(uniform_int,"glUniform1i");LOAD(uniform3,"glUniform3f");LOAD(uniform4,"glUniform4f");
#undef LOAD
        context=current;thread=GetCurrentThreadId();
    }
    GLuint shader(GLenum type,const char* source) {
        const GLuint result=create_shader(type);
        if(!result) throw std::runtime_error("Cannot allocate hand overlay shader");
        shader_source(result,1,&source,nullptr);compile_shader(result);
        GLint ok{};get_shader(result,compile_status,&ok);
        if(!ok) {std::array<char,1024> log{};shader_log(result,static_cast<GLsizei>(log.size()),nullptr,log.data());
            delete_shader(result);throw std::runtime_error(std::string("Hand shader: ")+log.data());}
        return result;
    }
    void resources() {
        if(!program) {
            GLuint vs{},fs{},linked{};
            try {
                vs=shader(vertex_shader,"#version 330 core\nlayout(location=0) in vec3 position;layout(location=1) in vec3 color;uniform mat4 local_to_clip;uniform int pass;out vec3 tint;out vec2 uv;void main(){if(pass==1){vec2 p=gl_VertexID==0?vec2(-1,-1):gl_VertexID==1?vec2(3,-1):vec2(-1,3);gl_Position=vec4(p,0,1);uv=p*.5+.5;}else{gl_Position=local_to_clip*vec4(position,1);uv=vec2(0);}tint=color;}");
                fs=shader(fragment_shader,"#version 330 core\nin vec3 tint;in vec2 uv;uniform int pass;uniform vec3 effect;uniform vec4 tangents;uniform mat4 eye_to_head;layout(location=0) out vec4 output_color;void main(){if(pass==1){vec3 ray=(eye_to_head*vec4(mix(tangents.x,tangents.y,uv.x),mix(tangents.z,tangents.w,uv.y),-1,0)).xyz;float angle=atan(length(ray.xy),-ray.z);float alpha=max(effect.x,effect.y*smoothstep(effect.z,effect.z+.174533,angle));output_color=vec4(0,0,0,alpha);}else output_color=vec4(tint,1);}");
                linked=create_program();if(!linked) throw std::runtime_error("Cannot allocate hand overlay program");
                attach_shader(linked,vs);attach_shader(linked,fs);link_program(linked);
                GLint ok{};get_program(linked,link_status,&ok);
                if(!ok) {std::array<char,1024> log{};program_log(linked,static_cast<GLsizei>(log.size()),nullptr,log.data());
                    throw std::runtime_error(std::string("Hand program: ")+log.data());}
                matrix_location=uniform_location(linked,"local_to_clip");
                pass_location=uniform_location(linked,"pass");effect_location=uniform_location(linked,"effect");tangents_location=uniform_location(linked,"tangents");
                eye_to_head_location=uniform_location(linked,"eye_to_head");
                if(matrix_location<0) throw std::runtime_error("Hand overlay matrix uniform missing");
                program=linked;linked=0;delete_shader(vs);delete_shader(fs);
            } catch(...) {if(vs)delete_shader(vs);if(fs)delete_shader(fs);if(linked)delete_program(linked);throw;}
        }
        if(!vao) gen_vao(1,&vao);
        if(!vbo) gen_buffer(1,&vbo);
        if(!fbos[0]) gen_fb(2,fbos.data());
        if(!vao || !vbo || !fbos[0] || !fbos[1]) throw std::runtime_error("Hand overlay allocation failed");
    }
    struct State {
        Impl& api;
        GLint read{},draw{},texture{},program{},vao{},buffer{};
        std::array<GLfloat,4> viewport{};
        std::array<GLboolean,4> color_mask{};
        GLboolean blend{},scissor{},depth{},depth_mask{},color_srgb{},discard{},logic{},smooth{},stencil{},depth_clamp{};
        std::vector<GLboolean> clip_planes;
        GLfloat line_width{};
        std::array<GLint,6> blending{};GLint polygon[2]{};GLboolean cull{},alpha_coverage{},coverage{},sample_mask{};
        explicit State(Impl& api):api(api) {
            glGetIntegerv(read_binding,&read);glGetIntegerv(draw_binding,&draw);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(0x8B8D,&program);
            glGetIntegerv(0x85B5,&vao);glGetIntegerv(0x8894,&buffer);glGetFloatv(GL_VIEWPORT,viewport.data());
            api.get_boolean_i(GL_COLOR_WRITEMASK,0,color_mask.data());blend=api.enabled_i(GL_BLEND,0);
            scissor=api.enabled_i(GL_SCISSOR_TEST,0);depth=glIsEnabled(GL_DEPTH_TEST);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);
            color_srgb=glIsEnabled(srgb);discard=glIsEnabled(raster_discard);logic=glIsEnabled(GL_COLOR_LOGIC_OP);
            smooth=glIsEnabled(GL_LINE_SMOOTH);glGetFloatv(GL_LINE_WIDTH,&line_width);
            stencil=glIsEnabled(GL_STENCIL_TEST);depth_clamp=glIsEnabled(0x864F);
            GLint clips{};glGetIntegerv(0x0D32,&clips); // MAX_CLIP_DISTANCES
            clip_planes.resize(static_cast<std::size_t>(clips));
            for(GLint i=0;i<clips;++i)clip_planes[i]=glIsEnabled(0x3000+i);
            constexpr GLenum blend_names[]{0x80C9,0x80C8,0x80CB,0x80CA,0x8009,0x883D};for(unsigned i=0;i<blending.size();++i)api.integer_i(blend_names[i],0,&blending[i]);
            glGetIntegerv(GL_POLYGON_MODE,polygon);cull=glIsEnabled(GL_CULL_FACE);alpha_coverage=glIsEnabled(0x809E);coverage=glIsEnabled(0x80A0);sample_mask=glIsEnabled(0x8E51);
        }
        static void restore(GLenum cap,GLboolean value) {if(value)glEnable(cap);else glDisable(cap);}
        ~State() {
            for(const auto fbo:api.fbos) if(fbo) {api.bind_fb(draw_fb,fbo);api.attach(draw_fb,color0,GL_TEXTURE_2D,0,0);}
            api.bind_fb(read_fb,read);api.bind_fb(draw_fb,draw);glBindTexture(GL_TEXTURE_2D,texture);
            api.viewport_i(0,viewport[0],viewport[1],viewport[2],viewport[3]);
            api.use_program(program);api.bind_vao(vao);api.bind_buffer(array_buffer,buffer);
            api.mask_i(0,color_mask[0],color_mask[1],color_mask[2],color_mask[3]);
            if(blend)api.enable_i(GL_BLEND,0);else api.disable_i(GL_BLEND,0);
            if(scissor)api.enable_i(GL_SCISSOR_TEST,0);else api.disable_i(GL_SCISSOR_TEST,0);
            restore(GL_DEPTH_TEST,depth);glDepthMask(depth_mask);restore(srgb,color_srgb);
            restore(raster_discard,discard);restore(GL_COLOR_LOGIC_OP,logic);restore(GL_LINE_SMOOTH,smooth);glLineWidth(line_width);
            restore(GL_STENCIL_TEST,stencil);restore(0x864F,depth_clamp);
            for(std::size_t i=0;i<clip_planes.size();++i)restore(0x3000+static_cast<GLenum>(i),clip_planes[i]);
            api.blend_func_i(0,blending[0],blending[1],blending[2],blending[3]);api.blend_equation_i(0,blending[4],blending[5]);
            glPolygonMode(GL_FRONT_AND_BACK,polygon[0]);restore(GL_CULL_FACE,cull);restore(0x809E,alpha_coverage);restore(0x80A0,coverage);restore(0x8E51,sample_mask);
        }
    };
    ~Impl() {
        if(context && wglGetCurrentContext()==context && thread==GetCurrentThreadId()) {
            if(program)delete_program(program);if(vao)delete_vao(1,&vao);if(vbo)delete_buffer(1,&vbo);delete_fb(2,fbos.data());
        }
    }
};
GlHandOverlay::GlHandOverlay():impl_(std::make_unique<Impl>()) {}
GlHandOverlay::~GlHandOverlay()=default;
void GlHandOverlay::flash(std::array<float,3> rgb,std::chrono::milliseconds duration) {impl_->flash_color=rgb;impl_->flash_until=std::chrono::steady_clock::now()+duration;}
void GlHandOverlay::status_frame(std::array<float,3> rgb,std::chrono::milliseconds duration) {impl_->status_color=rgb;impl_->status_until=std::chrono::steady_clock::now()+duration;}
void GlHandOverlay::status_text(std::string_view text,std::chrono::milliseconds duration){impl_->text=text.substr(0,40);impl_->text_until=std::chrono::steady_clock::now()+duration;}
void GlHandOverlay::progress(float value,std::chrono::milliseconds duration){if(!std::isfinite(value))throw std::invalid_argument("Invalid calibration progress");impl_->progress=std::clamp(value,0.f,1.f);impl_->progress_until=std::chrono::steady_clock::now()+duration;}
void GlHandOverlay::keypad(const KeypadSnapshot& state){impl_->keypad=state;}
void GlHandOverlay::menu_button(const PanelSnapshot& state){impl_->menu_button=state;}
void GlHandOverlay::comfort(ComfortVisual visual){
    if(!std::isfinite(visual.fade)||!std::isfinite(visual.vignette)||!std::isfinite(visual.radius_degrees))throw std::invalid_argument("Invalid comfort visual");
    visual.fade=std::clamp(visual.fade,0.f,1.f);visual.vignette=std::clamp(visual.vignette,0.f,1.f);visual.radius_degrees=std::clamp(visual.radius_degrees,25.f,60.f);impl_->comfort=visual;
}
void GlHandOverlay::draw(const XrRuntimeFrame& frame,bool pointing_ray,unsigned pointing_hand) {
    if(!frame.id || !frame.focused || !frame.should_render || frame.terminal || !frame.position_valid || !frame.orientation_valid) return;
    auto& vertices=impl_->vertices;vertices.clear();vertices.reserve(bones.size()*4+2);
    for(std::size_t h=0;h<frame.hands.size();++h) {
        const auto& hand=frame.hands[h];if(!hand.active)continue;
        const bool flashing=std::chrono::steady_clock::now()<impl_->flash_until;
        const std::array<float,3> color=flashing?impl_->flash_color:h==0?std::array<float,3>{.1f,.95f,1.f}:std::array<float,3>{1.f,.65f,.15f};
        for(const auto& bone:bones) {
            const auto& a=hand.joints[bone[0]];const auto& b=hand.joints[bone[1]];
            if(usable(a) && usable(b)) {vertices.push_back({a.pose.position,color});vertices.push_back({b.pose.position,color});}
        }
    }
    if(pointing_ray && !impl_->keypad.visible && impl_->menu_button.hover<0 && pointing_hand<2 && frame.running && frame.focused && frame.position_tracked && frame.orientation_tracked) {
        const auto& hand=frame.hands[pointing_hand];const auto& proximal=hand.joints[index_proximal];
        const auto& tip=hand.joints[xr_index_tip];const auto& thumb=hand.joints[xr_thumb_tip];
        if(hand.active && usable(thumb) && usable(proximal) && usable(tip)) {
            const auto a=proximal.pose.position,b=tip.pose.position;
            const std::array<double,3> direction{static_cast<double>(b.x)-a.x,
                static_cast<double>(b.y)-a.y,static_cast<double>(b.z)-a.z};
            const double length=std::hypot(direction[0],direction[1],direction[2]);
            // Match HandInteractionBridge's default 1 cm position-derived ray.
            if(std::isfinite(length) && length>=0.01f) {
                const Vec3 end{static_cast<float>(b.x+3.0*direction[0]/length),
                    static_cast<float>(b.y+3.0*direction[1]/length),
                    static_cast<float>(b.z+3.0*direction[2]/length)};
                constexpr std::array<float,3> color{.15f,1.f,.35f};
                vertices.push_back({b,color});vertices.push_back({end,color});
            }
        }
    }
    if(std::chrono::steady_clock::now()<impl_->status_until) {
        // Three nested squares, 1 m ahead of the eye centre, locked to the head.
        const auto centre=stereo_center({frame.eyes[0].pose,frame.eyes[1].pose});
        for(const float half:{.10f,.095f,.09f}) {
            const std::array<Vec3,4> corners{Vec3{-half,-half,-1},Vec3{half,-half,-1},Vec3{half,half,-1},Vec3{-half,half,-1}};
            for(std::size_t c=0;c<4;++c) {
                vertices.push_back({compose(centre,Pose{{},corners[c]}).position,impl_->status_color});
                vertices.push_back({compose(centre,Pose{{},corners[(c+1)%4]}).position,impl_->status_color});
            }
        }
    }
    if(!impl_->text.empty()&&std::chrono::steady_clock::now()<impl_->text_until) {
        const auto centre=stereo_center({frame.eyes[0].pose,frame.eyes[1].pose});
        constexpr float cell=.004f;constexpr std::array<float,3> color{.95f,.95f,.95f};
        const float left=-static_cast<float>(impl_->text.size())*cell*3.f;
        text_vertices(vertices,centre,impl_->text,left,-.15f,-1,cell,color);
    }
    if(std::chrono::steady_clock::now()<impl_->progress_until){
        const auto centre=stereo_center({frame.eyes[0].pose,frame.eyes[1].pose});
        rectangle(vertices,centre,0,-.20f,-1,.5f,.016f,{.6f,.6f,.6f});
        for(float y:{-.204f,-.20f,-.196f}){
            vertices.push_back({compose(centre,Pose{{},{-.25f,y,-1}}).position,{1.f,.85f,.1f}});
            vertices.push_back({compose(centre,Pose{{},{-.25f+.5f*impl_->progress,y,-1}}).position,{1.f,.85f,.1f}});
        }
    }
    for(const auto* panel:{&impl_->keypad,&impl_->menu_button})if(panel->visible&&frame.space_revision==panel->space_revision){
        const auto& k=*panel;const float scale=k.scale;rectangle(vertices,k.panel,0,0,0,(k.compact?.18f:.46f)*scale,(k.compact?.09f:.65f)*scale,{.25f,.65f,1.f});
        if(!k.compact){const auto title=k.help_mode?"POMOC":k.title;const float title_cell=std::min(.0033f,.33f/(6.f*static_cast<float>(std::max(std::size_t{1},title.size()))))*scale;text_vertices(vertices,k.panel,title,-.20f*scale,.30f*scale,0,title_cell,k.help_mode?std::array<float,3>{1.f,.85f,.2f}:std::array<float,3>{.8f,.8f,.8f});
            const float value_cell=std::min(.005f,.38f/(6.f*static_cast<float>(std::max(std::size_t{1},k.value.size()))))*scale;
            if(k.help_mode){std::string_view remaining=k.description;const auto cell=.00215f*scale;for(unsigned row=0;row<3&&!remaining.empty();++row){std::size_t n=std::min(std::size_t{32},remaining.size());if(n<remaining.size()){const auto at=remaining.substr(0,n+1).find_last_of(' ');if(at!=remaining.npos&&at>0)n=at;}
                    text_vertices(vertices,k.panel,remaining.substr(0,n),-.205f*scale,(.252f-.022f*static_cast<float>(row))*scale,0,cell,{1.f,.9f,.5f});remaining.remove_prefix(n);while(!remaining.empty()&&remaining.front()==' ')remaining.remove_prefix(1);}}
            else text_vertices(vertices,k.panel,k.value.empty()?"-":k.value,-.19f*scale,.235f*scale,0,value_cell,{1.f,1.f,1.f});}
        const auto& keys=k.buttons;
        for(std::size_t i=0;i<keys.size();++i){const auto& b=keys[i];const auto color=static_cast<int>(i)==k.pressed?std::array<float,3>{1.f,.5f,.1f}:static_cast<int>(i)==k.hover?std::array<float,3>{.15f,1.f,.35f}:std::array<float,3>{.35f,.65f,1.f};
            const auto shade=b.enabled?color:std::array<float,3>{.3f,.3f,.3f};rectangle(vertices,k.panel,b.x,b.y,0,b.width,b.height,shade);
            if(static_cast<int>(i)==k.hover&&k.dwell_progress>0){const auto width=(b.width-.01f)*k.dwell_progress;rectangle(vertices,k.panel,b.x-(b.width-.01f-width)/2,b.y-b.height/2+.005f,0,width,.003f,{.15f,1.f,.35f});}
            const float cell=std::min(.004f*scale,(b.width-.01f)/(6.f*static_cast<float>(std::max(std::size_t{1},b.label.size()))));
            text_vertices(vertices,k.panel,b.label,b.x-static_cast<float>(b.label.size())*cell*3.f,b.y+cell*3.f,0,cell,shade);
        }
        for(unsigned h=0;h<2;++h)if(k.rays[h]){const auto color=k.ray_targets[h]<0?std::array<float,3>{.55f,.55f,.55f}:k.help_mode?std::array<float,3>{1.f,.85f,.2f}:k.release_required?std::array<float,3>{1.f,.5f,.1f}:std::array<float,3>{.15f,1.f,.35f};
            vertices.push_back({k.ray_start[h],color});vertices.push_back({k.ray_end[h],color});if(k.ray_targets[h]>=0)rectangle(vertices,Pose{k.panel.orientation,k.ray_end[h]},0,0,.001f,.009f,.009f,color);}
        for(unsigned h=0;h<2;++h)if(k.touches[h])rectangle(vertices,k.panel,k.touch_local[h].x,k.touch_local[h].y,.001f,.012f,.012f,{.15f,1.f,.35f});
        if(!k.compact){const auto footer=k.help_mode?"POMOC - ? WLACZA KLIKANIE":k.release_required?(k.direct_touch?"COFNIJ PALEC PRZED KOLEJNYM":"ODSUN PROMIEN PRZED KOLEJNYM"):k.hover<0?"DOTKNIJ / WSKAZ I PRZYTRZYMAJ":k.direct_touch?"DOTYK":"WSKAZUJ DO KONCA PASKA";
            text_vertices(vertices,k.panel,footer,-.21f*scale,-.308f*scale,0,.002f*scale,{.8f,.8f,.8f});
            if(!k.help_mode&&!k.notice.empty())text_vertices(vertices,k.panel,k.notice,-.21f*scale,-.35f*scale,0,.00175f*scale,{.8f,1.f,.8f});}
    }
    const bool effect=impl_->comfort.fade>0||impl_->comfort.vignette>0;
    if(vertices.empty()&&!effect)return;
    const auto line_count=vertices.size();if(vertices.size()<3)vertices.resize(3);
    std::array<Matrix4,2> matrices;
    for(std::size_t i=0;i<2;++i) {
        const auto& eye=frame.eyes[i];
        if(!eye.texture || eye.width<=0 || eye.height<=0 || eye.width>16384 || eye.height>16384)
            throw std::invalid_argument("Invalid hand overlay eye image");
        matrices[i]=multiply(projection(eye.fov,.01f,20.f),view_matrix(eye.pose));
    }
    if(frame.eyes[0].texture==frame.eyes[1].texture)throw std::invalid_argument("Hand overlay eye images alias");
    auto& s=*impl_;s.initialize();Impl::State state(s);s.resources();
    // Validate both images before drawing either. Host selectors are object
    // state: only our own FBO's draw selector is changed.
    for(std::size_t i=0;i<2;++i) {
        const auto& eye=frame.eyes[i];if(!glIsTexture(eye.texture))throw std::invalid_argument("Hand eye is not a texture");
        glBindTexture(GL_TEXTURE_2D,eye.texture);GLint bound{},width{},height{},format{};
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&bound);
        if(static_cast<GLuint>(bound)!=eye.texture)throw std::invalid_argument("Hand eye is not a 2D texture");
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
        if(width!=eye.width || height!=eye.height || (format!=GL_RGBA8 && format!=0x8C43))
            throw std::invalid_argument("Hand eye texture storage mismatch");
        s.bind_fb(draw_fb,s.fbos[i]);s.attach(draw_fb,color0,GL_TEXTURE_2D,eye.texture,0);glDrawBuffer(color0);
        if(s.status(draw_fb)!=complete)throw std::runtime_error("Hand overlay framebuffer incomplete");
    }
    s.bind_vao(s.vao);s.bind_buffer(array_buffer,s.vbo);
    s.buffer_data(array_buffer,static_cast<std::ptrdiff_t>(vertices.size()*sizeof(Vertex)),vertices.data(),0x88E0); // STREAM_DRAW
    s.attribute(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);s.enable_attribute(0);
    s.attribute(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<const void*>(offsetof(Vertex,color)));s.enable_attribute(1);
    s.use_program(s.program);s.disable_i(GL_BLEND,0);s.disable_i(GL_SCISSOR_TEST,0);
    s.mask_i(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glDisable(srgb);glDisable(raster_discard);glDisable(GL_COLOR_LOGIC_OP);glDisable(GL_LINE_SMOOTH);glLineWidth(1.f);
    glDisable(GL_STENCIL_TEST);glDisable(0x864F);
    glDisable(GL_CULL_FACE);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glDisable(0x809E);glDisable(0x80A0);glDisable(0x8E51);
    for(std::size_t i=0;i<state.clip_planes.size();++i)glDisable(0x3000+static_cast<GLenum>(i));
    for(std::size_t i=0;i<2;++i) {
        s.bind_fb(draw_fb,s.fbos[i]);s.viewport_i(0,0,0,static_cast<float>(frame.eyes[i].width),static_cast<float>(frame.eyes[i].height));
        if(effect){s.uniform_int(s.pass_location,1);const auto& fov=frame.eyes[i].fov;s.uniform4(s.tangents_location,std::tan(fov.left),std::tan(fov.right),std::tan(fov.down),std::tan(fov.up));
            const auto centre=stereo_center({frame.eyes[0].pose,frame.eyes[1].pose});const auto relative=compose(inverse(Pose{centre.orientation,{}}),Pose{frame.eyes[i].pose.orientation,{}});const auto eye_to_head=view_matrix(inverse(relative));s.uniform_matrix(s.eye_to_head_location,1,GL_FALSE,eye_to_head.data());
            s.uniform3(s.effect_location,s.comfort.fade,s.comfort.vignette,s.comfort.radius_degrees*.01745329252f);
            s.blend_func_i(0,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);s.blend_equation_i(0,0x8006,0x8006);s.enable_i(GL_BLEND,0);glDrawArrays(GL_TRIANGLES,0,3);s.disable_i(GL_BLEND,0);}
        s.uniform_int(s.pass_location,0);s.uniform_matrix(s.matrix_location,1,GL_FALSE,matrices[i].data());glDrawArrays(GL_LINES,0,static_cast<GLsizei>(line_count));
    }
}
}
