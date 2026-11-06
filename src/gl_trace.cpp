// Observational by default; SWVR_STEREO_PREVIEW enables a menu-only experiment.
// Start outside DllMain, before GL function resolution.
// Keep this module loaded for the process lifetime after successful initialization.
#include <windows.h>
#include <bcrypt.h>
#include <GL/gl.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <mutex>
#include <string>
#include <fstream>
#include <array>
#include <map>
#include <tuple>
#include "swvr/game_profile.hpp"
#include "swvr/camera_bridge.hpp"
#include "swvr/gl_eye_targets.hpp"
#include "swvr/gl_xr_presenter.hpp"
#include "swvr/gl_hand_overlay.hpp"
#include "swvr/native_camera.hpp"
#include "swvr/native_eye_resources.hpp"
#include "swvr/stereo_culling.hpp"
#include "swvr/interaction_probe.hpp"
#include "swvr/head_anchor.hpp"
#include "swvr/head_mask_shader.hpp"
#include "swvr/head_mask_gl.hpp"
#include "swvr/head_mask_sources.hpp"
#include "swvr/hand_interaction.hpp"
#include "swvr/native_hand_ray.hpp"
#include "swvr/native_input_probe.hpp"
#include "swvr/native_hand_buttons.hpp"
#include "swvr/native_button_retirement.hpp"
#include "swvr/xr_runtime.hpp"
#include "swvr/xr_readiness.hpp"
#include "swvr/avatar_pose_gl.hpp"
#include "swvr/driver_wheel_gl.hpp"
#include "swvr/driver_wheel_shader.hpp"
#include "swvr/avatar_calibration.hpp"
#include "swvr/avatar_profile.hpp"
#include "swvr/gesture_control.hpp"
#include "swvr/hand_recording.hpp"
#include "swvr/numeric_keypad.hpp"
#include "swvr/vr_settings_menu.hpp"
#include "swvr/native_avatar_identity.hpp"
#include "swvr/trace_report.hpp"
#include "swvr/diagnostic_image.hpp"
#include "swvr/body_yaw.hpp"
#include "swvr/native_locomotion.hpp"
#include "swvr/hand_turn.hpp"
#include "swvr/hand_steering.hpp"
#include "swvr/native_vehicle_keypad.hpp"
#include "swvr/control_recording.hpp"
#include "swvr/performance.hpp"
#include "swvr/gl_gpu_timer.hpp"
#include "swvr/ui_click_sound.hpp"
#include "swvr/comfort.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <chrono>
#include <cmath>
#include <vector>
#include <iterator>

namespace {
std::mutex log_mutex,resolve_mutex;
FILE* output{};
std::atomic<unsigned long long> frame{},events{};
std::atomic<bool> ready{};
std::atomic<unsigned long long> capture_epoch{};
HANDLE capture_request{};
HANDLE preview_start_request{};
HANDLE preview_stop_request{}; // User ends the measurement ("Local\StormworksVR.Stop.<PID>").
HANDLE avatar_calibrate_request{};
HANDLE avatar_profile_clear_request{},gesture_reload_request{},joystick_mode_request{},arm_mode_request{},keypad_request{};
bool preview_start_requested=true;
using UniformKey=std::tuple<std::uintptr_t,GLuint,GLint>;
std::map<UniformKey,std::string> known_uniforms; // protected by log_mutex
bool uniform_cache_full{};
unsigned long long written_events{}; // protected by log_mutex
constexpr unsigned long long limit=250000;
std::string image_sha256() noexcept {
    struct Crypto {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        ~Crypto() {if(hash) BCryptDestroyHash(hash);if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);}
    } crypto;
    try {
        wchar_t path[32768]{};
        const auto length=GetModuleFileNameW(nullptr,path,32768);
        if(!length || length>=32768) return "unavailable";
        std::ifstream file(path,std::ios::binary);
        if(!file || BCryptOpenAlgorithmProvider(&crypto.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
           BCryptCreateHash(crypto.algorithm,&crypto.hash,nullptr,0,nullptr,0,0)<0) return "unavailable";
        std::array<unsigned char,65536> chunk{};
        while(file) {
            file.read(reinterpret_cast<char*>(chunk.data()),chunk.size());
            if(file.gcount() && BCryptHashData(crypto.hash,chunk.data(),static_cast<ULONG>(file.gcount()),0)<0)
                return "unavailable";
        }
        if(!file.eof()) return "unavailable";
        std::array<unsigned char,32> digest{};
        if(BCryptFinishHash(crypto.hash,digest.data(),static_cast<ULONG>(digest.size()),0)<0) return "unavailable";
        constexpr char hex[]="0123456789abcdef";
        std::string result;result.reserve(64);
        for(auto byte:digest) {result+=hex[byte>>4];result+=hex[byte&15];}
        return result;
    } catch(...) {return "unavailable";}
}
decltype(&wglGetProcAddress) real_get{};
decltype(&SwapBuffers) real_swap{};
decltype(&glDrawElements) real_elements{};
decltype(&glDrawArrays) real_arrays{};
decltype(&glViewport) real_viewport{};
decltype(&wglDeleteContext) real_delete_context{};
using UseProgram=void(APIENTRY*)(GLuint);
using BindFramebuffer=void(APIENTRY*)(GLenum,GLuint);
using UniformLocation=GLint(APIENTRY*)(GLuint,const char*);
using UniformMatrix=void(APIENTRY*)(GLint,GLsizei,GLboolean,const GLfloat*);
using ShaderSource=void(APIENTRY*)(GLuint,GLsizei,const char* const*,const GLint*);
using ShaderOperation=void(APIENTRY*)(GLuint);
using UniformInt=void(APIENTRY*)(GLint,GLint);
using UniformFloat=void(APIENTRY*)(GLint,GLfloat);
UseProgram real_use{};
BindFramebuffer real_bind{};
UniformLocation real_location{};
UniformMatrix real_matrix{};
ShaderSource real_shader_source{};
ShaderOperation real_compile_shader{},real_link_program{},real_delete_shader{};
UniformInt real_uniform_int{};
UniformFloat real_uniform_float{};
swvr::GlHeadMask head_mask;
bool head_mask_enabled{},head_mask_fixture{},head_mask_requested{};
thread_local bool fixture_eye_draw{};
swvr::HeadMaskShaderSources shader_sources;
swvr::HandInteractionBridge hand_interactions{swvr::HandInteractionOptions{.both_hands=true}};
bool hand_ray_active{};
bool input_probe_active{};
bool avatar_requested{};
std::atomic<bool> avatar_enabled{};
swvr::GlAvatarPose avatar_pose;
swvr::GlDriverWheel driver_wheel;
swvr::HeadMaskShaderSources wheel_sources{0x8B31,swvr::patch_driver_wheel_vertex,"driver_wheel_shader"};
std::atomic<bool> wheel_enabled{};
bool wheel_requested{};
void xr_note(const char*,const char*) noexcept;
void wheel_failed(const char* error) noexcept {
    if(wheel_enabled.exchange(false)){driver_wheel.end_eye();xr_note("driver_wheel_error",error);}
}
std::unique_ptr<swvr::NativeAvatarIdentity> avatar_identity;
bool hand_buttons_active{},hand_cleanup_pending{};
ULONGLONG hand_cleanup_started{};
void apply_head_mask(bool active) noexcept;
void xr_note(const char*,const char*) noexcept;
void avatar_failed(const char* error) noexcept {
    if(avatar_enabled.exchange(false)) {avatar_pose.end_eye();xr_note("avatar_pose_error",error);}
}
void avatar_selection(void*,const swvr::NativeHandSelection& selection) noexcept {
    if(hand_buttons_active) {
        const auto callback=swvr::native_hand_buttons_callbacks();
        if(callback.selection_complete)callback.selection_complete(callback.context,selection);
    }
    if(avatar_identity)avatar_identity->observe(selection.completed?selection.controller_live:nullptr);
}
using CameraUpdate=void(*)(void*,float);
CameraUpdate real_camera_update{};
using Renderer=void(*)(void*,void*,void*,void*,void*,void*,void*,void*);
Renderer real_renderer{};
bool stereo_preview{};
bool openxr_preview{};
bool gameplay_preview{},gameplay_profile_failed{},gameplay_armed{};
// Seated neck-pivot head limit (Gameplay only). SWVR_HEAD_LIMIT=0 disables it.
bool head_limit_enabled=true;
const swvr::HeadLimit head_limit{};
// Opt-in (SWVR_BODY_YAW=1): the character's look yaw follows the headset while
// walking in first person. Seat, third-person and free-camera modes are excluded.
bool body_yaw_enabled{};
swvr::HandLocomotion hand_locomotion;
swvr::HandSteering hand_steering;
bool locomotion_requested{},locomotion_cleanup_pending{};
bool arm_swing_requested{},locomotion_mode_valid=true;
bool record_hands_requested{},keypad_requested{},gestures_suspended{},gesture_change_failed{};
bool persist_gesture_change{};
std::filesystem::path gesture_config_path,avatar_profile_path,profile_staging_path;
swvr::GestureSettings active_gesture_settings;
swvr::GestureChangeQueue gesture_changes;
std::string gesture_notice_text;
ULONGLONG locomotion_cleanup_started{};
bool walking_first_person(const void* owner) noexcept {
    const auto* bytes=static_cast<const unsigned char*>(owner);
    // 1A0 third person, 1A1 seat look limits, 1A3 saved-look mode, 1A4..1A6 free cameras.
    for(std::size_t offset:{0x1a0u,0x1a1u,0x1a3u,0x1a4u,0x1a5u,0x1a6u})if(bytes[offset])return false;
    return true;
}
ULONGLONG preview_duration_ms=30000;
using CameraOwnerUpdate=void(*)(void*,void*,float,std::uint8_t);
using Frustum=void(*)(void*,const void*,float);
using Visibility=void(*)(void*,void*,void*,void*);
CameraOwnerUpdate real_camera_owner_update{};
Frustum real_frustum{};
Visibility native_visibility{};
thread_local bool main_visibility_scope{};
bool collect_events=true;
bool independent_native_eyes{};
bool resource_profile_failed{};
swvr::NativeResourceApi resource_api;
std::mutex resource_mutex;
std::map<void*,void*> package_managers;
std::filesystem::path xr_loader;
thread_local const swvr::Fov* active_eye_fov{};
thread_local bool eye_projection_failed{};
thread_local unsigned eye_projection_updates{};
std::filesystem::path diagnostic_base;
std::uintptr_t executable_base{};
void package_initialize(void* package,void* manager,std::uint32_t width,std::uint32_t height,
    std::uint32_t p5,std::uint32_t p6,std::uint32_t p7,std::uint32_t p8,
    std::uint8_t p9,std::uint8_t p10,std::uint8_t p11,std::uint32_t p12,std::uint32_t p13,
    std::uint8_t p14,std::uint8_t p15,std::uint32_t p16,std::uint32_t p17,std::uint8_t p18) {
    resource_api.initialize(package,manager,width,height,p5,p6,p7,p8,p9,p10,p11,p12,p13,p14,p15,p16,p17,p18);
    try {
        std::lock_guard lock(resource_mutex);
        if(package_managers.contains(package) || package_managers.size()<256) package_managers[package]=manager;
    } catch(...) {}
}
// State is queried per event, so switching contexts does not corrupt metadata.
void record(const char* kind,long long a,long long b,long long c,void* caller,
            const char* name=nullptr,const float* matrix=nullptr) noexcept {
    if(!ready || !collect_events || events.load()>=limit) return;
    const auto epoch=capture_epoch.load();
    try {
        const auto context=wglGetCurrentContext();
        GLint program{},fbo{};
        // Modern calls only occur in a valid context. Legacy-only contexts are not supported by this tracer.
        if(context) {glGetIntegerv(0x8B8D,&program);glGetIntegerv(0x8CA6,&fbo);}
        std::lock_guard lock(log_mutex);
        if(!output || epoch!=capture_epoch.load() || events.load()>=limit) return;
        ++events;
        fprintf(output,"%llu\t%lu\t%p\t%s\t%d\t%d\t%lld\t%lld\t%lld\t%p",
            frame.load(),GetCurrentThreadId(),context,kind,program,fbo,a,b,c,caller);
        if(name) {
            fputc('\t',output);
            for(const char* p=name;*p;++p) fputc((*p=='\t'||*p=='\n'||*p=='\r')?' ':*p,output);
        }
        if(matrix) for(int i=0;i<16;++i) fprintf(output,"\t%.9g",matrix[i]);
        fputc('\n',output);
        ++written_events;
        if(written_events==limit) fprintf(output,"#state\tlimit_reached\n");
        if(written_events==limit || !strcmp(kind,"swap")) fflush(output);
    } catch(...) { /* Never propagate diagnostics errors into the host. */ }
}
void APIENTRY use(GLuint program) {real_use(program);apply_head_mask(false);record("program",program,0,0,_ReturnAddress());}
void APIENTRY bind(GLenum target,GLuint fbo) {real_bind(target,fbo);record("framebuffer",target,fbo,0,_ReturnAddress());}
GLint APIENTRY location(GLuint program,const char* name) {
    const auto result=real_location(program,name);
    if(avatar_enabled)try {avatar_pose.uniform_location(program,result,name);}catch(const std::exception& error) {avatar_failed(error.what());}
    if(result>=0 && name) {
        try {
            std::lock_guard lock(log_mutex);
            const UniformKey key{reinterpret_cast<std::uintptr_t>(wglGetCurrentContext()),program,result};
            if(known_uniforms.contains(key) || known_uniforms.size()<16384) {
                auto clean=std::string(name);
                for(char& c:clean) if(c=='\t'||c=='\n'||c=='\r') c=' ';
                known_uniforms[key]=std::move(clean);
            } else uniform_cache_full=true;
        } catch(...) {}
    }
    record("uniform",program,result,0,_ReturnAddress(),name);return result;
}
void APIENTRY matrix(GLint loc,GLsizei count,GLboolean transpose,const GLfloat* values) {
    real_matrix(loc,count,transpose,values);
    if(avatar_enabled)try {avatar_pose.matrix_uploaded(loc,count,transpose!=GL_FALSE,values);}catch(const std::exception& error) {avatar_failed(error.what());}
    if(wheel_enabled)try {driver_wheel.matrix_uploaded(loc,count,transpose!=GL_FALSE,values);}catch(const std::exception& error) {wheel_failed(error.what());}
    record("matrix4",loc,count,transpose,_ReturnAddress(),nullptr,count>0?values:nullptr);
}
void camera_update(void* camera,float aspect) {
    real_camera_update(camera,aspect);
    // The renderer rebuilds three floating-origin cameras. Override only
    // those measured call sites, after their original update refreshed caches.
    const auto camera_caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-executable_base;
    if(active_eye_fov && (camera_caller==0x6d4f80 || camera_caller==0x6d5027 || camera_caller==0x6d50ca)) {
        try {
            const auto lens_index=camera_caller==0x6d4f80?0:camera_caller==0x6d5027?1:2;
            if(lens_index==0 && (avatar_enabled || wheel_enabled)) {
                swvr::Matrix4d transform{};
                std::memcpy(transform.data(),static_cast<unsigned char*>(camera)+swvr::game_profile::transform_offset,sizeof(transform));
                if(avatar_enabled)avatar_pose.camera_rebased(transform);
                if(wheel_enabled)driver_wheel.camera_rebased(transform);
            }
            swvr::apply_game_eye_projection({static_cast<unsigned char*>(camera),swvr::game_profile::camera_size},active_eye_fov[lens_index]);
            ++eye_projection_updates;
        }
        catch(...) {eye_projection_failed=true;} // Never unwind through native game code.
    }
    if(!ready || !collect_events || events.load()>=limit) return;
    const auto data=static_cast<const unsigned char*>(camera);
    const auto address=static_cast<long long>(reinterpret_cast<std::uintptr_t>(camera));
    const auto caller=_ReturnAddress();
    // The original function has just written these fields. Observation only;
    // no guessed C++ class layout, writes to game state, or second simulation.
    record("camera_view",address,0,0,caller,nullptr,
           reinterpret_cast<const float*>(data+swvr::game_profile::view_offset));
    record("camera_projection",address,0,0,caller,nullptr,
           reinterpret_cast<const float*>(data+swvr::game_profile::projection_offset));
    record("camera_view_projection",address,0,0,caller,nullptr,
           reinterpret_cast<const float*>(data+swvr::game_profile::view_projection_offset));
}
void write_eye_image(const std::filesystem::path& path,const std::vector<unsigned char>& pixels,int width,int height) {
    std::ofstream image(path,std::ios::binary);
    if(!image) throw std::runtime_error("Cannot write eye diagnostic");
    image<<"P6\n"<<width<<' '<<height<<"\n255\n";
    const auto stride=static_cast<std::size_t>(width)*3;
    for(int row=height-1;row>=0;--row)
        image.write(reinterpret_cast<const char*>(pixels.data()+row*stride),stride);
    if(!image) throw std::runtime_error("Eye diagnostic write failed");
}
void xr_note(const char* key,const char* message) noexcept {
    try {
        std::lock_guard lock(log_mutex);
        if(output) {fprintf(output,"#%s\t%s\n",key,message);fflush(output);}
        // A small sidecar remains readable after the GL event cap and avoids
        // repeatedly scanning a large trace to supervise a bounded test.
        std::ofstream status(diagnostic_base.wstring()+L".xr.txt",std::ios::app);
        if(status) status<<'#'<<key<<'\t'<<message<<'\n';
    } catch(...) {}
}
void begin_hand_button_shutdown() noexcept {
    if(!hand_buttons_active || hand_cleanup_pending)return;
    // Native operations remain on live logic scopes. Render-thread shutdown
    // cancels intent and lets those scopes release/retire their own objects.
    swvr::reset_native_hand_buttons();
    swvr::note_native_button_retirement_reset();
    hand_cleanup_pending=true;hand_cleanup_started=GetTickCount64();
    xr_note("hand_buttons_drain","pending");
}
void poll_hand_button_shutdown() noexcept {
    if(!hand_cleanup_pending)return;
    try {
        const auto buttons=swvr::native_hand_buttons_stats();
        const auto retirement=swvr::native_button_retirement_stats();
        const bool idle=!buttons.own_hold && buttons.pending_events==0 && retirement.retained_records==0;
        const bool timed_out=GetTickCount64()-hand_cleanup_started>=5000;
        if(!idle && !timed_out)return;
        hand_cleanup_pending=false;
        const auto counts="selections="+std::to_string(buttons.selections)+" certified="+std::to_string(buttons.certified)+
            " queued="+std::to_string(buttons.events_queued)+" accepted="+std::to_string(buttons.presses_accepted)+
            " rejected="+std::to_string(buttons.presses_rejected)+" releases="+std::to_string(buttons.release_requests)+
            " faults="+std::to_string(buttons.faults)+" pending="+std::to_string(buttons.pending_events)+
            " own_hold="+std::to_string(buttons.own_hold)+" blocked="+std::to_string(buttons.blocked)+
            " last_fault="+buttons.last_fault+" lifecycle_releases="+std::to_string(buttons.lifecycle_releases)+
            " keypad_hand_releases="+std::to_string(buttons.keypad_hand_releases)+
            " selection_denials="+std::to_string(buttons.selection_denials)+" last_selection_denial="+buttons.last_selection_denial+
            " non_button_targets="+std::to_string(buttons.non_button_targets)+" targeted_types="+[&]{
                std::string list;for(std::size_t i=0;i<buttons.targeted_types.size();++i)list+=(i?",":"")+std::to_string(buttons.targeted_types[i]);return list;}();
        xr_note("hand_buttons_totals",counts.c_str());
        const auto retired="registered="+std::to_string(retirement.registrations)+
            " write="+std::to_string(retirement.write_retired)+" localhost="+std::to_string(retirement.localhost_retired)+
            " retained="+std::to_string(retirement.retained_records)+" quarantined="+std::to_string(retirement.quarantined)+
            " unknown_deletes="+std::to_string(retirement.unknown_deletes)+
            " source_reset="+std::to_string(retirement.source_reset_retired)+" source_destroy="+std::to_string(retirement.source_destroy_retired);
        xr_note("button_retirement_totals",retired.c_str());
        swvr::flush_native_hand_buttons();swvr::flush_native_button_retirement();
        if(!idle)xr_note("openxr_error","Hand-button cleanup did not drain within five seconds");
        else if(buttons.blocked || buttons.faults || retirement.quarantined)
            xr_note("openxr_error","Hand-button backend ended with unresolved or failed operations");
        else xr_note("hand_buttons_drain","complete");
    } catch(...) {
        hand_cleanup_pending=false;xr_note("openxr_error","Could not inspect hand-button shutdown");
    }
}
void* original_gl_proc(const char* name) {return reinterpret_cast<void*>(real_get(name));}
void apply_head_mask(bool active) noexcept {
    if(!head_mask_enabled) return;
    try {head_mask.before_draw(active);}
    catch(const std::exception& error) {
        head_mask_enabled=false;xr_note("head_mask_error",error.what());
    }
}
bool head_eye_active() {return active_eye_fov!=nullptr || (head_mask_fixture && fixture_eye_draw);}
std::uintptr_t current_context() {return reinterpret_cast<std::uintptr_t>(wglGetCurrentContext());}
swvr::HeadMaskShaderApi shader_api() {
    using GetShader=void(APIENTRY*)(GLuint,GLenum,GLint*);
    const auto pointer=real_get("glGetShaderiv");
    const bool valid=pointer && pointer!=reinterpret_cast<PROC>(1) && pointer!=reinterpret_cast<PROC>(2) &&
        pointer!=reinterpret_cast<PROC>(3) && pointer!=reinterpret_cast<PROC>(-1);
    return {real_shader_source,real_compile_shader,valid?reinterpret_cast<GetShader>(pointer):nullptr};
}
void APIENTRY shader_source(GLuint shader,GLsizei count,const char* const* strings,const GLint* lengths) {
    // A new source submission supersedes any older fallback, even when the
    // feature has since been disabled or the new shader is unsupported.
    if(head_mask_requested) try {shader_sources.forget(current_context(),shader);} catch(...) {}
    if(wheel_requested)try {wheel_sources.forget(current_context(),shader);}catch(...){}
    if(wheel_enabled)try {
        if(wheel_sources.submit(current_context(),shader,count,strings,lengths,shader_api(),xr_note))return;
    }catch(const std::exception& error){wheel_failed(error.what());}
    if(head_mask_enabled && count>0 && count<=1024 && strings) try {
        if(shader_sources.submit(current_context(),shader,count,strings,lengths,shader_api(),xr_note)) return;
    } catch(const std::exception& error) {xr_note("head_mask_shader_error",error.what());}
    real_shader_source(shader,count,strings,lengths);
}
void APIENTRY compile_shader(GLuint shader) {
    real_compile_shader(shader);
    if(wheel_requested)try{wheel_sources.compiled(current_context(),shader,shader_api(),xr_note);}catch(const std::exception& error){wheel_failed(error.what());}
    if(!head_mask_requested) return;
    try {shader_sources.compiled(current_context(),shader,shader_api(),xr_note);}
    catch(const std::exception& error) {xr_note("head_mask_shader_error",error.what());}
}
void APIENTRY link_program(GLuint program) {
    real_link_program(program);
    if(wheel_enabled)try{driver_wheel.program_linked(program);}catch(const std::exception& error){wheel_failed(error.what());}
    if(avatar_enabled)try {avatar_pose.program_linked(program);}catch(const std::exception& error) {avatar_failed(error.what());}
    if(head_mask_requested) try {head_mask.program_linked(program);} catch(...) {head_mask_enabled=false;}
}
void APIENTRY delete_shader(GLuint shader) {
    if(wheel_requested)try{wheel_sources.forget(current_context(),shader);}catch(...){}
    if(head_mask_requested) try {shader_sources.forget(current_context(),shader);} catch(...) {}
    real_delete_shader(shader);
}
void APIENTRY uniform_int(GLint location,GLint value) {real_uniform_int(location,value);apply_head_mask(head_eye_active());}
void APIENTRY uniform_float(GLint location,GLfloat value) {real_uniform_float(location,value);apply_head_mask(head_eye_active());}
BOOL WINAPI delete_context(HGLRC context) {
    const auto result=real_delete_context(context);
    if(result && wheel_requested)try{driver_wheel.context_destroyed(context);wheel_sources.context_destroyed(reinterpret_cast<std::uintptr_t>(context));}catch(...){}
    if(result && avatar_enabled)try {avatar_pose.context_destroyed(context);}catch(const std::exception& error) {avatar_failed(error.what());}
    if(result && head_mask_requested) try {
        head_mask.context_destroyed(context);
        shader_sources.context_destroyed(reinterpret_cast<std::uintptr_t>(context));
    } catch(...) {}
    return result;
}
std::filesystem::path configured_path(const wchar_t* name) {
    wchar_t path[32768]{};const auto length=GetEnvironmentVariableW(name,path,32768);
    if(!length)return {};
    if(length>=32768||!std::filesystem::path(path).is_absolute())throw std::invalid_argument("Expected absolute configuration path");
    return path;
}
bool poll_gesture_change(swvr::HandRecordingWriter&,swvr::GlHandOverlay& overlay,std::optional<swvr::GestureChange> menu_change={}) {
    if(!locomotion_requested&&!menu_change&&!gesture_changes.pending())return false;
    bool requested=menu_change.has_value()||(gesture_reload_request&&WaitForSingleObject(gesture_reload_request,0)==WAIT_OBJECT_0);
    auto mode=arm_swing_requested?swvr::LocomotionMode::arm_swing:swvr::LocomotionMode::joystick;
    if(joystick_mode_request&&WaitForSingleObject(joystick_mode_request,0)==WAIT_OBJECT_0){mode=swvr::LocomotionMode::joystick;requested=true;}
    if(arm_mode_request&&WaitForSingleObject(arm_mode_request,0)==WAIT_OBJECT_0){mode=swvr::LocomotionMode::arm_swing;requested=true;}
    bool notice{};
    if(requested) {
        notice=true;
        try {
            if(gesture_changes.pending())throw std::runtime_error("Gesture change already pending");
            auto settings=menu_change?menu_change->settings:gesture_config_path.empty()?swvr::GestureSettings{}:swvr::load_gesture_settings(gesture_config_path);
            if(menu_change)mode=menu_change->mode;
            settings.input.movement_fists=mode==swvr::LocomotionMode::arm_swing;
            if(locomotion_requested&&!swvr::pause_native_locomotion(true))throw std::runtime_error("Cannot pause native movement backend");
            if(!locomotion_requested)hand_locomotion.suspend(true);
            gestures_suspended=true;gesture_change_failed=false;
            hand_interactions.reset();swvr::reset_native_hand_buttons();
            gesture_changes.request({settings,mode},swvr::GestureChangeQueue::Clock::now());
            persist_gesture_change=menu_change.has_value();
            xr_note("gesture_reload","waiting_for_input_release");
            gesture_notice_text="ZMIANA - CZEKAM NA ZWOLNIENIE";
            overlay.status_text("ZMIANA USTAWIEN - ZATRZYMANIE",std::chrono::milliseconds(1500));
        }catch(const std::exception& e){gesture_notice_text="USTAWIENIA ODRZUCONE";xr_note("gesture_reload_rejected",e.what());overlay.status_text("USTAWIENIA ODRZUCONE - SPRAWDZ LOG",std::chrono::milliseconds(1500));}
    }
    if(gesture_changes.pending()) {
        const auto native=swvr::native_locomotion_stats();const auto buttons=swvr::native_hand_buttons_stats();
        const auto status=gesture_changes.poll(native.holding||buttons.own_hold||buttons.pending_events!=0,
            native.faults!=0||buttons.faults!=0||buttons.blocked,swvr::GestureChangeQueue::Clock::now());
        if(status==swvr::GestureChangeStatus::ready) {
            const auto change=*gesture_changes.take();hand_locomotion.configure(change.settings);hand_locomotion.set_mode(change.mode);
            active_gesture_settings=change.settings;arm_swing_requested=change.mode==swvr::LocomotionMode::arm_swing;
            hand_interactions.set_priority(static_cast<swvr::InteractionHand>(change.settings.input.pointing_priority));swvr::reset_native_hand_buttons();
            const bool resumed=!locomotion_requested||swvr::pause_native_locomotion(false);
            if(!locomotion_requested)hand_locomotion.suspend(false);
            if(resumed) {
                gestures_suspended=false;gesture_change_failed=false;
                if(locomotion_requested)xr_note("locomotion_mode",arm_swing_requested?"arm_swing":"joystick");
                xr_note("input_preferences",(std::string("joystick_hand=")+(active_gesture_settings.input.joystick_hand==swvr::GestureHand::left?"left":"right")+" pointing_priority="+(active_gesture_settings.input.pointing_priority==swvr::GestureHand::left?"left":"right")).c_str());
                xr_note("gesture_settings",swvr::describe_gesture_settings(active_gesture_settings).c_str());
                xr_note("gesture_reload","applied_after_release");
                gesture_notice_text="USTAWIENIA PRZYJETE";
                if(std::exchange(persist_gesture_change,false))try{
                    if(gesture_config_path.empty())throw std::runtime_error("No settings path");
                    swvr::save_gesture_settings(gesture_config_path,active_gesture_settings);gesture_notice_text="USTAWIENIA ZAPISANE";xr_note("gesture_preferences","saved");
                }catch(const std::exception& e){gesture_notice_text="DZIALA / BLAD ZAPISU USTAWIEN";xr_note("gesture_preferences_save_error",e.what());}
                overlay.status_text("USTAWIENIA PRZYJETE",std::chrono::milliseconds(1500));
            }else{gesture_change_failed=true;gesture_notice_text="BLAD ZMIANY - RUCH WSTRZYMANY";xr_note("gesture_reload_error","Native backend faulted while resuming");}
            notice=true;
        }else if(status==swvr::GestureChangeStatus::fault||status==swvr::GestureChangeStatus::timeout) {
            gesture_change_failed=true;notice=true;
            gesture_notice_text="BLAD ZMIANY - RUCH WSTRZYMANY";
            xr_note("gesture_reload_error",status==swvr::GestureChangeStatus::fault?"Native input fault; movement remains suspended":"Input release timed out; movement remains suspended");
            overlay.status_text("ZMIANA NIEUDANA - RUCH WSTRZYMANY",std::chrono::milliseconds(1500));
        }
    }
    return notice;
}
void flush_locomotion_journal(swvr::HandRecordingWriter& recording,bool stop=false){
    if(!record_hands_requested)return;
    const auto batch=hand_locomotion.drain_journal(stop||!recording.active());
    if(batch.overflow){recording.mark_incomplete();xr_note("hand_recording_error","Input journal overflow; recording is incomplete");}
    for(const auto& event:batch.events)if(recording.active()&&!recording.append(event))break;
}
struct NativeVr {
    using Clock=std::chrono::steady_clock;
    static double elapsed(Clock::time_point from) {return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}
    swvr::XrRuntime runtime;
    swvr::XrReadinessMonitor readiness;
    swvr::GlXrPresenter presenter;
    swvr::GlHandOverlay hand_overlay;
    swvr::NativeEyeResources resources;
    void* original_targets{};
    bool resources_ready_logged{};
    bool target_suspended{};
    swvr::XrRuntimeFrame current{};
    std::optional<swvr::Pose> origin;
    // Calibrated neck pivot and the per-frame effective origin after the
    // seated head limit. All consumers of tracked poses use view_origin.
    swvr::Vec3 neck_reference{};
    swvr::Pose view_origin{};
    swvr::ControlRecording control_recording;
    swvr::PerformanceReport performance;
    swvr::GlGpuTimer gpu_timer;
    swvr::UiClickSound click_sound;
    swvr::ComfortMotion comfort_motion;
    float head_fade{};double frame_turn{};
    Clock::time_point work_started{};bool work_measured{};
    void collect_gpu(){while(const auto sample=gpu_timer.poll())performance.gpu_sample(sample->milliseconds,sample->budget_ms,sample->frame);performance.gpu_available=gpu_timer.supported();performance.gpu_skipped=gpu_timer.skipped();performance.gpu_pending=gpu_timer.pending();}
    void menu_click(){if(active_gesture_settings.input.click_sound)click_sound.play();}
    double last_avatar_cpu{},last_recording_cpu{};
    std::uint64_t initial_cache_hits{},initial_pose_calls{};
    bool head_recenter_pending{};
    std::optional<swvr::Vec3> calibrated_head_position;
    swvr::HandTurn hand_turn;
    std::uint64_t head_frames{},head_limited_frames{};
    float head_max_extent{},head_max_correction{};
    std::optional<swvr::AvatarBodyCalibration> avatar_body;
    std::optional<swvr::AvatarProfile> stored_body;
    swvr::NumericKeypad keypad;
    std::uint64_t vehicle_keypad_generation{},vehicle_keypad_seen{};
    swvr::VrSettingsMenu settings_menu;
    swvr::VrMenuSummon menu_summon;
    std::optional<swvr::GestureChange> menu_change;
    unsigned panel_priority=1;
    bool calibration_guided{};
    Clock::time_point guidance_deadline{};
    swvr::HandRecordingWriter hand_recording;
    Clock::time_point calibration_feedback_until{},control_feedback_until{},menu_feedback_until{};
    std::uint64_t avatar_calibration_generation{};
    swvr::AvatarCalibrationHold t_pose_hold;
    swvr::AvatarCalibrationWindow calibration_window;
    std::map<std::string,std::uint64_t> t_pose_issues; // Eye-0 frames per rejection reason.
    std::uint64_t t_pose_valid_frames{},t_pose_accepted{};
    std::array<swvr::Pose,2> relative_eyes{};
    std::optional<std::array<swvr::Pose,2>> previous_eyes;
    std::array<swvr::Fov,2> previous_fovs{};
    void* camera_owner{};
    swvr::Matrix4d base_render{},base_cull{};
    swvr::Matrix4d current_look{},previous_look{};
    std::array<swvr::Matrix4d,3> head_anchors{};
    swvr::HeadAnchorSettings head_settings{};
    bool early_renderable{},base_cameras_valid{},cull_failed{};
    std::uint64_t early_frame{},cull_updates{},forced_culls{},frame_culls{},history_resets{};
    std::array<swvr::PinchDetector,2> pinches;
    std::uint64_t revision{},last_menu_frame{},submitted{},hand_samples[2]{},pinch_begins[2]{},pinch_releases[2]{};
    ULONGLONG started=GetTickCount64();
    double render_ms{},capture_ms{},xr_begin_ms{},xr_submit_ms{},interval_ms{};
    std::uint64_t intervals{};
    std::optional<Clock::time_point> previous_begin;
    std::ofstream camera_samples;
    NativeVr() {
        hand_interactions.reset();
        if(avatar_enabled&&!avatar_profile_path.empty())try{
            stored_body=swvr::load_avatar_profile(avatar_profile_path);
            xr_note("avatar_profile",stored_body?"loaded":"not_saved");
        }catch(const std::exception& e){xr_note("avatar_profile_error",e.what());}
        if(record_hands_requested)try{
            hand_recording.open_journal(diagnostic_base.wstring()+L".hands.tsv");hand_locomotion.start_journal();
            control_recording.open(diagnostic_base.wstring()+L".controls.tsv");
            settings_menu.record_to(&control_recording);keypad.record_to(&control_recording);menu_summon.record_to(&control_recording);hand_turn.record_to(&control_recording);
            xr_note("hand_recording","journal_v3_limit_64MiB");
        }catch(const std::exception& e){xr_note("hand_recording_error",e.what());}
        {const auto stats=avatar_pose.stats();last_avatar_cpu=stats.pose_cpu_ms;initial_cache_hits=stats.cache_hits;initial_pose_calls=stats.pose_calls;}
        if(hand_ray_active && swvr::rearm_native_hand_ray_log())
            xr_note("hand_ray_log","rearmed_at_vr_start");
        if(input_probe_active && swvr::rearm_native_input_probe_log())
            xr_note("input_probe_log","rearmed_at_vr_start");
        runtime.initialize({xr_loader,wglGetCurrentDC(),wglGetCurrentContext()});
        xr_note("openxr_runtime",runtime.info().runtime_name.c_str());
        xr_note("openxr_system",runtime.info().system_name.c_str());
        xr_note("system_hand_input",runtime.info().hand_system_gesture_extension?"runtime_flag_and_palm_guard":"palm_guard_no_runtime_flag");
        xr_note("openxr_color",runtime.info().srgb?"srgb_encoded_copy":"rgba8_encoded_copy_unverified");
        const auto mode=std::string(gameplay_preview?"initialized_gameplay_experiment_seconds=":"initialized_menu_experiment_seconds=")+
            std::to_string(preview_duration_ms/1000);
        xr_note("openxr_state",mode.c_str());
        if(gameplay_preview) {
            camera_samples.open(diagnostic_base.wstring()+L".camera-world.tsv",std::ios::out);
            camera_samples<<"qpc\tframe\txr_id";
            for(int i=0;i<16;++i) camera_samples<<"\tbase_t"<<i;
            for(int i=0;i<16;++i) camera_samples<<"\thead_t"<<i;
            for(int i=0;i<16;++i) camera_samples<<"\tlook_t"<<i;
            camera_samples<<"\tmode_bits\tbackoff\tshift_x\tshift_y\tshift_z";
            camera_samples<<"\torigin_qx\torigin_qy\torigin_qz\torigin_qw\torigin_x\torigin_y\torigin_z";
            camera_samples<<"\thand_sequence\thand_valid\thand_status\thand_x\thand_y\thand_z\thand_dx\thand_dy\thand_dz\tpinch_gesture_id\tpinch_down";
            camera_samples<<'\n';camera_samples.precision(17);
        }
    }
    void reset_history() {
        if(previous_eyes) {previous_eyes.reset();++history_resets;}
    }
    // Body yaw following (opt-in): mapping of the native yaw field and counters.
    // NativeVr is created only after the full EXE SHA and camera hook guards.
    swvr::LookYawCalibration look_yaw{10,swvr::game_profile::look_yaw_sign};
    bool has_previous_base{};
    std::uint64_t body_yaw_frames{},body_yaw_writes{};
    double body_yaw_total{};
    // Accumulated artificial (mouse/key) turn applied to the view while walking.
    double turn_yaw{},turn_total{};
    float written_yaw{};bool wrote_yaw{};
    float settled_yaw{};bool has_settled_yaw{}; // Field left by the previous native update.
    // Last fully presented native images and the XR views they were rendered
    // with, for resubmission when a frame has no new image.
    swvr::GlEyeTargets* last_images{};
    std::optional<swvr::XrViewOverride> last_views;
    Clock::time_point last_render_time{};
    std::uint64_t last_revision{},repeated_frames{},empty_frames{};
    std::uint64_t empty_hidden{},empty_present_failed{},empty_no_previous{},empty_recentred{},empty_too_old{};
    void create_resources(void* targets) {
        if(!independent_native_eyes) return;
        void* manager{};
        {
            std::lock_guard lock(resource_mutex);
            const auto found=package_managers.find(targets);
            if(found!=package_managers.end()) manager=found->second;
        }
        if(!manager) throw std::runtime_error("No measured manager for this native target package");
        original_targets=targets;
        const auto bytes=static_cast<const unsigned char*>(targets);
        const unsigned char* gbuffer{};std::memcpy(&gbuffer,bytes,sizeof(gbuffer));
        unsigned attachments{},width{},height{},config_cc{};
        if(gbuffer) std::memcpy(&attachments,gbuffer+0x58,sizeof(attachments));
        std::memcpy(&width,bytes+0xc0,sizeof(width));std::memcpy(&height,bytes+0xc4,sizeof(height));
        std::memcpy(&config_cc,bytes+0xcc,sizeof(config_cc));
        const auto config="size="+std::to_string(width)+"x"+std::to_string(height)+" attachments="+
            std::to_string(attachments)+" CC="+std::to_string(config_cc)+" C9="+std::to_string(bytes[0xc9]);
        xr_note("native_eye_source_config",config.c_str());
        resources.create({static_cast<const unsigned char*>(targets),swvr::native_target_package_size},manager,resource_api);
        xr_note("native_eye_resources","created_pending_realization");
    }
    bool resources_ready(void* targets) {
        if(!independent_native_eyes) return true;
        if(targets!=original_targets) throw std::runtime_error("Native target package changed during preview");
        const std::span<const unsigned char> source{static_cast<const unsigned char*>(targets),swvr::native_target_package_size};
        if(!resources.matches_configuration(source)) {
            resources.close();resources_ready_logged=false;reset_history();hand_interactions.reset();
            xr_note("native_eye_resources","recreating_after_source_configuration_change");
            create_resources(targets);return false;
        }
        const bool complete=resources.ready(source);
        if(complete && !resources_ready_logged) {
            resources_ready_logged=true;
            xr_note("native_eye_resources","ready_distinct_resource_and_FBO_names");
        }
        return complete;
    }
    bool begin() {
        last_menu_frame=frame.load();
        const double interval=previous_begin?elapsed(*previous_begin):0;
        if(previous_begin) {interval_ms+=elapsed(*previous_begin);++intervals;}
        previous_begin=Clock::now();
        const auto begin_time=Clock::now();
        current=runtime.begin_frame();
        if(interval>0)performance.interval.observe(interval,current.id);
        xr_begin_ms+=elapsed(begin_time);
        if(current.terminal) throw std::runtime_error("OpenXR session became terminal");
        frame_turn=0;head_fade=0;work_measured=current.should_render;work_started=Clock::now();collect_gpu();
        if(work_measured)gpu_timer.begin(current.id,static_cast<double>(current.predicted_display_period_ns)/1e6);
        flush_locomotion_journal(hand_recording);
        const bool was_seated=active_gesture_settings.comfort.seated;
        if(poll_gesture_change(hand_recording,hand_overlay,std::exchange(menu_change,{}))){hand_turn.reset();control_feedback_until=Clock::now()+std::chrono::milliseconds(1500);menu_feedback_until=control_feedback_until;settings_menu.message(gesture_notice_text);}
        if(was_seated!=active_gesture_settings.comfort.seated){head_recenter_pending=true;calibrated_head_position.reset();comfort_motion.reset();}
        const auto next_priority=static_cast<unsigned>(active_gesture_settings.input.pointing_priority);
        if(panel_priority!=next_priority){panel_priority=next_priority;settings_menu.set_preferred_hand(panel_priority);keypad.set_preferred_hand(panel_priority);}
        if(calibration_guided&&Clock::now()>guidance_deadline){calibration_guided=false;hand_locomotion.reset("calibration_timeout");hand_overlay.status_text("KALIBRACJA - CZAS MINAL",std::chrono::milliseconds(1500));control_feedback_until=Clock::now()+std::chrono::milliseconds(1500);}
        if(!settings_menu.snapshot().visible&&!keypad.snapshot().visible&&!calibration_guided&&menu_summon.update(current,active_gesture_settings.input.dwell_ms)){
            menu_click();
            settings_menu.set_preferred_hand(static_cast<unsigned>(active_gesture_settings.input.pointing_priority));
            if(settings_menu.open(current,{locomotion_requested,avatar_enabled,true},active_gesture_settings,arm_swing_requested)){
                menu_summon.reset();calibration_guided=false;hand_interactions.reset();swvr::reset_native_hand_buttons();hand_locomotion.reset("settings_menu");xr_note("vr_settings_menu","opened_by_touch");
            }
        }
        if(settings_menu.snapshot().visible){
            settings_menu.status(active_gesture_settings,arm_swing_requested);
            if(Clock::now()>=menu_feedback_until)settings_menu.message({});
            if(const auto action=settings_menu.update(current)){
                xr_note("vr_menu_action",std::to_string(static_cast<unsigned>(*action)).c_str());
                auto next=active_gesture_settings;auto next_mode=arm_swing_requested?swvr::LocomotionMode::arm_swing:swvr::LocomotionMode::joystick;bool change{};
                switch(*action){
                case swvr::VrMenuAction::joystick:next_mode=swvr::LocomotionMode::joystick;change=true;break;
                case swvr::VrMenuAction::arm_swing:next_mode=swvr::LocomotionMode::arm_swing;change=true;break;
                case swvr::VrMenuAction::reload:try{next=gesture_config_path.empty()?swvr::GestureSettings{}:swvr::load_gesture_settings(gesture_config_path);next_mode=next.input.movement_fists?swvr::LocomotionMode::arm_swing:swvr::LocomotionMode::joystick;change=true;}catch(const std::exception& e){settings_menu.message("BLAD PLIKU PARAMETROW");menu_feedback_until=Clock::now()+std::chrono::milliseconds(1500);xr_note("gesture_reload_rejected",e.what());}break;
                case swvr::VrMenuAction::joystick_left:next.input.joystick_hand=swvr::GestureHand::left;change=true;break;
                case swvr::VrMenuAction::joystick_right:next.input.joystick_hand=swvr::GestureHand::right;change=true;break;
                case swvr::VrMenuAction::priority_left:next.input.pointing_priority=swvr::GestureHand::left;change=true;break;
                case swvr::VrMenuAction::priority_right:next.input.pointing_priority=swvr::GestureHand::right;change=true;break;
                case swvr::VrMenuAction::calibrate:calibration_guided=true;t_pose_hold.reset();calibration_window.reset();guidance_deadline=Clock::now()+std::chrono::seconds(15);xr_note("avatar_calibration","guided_from_menu");break;
                case swvr::VrMenuAction::clear_profile:if(avatar_profile_clear_request)SetEvent(avatar_profile_clear_request);break;
                case swvr::VrMenuAction::keypad:hand_overlay.status_text("WSKAZ KLAWIATURE POJAZDU I UZYJ Q/E LUB GESTU PRZYCISKU",std::chrono::milliseconds(4000));control_feedback_until=Clock::now()+std::chrono::milliseconds(4000);xr_note("vehicle_keypad","select_vehicle_keypad");break;
                case swvr::VrMenuAction::recenter:head_recenter_pending=true;calibrated_head_position.reset();comfort_motion.reset();settings_menu.cancel();break;
                case swvr::VrMenuAction::close:break;
                default:change=swvr::adjust_vr_preferences(*action,next);if(*action==swvr::VrMenuAction::defaults)next_mode=next.input.movement_fists?swvr::LocomotionMode::arm_swing:swvr::LocomotionMode::joystick;break;
                }
                if(change)menu_change=swvr::GestureChange{next,next_mode};
                if(!settings_menu.snapshot().visible){menu_summon.reset();hand_interactions.reset();swvr::reset_native_hand_buttons();hand_locomotion.reset("menu_action");}
            }
        }
        if(settings_menu.take_click())menu_click();
        if(avatar_profile_clear_request&&WaitForSingleObject(avatar_profile_clear_request,0)==WAIT_OBJECT_0){
            stored_body.reset();avatar_body.reset();t_pose_hold.reset();calibration_window.reset();
            std::error_code error;if(!avatar_profile_path.empty())std::filesystem::remove(avatar_profile_path,error);
            xr_note(error?"avatar_profile_error":"avatar_profile",error?"Could not remove saved profile":"cleared");
            hand_overlay.status_text(error?"PROFIL NIEUSUNIETY - SPRAWDZ LOG":"KALIBRACJA USUNIETA",std::chrono::milliseconds(1500));
            control_feedback_until=Clock::now()+std::chrono::milliseconds(1500);
        }
        if(keypad_request&&WaitForSingleObject(keypad_request,0)==WAIT_OBJECT_0){
            settings_menu.cancel();menu_summon.reset();calibration_guided=false;
            if(keypad.snapshot().visible)keypad.cancel();
            else if(keypad.open(current,active_gesture_settings))xr_note("keypad_prototype","opened_local_only");
            else xr_note("keypad_prototype","open_rejected_tracking_or_focus");
            hand_interactions.reset();swvr::reset_native_hand_buttons();hand_locomotion.reset();
        }
        const auto vehicle_pad=swvr::vehicle_keypad_poll(current.running&&current.focused&&!current.terminal);
        if(vehicle_keypad_generation&&(!vehicle_pad||vehicle_pad->generation!=vehicle_keypad_generation)){
            keypad.cancel();keypad.take_result();vehicle_keypad_generation=0;
            hand_overlay.status_text("KLAWIATURA ZAMKNIETA LUB ZMIENIONA",std::chrono::milliseconds(1500));
        }
        if(vehicle_pad&&vehicle_pad->generation!=vehicle_keypad_seen&&current.focused){
            settings_menu.cancel();menu_summon.reset();calibration_guided=false;
            if(keypad.open_vehicle(current,active_gesture_settings,vehicle_pad->button_type,vehicle_pad->current)){
                vehicle_keypad_generation=vehicle_keypad_seen=vehicle_pad->generation;
                hand_interactions.reset();swvr::reset_native_hand_buttons();hand_locomotion.reset();hand_steering.reset();
                xr_note("vehicle_keypad","opened");
            }
        }
        keypad.update(current);if(keypad.take_click())menu_click();hand_overlay.keypad(settings_menu.snapshot().visible?settings_menu.snapshot():keypad.snapshot());
        hand_overlay.menu_button(!settings_menu.snapshot().visible&&!keypad.snapshot().visible&&!calibration_guided?menu_summon.snapshot():swvr::PanelSnapshot{});
        if(const auto result=keypad.take_result()){
            if(vehicle_keypad_generation){
                const bool queued=swvr::vehicle_keypad_reply(vehicle_keypad_generation,result->accepted?result->native_values:std::nullopt);
                xr_note("vehicle_keypad",queued?(result->accepted?"submit_queued":"cancel_queued"):"stale_reply_rejected");vehicle_keypad_generation=0;
            }else xr_note("keypad_prototype",result->accepted?("accepted_local value="+result->value).c_str():"cancelled");
            hand_interactions.reset();swvr::reset_native_hand_buttons();hand_locomotion.reset();
        }
        if(!current.focused){
            if(t_pose_hold.holding()){hand_overlay.status_text("KALIBRACJA - BRAK FOKUSU VR",std::chrono::milliseconds(1500));hand_overlay.progress(0,std::chrono::milliseconds(1500));calibration_feedback_until=Clock::now()+std::chrono::milliseconds(1500);}
            t_pose_hold.reset();calibration_window.reset();
        }
        if(const auto notice=readiness.observe(current,Clock::now())) {
            const auto message=std::string("reason=")+swvr::xr_readiness_name(notice->reason)+
                " waited_ms="+std::to_string(notice->waited.count());
            xr_note("openxr_readiness",message.c_str());
        }
        if(revision!=current.space_revision) {
            hand_steering.reset();
            revision=current.space_revision;origin.reset();reset_history();
            hand_turn.reset();comfort_motion.reset();head_recenter_pending=false;
            t_pose_hold.reset();calibration_window.reset();calibrated_head_position.reset();
            hand_interactions.reset();
            for(auto& pinch:pinches) pinch.reset();
            xr_note("openxr_calibration","LOCAL_changed");
        }
        for(unsigned i=0;i<2;++i) {
            const auto& hand=current.hands[i];
            const auto& thumb=hand.joints[swvr::xr_thumb_tip];
            const auto& index=hand.joints[swvr::xr_index_tip];
            const bool valid=thumb.position_valid && index.position_valid;
            const bool tracked=thumb.position_tracked && index.position_tracked;
            const auto gesture=pinches[i].update({hand.active,valid,tracked,current.focused,
                current.predicted_display_time_ns,thumb.pose.position,index.pose.position},current.predicted_display_time_ns);
            if(hand.active && valid && tracked) ++hand_samples[i];
            if(gesture==swvr::PinchEvent::began) ++pinch_begins[i];
            if(gesture==swvr::PinchEvent::released) ++pinch_releases[i];
            if(gesture!=swvr::PinchEvent::none && gesture!=swvr::PinchEvent::held)
                record("xr_pinch",i,static_cast<int>(gesture),current.focused,nullptr);
        }
        if(!current.id || !current.should_render) {reset_history();hand_interactions.reset();return false;}
        const std::array<swvr::Pose,2> eyes{current.eyes[0].pose,current.eyes[1].pose};
        if(!origin) {
            if(!current.focused || !current.position_tracked || !current.orientation_tracked) return false;
            origin=swvr::gravity_aligned_origin(eyes);
            const auto calibration="gravity_aligned_origin xr_id="+std::to_string(current.id)+" space_revision="+
                std::to_string(current.space_revision)+" pos="+std::to_string(origin->position.x)+","+
                std::to_string(origin->position.y)+","+std::to_string(origin->position.z)+" quat="+
                std::to_string(origin->orientation.x)+","+std::to_string(origin->orientation.y)+","+
                std::to_string(origin->orientation.z)+","+std::to_string(origin->orientation.w);
            xr_note("openxr_calibration",calibration.c_str());
            neck_reference=swvr::neck_position(*origin,eyes);
        }
        if(head_recenter_pending&&current.focused&&current.position_tracked&&current.orientation_tracked){
            hand_steering.reset();
            origin=swvr::recenter_head_origin(*origin,eyes);if(calibrated_head_position)origin->position=*std::exchange(calibrated_head_position,{});neck_reference=swvr::neck_position(*origin,eyes);
            head_recenter_pending=false;reset_history();hand_turn.reset();hand_interactions.reset();hand_locomotion.reset("head_recenter");
            xr_note("openxr_calibration","head_recentered");
        }
        view_origin=*origin;
        if(head_limit_enabled && gameplay_preview) {
            const auto comfort=swvr::head_comfort_view(*origin,neck_reference,eyes,head_limit,true,active_gesture_settings.comfort.head_fade);
            view_origin=comfort.origin;head_fade=comfort.fade;++head_frames;
            if(comfort.limited) {
                ++head_limited_frames;
                const swvr::Vec3 c{view_origin.position.x-origin->position.x,view_origin.position.y-origin->position.y,view_origin.position.z-origin->position.z};
                head_max_correction=std::max(head_max_correction,std::hypot(c.x,c.y,c.z));
            }
            head_max_extent=std::max(head_max_extent,comfort.extent);
        }
        relative_eyes=swvr::cockpit_views({},view_origin,eyes);
        return true;
    }
    void finish(swvr::GlEyeTargets* images=nullptr) {
        if(!current.id) return;
        const auto movement=hand_locomotion.inspect();
        const bool moving=!settings_menu.snapshot().visible&&!keypad.snapshot().visible&&!calibration_guided&&!gestures_suspended&&movement.active&&(movement.forward!=0||movement.sideways!=0);
        hand_overlay.comfort(comfort_motion.update(current,active_gesture_settings.comfort,moving,frame_turn,head_fade));
        const auto submit_time=Clock::now();
        std::array<bool,2> completed{};
        bool repeated{};
        if(images) {
            completed=presenter.present(*images,current.eyes);
            const auto pointing=hand_interactions.inspect();
            const auto overlay_start=Clock::now();hand_overlay.draw(current,hand_ray_active && pointing.valid_ray,static_cast<unsigned>(pointing.hand));performance.overlay.observe(elapsed(overlay_start),current.id);
        } else if(last_images && last_views && current.should_render && last_revision==current.space_revision &&
                  submit_time-last_render_time<=std::chrono::milliseconds(250)) {
            // No new native image this frame (maintenance frame, renderer not
            // reached, tiny source). Resubmit the previous images with the poses
            // they were rendered with; the compositor reprojects them instead of
            // showing a black frame. The hand skeleton uses the same old views.
            completed=presenter.present(*last_images,current.eyes);
            auto stale=current;
            for(unsigned i=0;i<2;++i) {stale.eyes[i].pose=last_views->poses[i];stale.eyes[i].fov=last_views->fov[i];}
            const auto pointing=hand_interactions.inspect();
            const auto overlay_start=Clock::now();hand_overlay.draw(stale,hand_ray_active && pointing.valid_ray,static_cast<unsigned>(pointing.hand));performance.overlay.observe(elapsed(overlay_start),current.id);
            repeated=completed[0] && completed[1];
        }
        gpu_timer.end();if(work_measured){performance.cpu_sample(elapsed(work_started),static_cast<double>(current.predicted_display_period_ns)/1e6,current.id);work_measured=false;}collect_gpu();
        if(repeated) runtime.end_frame(current.id,completed,*last_views);
        else runtime.end_frame(current.id,completed);
        const bool fresh=images && completed[0] && completed[1];
        if(fresh) {
            previous_eyes=relative_eyes;
            previous_fovs={current.eyes[0].fov,current.eyes[1].fov};
            last_images=images;last_render_time=submit_time;last_revision=current.space_revision;
            last_views=swvr::XrViewOverride{{current.eyes[0].pose,current.eyes[1].pose},{current.eyes[0].fov,current.eyes[1].fov}};
        } else reset_history();
        if(repeated) ++repeated_frames;
        else if(!fresh) {
            ++empty_frames;
            // Why no image could be shown: diagnostic for black frames.
            if(!current.should_render)++empty_hidden;
            else if(images)++empty_present_failed;
            else if(!last_images||!last_views)++empty_no_previous;
            else if(last_revision!=current.space_revision)++empty_recentred;
            else ++empty_too_old;
        }
        const auto avatar_ms=avatar_pose.stats().pose_cpu_ms,recording_ms=hand_recording.cpu_ms()+control_recording.cpu_ms();
        performance.avatar.observe(avatar_ms-last_avatar_cpu,current.id);last_avatar_cpu=avatar_ms;
        performance.recording.observe(recording_ms-last_recording_cpu,current.id);last_recording_cpu=recording_ms;
        current={};early_renderable=false;base_cameras_valid=false;
        xr_submit_ms+=elapsed(submit_time);
        if(fresh) ++submitted;
    }
    void close() {
        avatar_pose.end_eye();driver_wheel.end_eye();
        hand_steering.reset();swvr::vehicle_keypad_poll(false);
        const auto keypad_stats=swvr::vehicle_keypad_stats();xr_note("vehicle_keypad_totals",("opened="+std::to_string(keypad_stats.opened)+" submitted="+std::to_string(keypad_stats.submitted)+" cancelled="+std::to_string(keypad_stats.cancelled)+" rejected="+std::to_string(keypad_stats.rejected)+" faults="+std::to_string(keypad_stats.faults)).c_str());
        hand_interactions.reset();
        if(locomotion_requested) {
            hand_locomotion.reset();locomotion_cleanup_pending=true;locomotion_cleanup_started=GetTickCount64();
            xr_note("locomotion_drain","pending");
        }
        begin_hand_button_shutdown();
        flush_locomotion_journal(hand_recording,true);
        hand_recording.close();
        control_recording.close();
        if(record_hands_requested)xr_note("control_recording_totals",("limited="+std::to_string(control_recording.limited())+" failed="+std::to_string(control_recording.failed())).c_str());
        try{gpu_timer.cancel();collect_gpu();const auto stats=avatar_pose.stats();performance.save(diagnostic_base.wstring()+L".performance.json",stats.cache_hits-initial_cache_hits,stats.pose_calls-initial_pose_calls,repeated_frames,empty_frames);xr_note("performance_report","written");}catch(const std::exception& e){xr_note("performance_report_error",e.what());}
        if(click_sound.failed())xr_note("menu_click_sound","audio_output_unavailable");
        if(record_hands_requested)xr_note("hand_recording_totals",("samples="+std::to_string(hand_recording.samples())+" limited="+std::to_string(hand_recording.limited())+" failed="+std::to_string(hand_recording.failed())).c_str());
        if(hand_recording.failed())xr_note("hand_recording_error","Recording did not finish cleanly");
        camera_samples.flush();
        runtime.cancel_frame();runtime.shutdown();
        if(resources.created()) {
            resources.close();
            xr_note("native_eye_resources","references_released");
        }
        const auto counts="submitted="+std::to_string(submitted)+" left_hand_samples="+
            std::to_string(hand_samples[0])+" right_hand_samples="+std::to_string(hand_samples[1])+
            " left_pinches="+std::to_string(pinch_begins[0])+" right_pinches="+std::to_string(pinch_begins[1])+
            " left_releases="+std::to_string(pinch_releases[0])+" right_releases="+std::to_string(pinch_releases[1]);
        xr_note("openxr_totals",counts.c_str());
        xr_note("openxr_frame_reuse",("repeated="+std::to_string(repeated_frames)+" empty="+std::to_string(empty_frames)+
            " empty_hidden="+std::to_string(empty_hidden)+" empty_present_failed="+std::to_string(empty_present_failed)+
            " empty_no_previous="+std::to_string(empty_no_previous)+" empty_recentred="+std::to_string(empty_recentred)+
            " empty_too_old="+std::to_string(empty_too_old)).c_str());
        const auto divisor=static_cast<double>(submitted?submitted:1);
        const auto timing="frames="+std::to_string(submitted)+" render_pair_avg_ms="+std::to_string(render_ms/divisor)+
            " capture_pair_avg_ms="+std::to_string(capture_ms/divisor)+" xr_begin_avg_ms="+std::to_string(xr_begin_ms/divisor)+
            " xr_submit_avg_ms="+std::to_string(xr_submit_ms/divisor)+" between_begin_avg_ms="+
            std::to_string(interval_ms/static_cast<double>(intervals?intervals:1))+" session_elapsed_ms="+
            std::to_string(GetTickCount64()-started);
        xr_note("openxr_cpu_timing",timing.c_str());
        const auto culling="planes_updated="+std::to_string(cull_updates)+" extra_visibility_passes="+std::to_string(forced_culls)+
            " history_resets="+std::to_string(history_resets);
        xr_note("openxr_culling",culling.c_str());
        if(body_yaw_enabled && gameplay_preview) {
            const auto yaw="frames="+std::to_string(body_yaw_frames)+" writes="+std::to_string(body_yaw_writes)+
                " total_deg="+std::to_string(body_yaw_total*57.29577951308232)+" calibrated="+std::to_string(look_yaw.locked())+
                " sign="+std::to_string(look_yaw.sign())+" mouse_turn_deg="+std::to_string(turn_total*57.29577951308232);
            xr_note("body_yaw_totals",yaw.c_str());
        }
        if(head_limit_enabled && gameplay_preview)
            xr_note("head_limit_totals",swvr::format_head_limit_totals({head_frames,head_limited_frames,head_max_extent,head_max_correction}).c_str());
        if(avatar_requested) {
            std::string pose="valid_frames="+std::to_string(t_pose_valid_frames)+" longest_hold_ms="+
                std::to_string(t_pose_hold.longest().count())+" holds="+std::to_string(t_pose_hold.holds())+" accepted="+std::to_string(t_pose_accepted);
            for(const auto& [reason,count]:t_pose_issues)pose+=" "+reason+"="+std::to_string(count);
            xr_note("avatar_calibration_totals",pose.c_str());
        }
        if(avatar_requested) {
            xr_note("avatar_pose_totals",swvr::format_avatar_pose_totals(avatar_pose.stats()).c_str());
            if(avatar_identity)xr_note("avatar_identity_totals",swvr::format_avatar_identity_totals(avatar_identity->stats()).c_str());
            try {
                const auto journal=avatar_pose.journal();
                std::ofstream draws(diagnostic_base.wstring()+L".avatar-draws.tsv");
                draws<<swvr::avatar_journal_header()<<'\n';
                for(const auto& entry:journal)draws<<swvr::format_avatar_journal_row(entry)<<'\n';
                xr_note("avatar_draw_journal",("entries="+std::to_string(journal.size())).c_str());
            } catch(const std::exception& error) {xr_note("avatar_draw_journal_error",error.what());}
        }
        if(wheel_requested){
            const auto s=driver_wheel.stats();xr_note("driver_wheel_totals",("eyes="+std::to_string(s.eyes)+" identified="+std::to_string(s.identified_eyes)+
                " draws="+std::to_string(s.draws)+" depth_draws="+std::to_string(s.depth_draws)+" patched_shaders="+std::to_string(wheel_sources.patched())+
                " compile_fallbacks="+std::to_string(wheel_sources.fallbacks())).c_str());
        }
        if(head_mask_requested) {
            const auto h=head_mask.stats();
            const auto masking="patched_shaders="+std::to_string(shader_sources.patched())+
                " compile_fallbacks="+std::to_string(shader_sources.fallbacks())+
                " candidate_calls="+std::to_string(h.candidate_calls)+" masked_calls="+std::to_string(h.masked_calls);
            xr_note("head_mask_totals",masking.c_str());
        }
        xr_note("openxr_state","closed");
    }
};
// Explicit render-thread lifetime. If the host exits before the bounded test
// closes, leave the instance for process teardown instead of invoking XR/native
// resource callbacks from a global destructor under the DLL loader lock.
NativeVr* native_vr{};
void stop_native_xr(const char* reason) noexcept {
    if(openxr_preview || native_vr) stereo_preview=false;
    openxr_preview=false;
    if(native_vr) {
        std::unique_ptr<NativeVr> closing(native_vr);native_vr=nullptr;
        xr_note("openxr_stop_reason",reason);
        try {closing->close();} catch(const std::exception& error) {xr_note("openxr_shutdown_error",error.what());}
    }
}
swvr::Matrix4d native_transform(const void* camera) {
    if(!camera) throw std::runtime_error("Missing native camera");
    std::uintptr_t vtable{};std::memcpy(&vtable,camera,sizeof(vtable));
    if(vtable!=executable_base+swvr::game_profile::camera_vtable_rva) throw std::runtime_error("Unexpected native camera class");
    swvr::Matrix4d transform{};
    std::memcpy(transform.data(),static_cast<const unsigned char*>(camera)+swvr::game_profile::transform_offset,sizeof(transform));
    return transform;
}
void camera_owner_update(void* owner,void* graphics,float alpha,std::uint8_t fourth) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-executable_base;
    const bool selected=caller==0x7b0446 && gameplay_preview && gameplay_armed && preview_start_requested && openxr_preview && wglGetCurrentContext();
    if(selected) try {
        if(!native_vr) native_vr=new NativeVr;
        if(native_vr->camera_owner && native_vr->camera_owner!=owner) throw std::runtime_error("Gameplay camera owner changed");
        if(native_vr->current.id) native_vr->finish();
        native_vr->camera_owner=owner;
        native_vr->early_frame=frame.load();native_vr->frame_culls=0;native_vr->cull_failed=false;
        native_vr->base_cameras_valid=false;
        native_vr->early_renderable=native_vr->begin();
        const bool turn_allowed=locomotion_requested&&!arm_swing_requested&&!gestures_suspended&&
            native_vr->early_renderable&&native_vr->submitted>0&&
            (!independent_native_eyes||native_vr->resources_ready_logged)&&
            walking_first_person(owner)&&!native_vr->t_pose_hold.holding()&&!native_vr->calibration_guided&&
            !native_vr->settings_menu.snapshot().visible&&!native_vr->keypad.snapshot().visible&&
            body_yaw_enabled&&native_vr->look_yaw.locked()&&swvr::native_locomotion_allows_turn();
        const double joystick_turn=native_vr->hand_turn.update(native_vr->current,active_gesture_settings,turn_allowed);
        native_vr->frame_turn=joystick_turn;
        // Body yaw follows the headset while walking in first person. Only the
        // native look yaw field is written; the XR view does not depend on it.
        native_vr->wrote_yaw=false;
        const bool has_settled=std::exchange(native_vr->has_settled_yaw,false);
        if(body_yaw_enabled && native_vr->early_renderable && native_vr->has_previous_base && walking_first_person(owner)) {
            ++native_vr->body_yaw_frames;
            // head_anchors[0] already carries the accumulated mouse turn.
            const auto center=swvr::stereo_center(native_vr->relative_eyes);
            const auto headset=swvr::offset_game_camera(native_vr->head_anchors[0],center);
            auto* field=reinterpret_cast<float*>(static_cast<unsigned char*>(owner)+0x2a0);
            // The game adds mouse turning to the field between updates; keep it
            // and turn the play space instead of pulling the body back.
            const auto step=swvr::follow_headset_yaw_turning(*field,
                has_settled?std::optional<float>(native_vr->settled_yaw):std::nullopt,
                native_vr->look_yaw,headset,native_vr->base_render,.0175,joystick_turn);
            if(step.turn!=0) {
                native_vr->turn_yaw=swvr::wrap_pi(native_vr->turn_yaw+step.turn);
                native_vr->turn_total+=std::abs(step.turn);
            }
            const float next=step.field;
            if(next!=*field) {
                native_vr->body_yaw_total+=std::abs(swvr::wrap_pi(static_cast<double>(next)-*field));
                *field=next;++native_vr->body_yaw_writes;
            }
            native_vr->written_yaw=*field;native_vr->wrote_yaw=native_vr->look_yaw.locked();
        } else if(!walking_first_person(owner)) native_vr->turn_yaw=0; // Seats/other cameras use their own frame.
        // 71A140 overwrites previous look at its end. Snapshot it before the
        // original call so source camera1 keeps its own temporal role.
        std::memcpy(native_vr->previous_look.data(),static_cast<const unsigned char*>(owner)+0x108,sizeof(swvr::Matrix4d));
    } catch(const std::exception& error) {xr_note("stereo_preview_error",error.what());stop_native_xr("early_frame_exception");}
    // Do not modify source transforms. The fourth source can be stale; culling
    // gets new planes from the current interpolated camera instead of adding
    // head offsets repeatedly to that conditional source.
    real_camera_owner_update(owner,graphics,alpha,fourth);
    if(selected && native_vr && native_vr->early_renderable) try {
        const auto bytes=static_cast<const unsigned char*>(owner);
        native_vr->base_render=native_transform(bytes+0x328);
        native_vr->base_cull=native_transform(bytes+0xaa8);
        std::memcpy(native_vr->current_look.data(),bytes+0x220,sizeof(swvr::Matrix4d));
        auto& settings=native_vr->head_settings;
        settings.mode_bits=(bytes[0x1a0]?swvr::head_mode_1a0:0)|(bytes[0x1a4]?swvr::head_mode_1a4:0)|
            (bytes[0x1a5]?swvr::head_mode_1a5:0)|(bytes[0x1a6]?swvr::head_mode_1a6:0);
        std::memcpy(&settings.backoff_distance,bytes+0x214,sizeof(settings.backoff_distance));
        std::memcpy(settings.world_shift.data(),bytes+0x1e0,sizeof(settings.world_shift));
        native_vr->head_anchors[0]=swvr::make_head_anchor(native_vr->base_render,native_vr->current_look,settings);
        native_vr->head_anchors[1]=swvr::make_head_anchor(native_transform(bytes+0x828),native_vr->previous_look,settings);
        native_vr->head_anchors[2]=swvr::make_head_anchor(native_transform(bytes+0x5a8),native_vr->current_look,settings);
        native_vr->base_cameras_valid=true;native_vr->has_previous_base=true;
        if(body_yaw_enabled && walking_first_person(owner) && native_vr->wrote_yaw) {
            // Whatever the original update added after our write is mouse/key
            // turning: it rotates the whole play space (view and body) instead
            // of being fought by the headset follow.
            float field{};std::memcpy(&field,bytes+0x2a0,sizeof(field));
            const double mouse=swvr::wrap_pi(static_cast<double>(field)-native_vr->written_yaw);
            native_vr->turn_yaw=swvr::wrap_pi(native_vr->turn_yaw+native_vr->look_yaw.sign()*mouse);
            native_vr->turn_total+=std::abs(mouse);
        }
        if(body_yaw_enabled && native_vr->turn_yaw!=0)
            for(auto& anchor:native_vr->head_anchors)anchor=swvr::rotate_game_yaw(anchor,native_vr->turn_yaw);
        if(body_yaw_enabled && walking_first_person(owner)) {
            float field{};std::memcpy(&field,bytes+0x2a0,sizeof(field));
            const bool was_locked=native_vr->look_yaw.locked();
            native_vr->look_yaw.observe(field,native_vr->current_look);
            native_vr->settled_yaw=field;native_vr->has_settled_yaw=std::isfinite(field);
            if(!was_locked && native_vr->look_yaw.locked()) {
                const auto note="locked sign="+std::to_string(native_vr->look_yaw.sign())+" offset="+std::to_string(native_vr->look_yaw.offset());
                xr_note("body_yaw_calibration",note.c_str());
            }
        }
        auto interaction_frame=native_vr->current;
        // Do not expose a pointing ray before any world image has reached XR,
        // or while newly configured native targets are still pending.
        interaction_frame.should_render=interaction_frame.should_render && native_vr->submitted>0 &&
            (!independent_native_eyes || native_vr->resources_ready_logged);
        const bool modal=native_vr->keypad.snapshot().visible||native_vr->settings_menu.snapshot().visible||gestures_suspended;
        const bool walking_allowed=locomotion_requested&&walking_first_person(owner)&&!native_vr->t_pose_hold.holding()&&!native_vr->calibration_guided&&!modal;
        const auto gesture_now=NativeVr::Clock::now();
        hand_steering.publish(interaction_frame,active_gesture_settings,swvr::native_steering_seat(),!modal&&!native_vr->calibration_guided&&!native_vr->t_pose_hold.holding(),gesture_now);
        const auto steering_command=hand_steering.inspect();
        if(locomotion_requested||record_hands_requested) {
            hand_locomotion.publish(interaction_frame,walking_allowed,gesture_now);
            const auto movement=hand_locomotion.inspect();
            const bool using_fists=hand_locomotion.suppresses_hand_actions();
            // Each joystick grip reserves its own hand. Never reinterpret
            // movement/turning fists as cockpit Q/E.
            auto reserved=hand_locomotion.reserved_hand_actions();
            if(native_vr->hand_turn.using_hand())reserved[1u-static_cast<unsigned>(active_gesture_settings.input.joystick_hand)]=true;
            for(unsigned h=0;h<2;++h)if(reserved[h])interaction_frame.hands[h].active=false;
            const float turn_rate=native_vr->hand_turn.rate();
            if(movement.active||using_fists||turn_rate!=0)native_vr->hand_overlay.status_frame(movement.sprint?std::array<float,3>{1.f,.5f,.1f}:std::array<float,3>{.15f,.6f,1.f},std::chrono::milliseconds(100));
            if(gesture_now>=native_vr->calibration_feedback_until&&gesture_now>=native_vr->control_feedback_until&&!modal)
                native_vr->hand_overlay.status_text(gesture_change_failed?"ZMIANA NIEUDANA - RUCH WSTRZYMANY":turn_rate!=0&&!movement.forward&&!movement.sideways?(turn_rate>0?"OBROT W PRAWO":"OBROT W LEWO"):hand_locomotion.status_text(),std::chrono::milliseconds(100));
        }
        for(unsigned h=0;h<2;++h)if(steering_command.hands[h])interaction_frame.hands[h].active=false;
        if(steering_command.active&&!modal)native_vr->hand_overlay.status_text("KIEROWNICA - OTWORZ DLON ABY PUSCIC",std::chrono::milliseconds(100));
        if(modal){interaction_frame.hands[0].active=false;interaction_frame.hands[1].active=false;}
        flush_locomotion_journal(native_vr->hand_recording);
        hand_interactions.publish(interaction_frame,
            swvr::HandCalibration{*native_vr->origin,native_vr->current.space_revision},native_vr->view_origin,native_vr->head_anchors[0]);
        if(native_vr->current.id<3 || native_vr->current.id%6==0) {
            LARGE_INTEGER now{};QueryPerformanceCounter(&now);
            auto& log=native_vr->camera_samples;
            log<<now.QuadPart<<'\t'<<frame.load()<<'\t'<<native_vr->current.id;
            for(const auto& m:{native_vr->base_render,native_vr->head_anchors[0],native_vr->current_look})
                for(const auto value:m) log<<'\t'<<value;
            log<<'\t'<<static_cast<unsigned>(settings.mode_bits)<<'\t'<<settings.backoff_distance;
            for(const auto value:settings.world_shift) log<<'\t'<<value;
            const auto& origin=*native_vr->origin;
            log<<'\t'<<origin.orientation.x<<'\t'<<origin.orientation.y<<'\t'<<origin.orientation.z<<'\t'<<origin.orientation.w
               <<'\t'<<origin.position.x<<'\t'<<origin.position.y<<'\t'<<origin.position.z;
            const auto hand=hand_interactions.inspect();
            log<<'\t'<<hand.sequence<<'\t'<<hand.valid_ray<<'\t'<<swvr::hand_interaction_status_name(hand.status);
            for(const auto value:hand.ray.origin) log<<'\t'<<value;
            for(const auto value:hand.ray.direction) log<<'\t'<<value;
            log<<'\t'<<hand.pinch_gesture_id<<'\t'<<hand.pinch_down<<'\n';
        }
        if(native_vr->current.id%60==0) native_vr->camera_samples.flush();
    } catch(const std::exception& error) {xr_note("stereo_preview_error",error.what());stop_native_xr("camera_snapshot_exception");}
}
void frustum(void* output_planes,const void* camera,float aspect) {
    real_frustum(output_planes,camera,aspect);
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-executable_base;
    if(caller!=0x6d3977 || !main_visibility_scope || !gameplay_preview || !native_vr || !native_vr->early_renderable ||
       !native_vr->base_cameras_valid || native_vr->early_frame!=frame.load()) return;
    try {
        const auto cull=native_transform(camera);
        // The measured prepass copies the fourth camera and only translates
        // it by the floating origin. Rebase the INTERPOLATED render camera by
        // the same delta, retaining the difference between endpoint cameras.
        for(int i=0;i<12;++i) if(std::abs(cull[i]-native_vr->base_cull[i])>1e-6)
            throw std::runtime_error("Culling camera basis changed unexpectedly");
        auto base=native_vr->head_anchors[0];
        for(int i=12;i<15;++i) base[i]+=cull[i]-native_vr->base_cull[i];
        float near_z{},far_z{};const auto bytes=static_cast<const unsigned char*>(camera);
        std::memcpy(&near_z,bytes+0x26c,sizeof(near_z));std::memcpy(&far_z,bytes+0x270,sizeof(far_z));
        const auto planes=swvr::make_stereo_cull_planes(base,native_vr->relative_eyes,
            {native_vr->current.eyes[0].fov,native_vr->current.eyes[1].fov},near_z,far_z);
        std::memcpy(output_planes,planes.data(),sizeof(planes));
        ++native_vr->cull_updates;++native_vr->frame_culls;
    } catch(const std::exception& error) {
        native_vr->cull_failed=true;xr_note("stereo_culling_error",error.what());
    }
}
void visibility(void* renderer,void* graphics,void* scene,void* camera) {
    const auto saved=main_visibility_scope;
    // Keep the host's passes unchanged, including auxiliary scenes. Apply XR
    // planes only in our bounded pass immediately around the main rendering,
    // where the scene lifetime is known, and restore its native flags afterwards.
    main_visibility_scope=false;
    native_visibility(renderer,graphics,scene,camera);
    main_visibility_scope=saved;
}
void rebuild_visibility(void* renderer,void* graphics,void* scene,void* owner,bool stereo) {
    if(!native_visibility || !owner) return;
    std::array<unsigned char,8> counters{};
    auto stats=static_cast<unsigned char*>(scene)+0x8d8;
    std::memcpy(counters.data(),stats,counters.size());
    const auto saved=main_visibility_scope;main_visibility_scope=stereo;
    native_visibility(renderer,graphics,scene,static_cast<unsigned char*>(owner)+0xaa8);
    main_visibility_scope=saved;
    std::memcpy(stats,counters.data(),counters.size());
}
void render(void* renderer,void* graphics,void* scene,void* commands,void* targets,
            void* camera0,void* camera1,void* camera2) {
    static thread_local bool inside{};
    if(inside) {real_renderer(renderer,graphics,scene,commands,targets,camera0,camera1,camera2);return;}
    inside=true;
    struct Leave {bool& inside;~Leave(){inside=false;}} leave{inside};
    const auto caller=_ReturnAddress();
    const auto caller_rva=reinterpret_cast<std::uintptr_t>(caller)-executable_base;
    record("render_begin",reinterpret_cast<std::intptr_t>(renderer),reinterpret_cast<std::intptr_t>(scene),
           reinterpret_cast<std::intptr_t>(targets),caller);
    const auto selected_rva=gameplay_preview?0x7b09acULL:0x84077bULL;
    const bool selected=caller_rva==selected_rva;
    if(gameplay_preview && caller_rva==0x84077b && !native_vr) gameplay_armed=false;
    if(selected && gameplay_preview && openxr_preview && !native_vr) {
        // Camera-owner updates can run during loading. Start XR on the next
        // owner update only after an actual main-world render has been seen.
        if(!gameplay_armed) xr_note("openxr_gameplay","armed_after_first_world_render");
        gameplay_armed=true;
        real_renderer(renderer,graphics,scene,commands,targets,camera0,camera1,camera2);
        record("render_end",0,0,0,caller);return;
    }
    const auto counter=reinterpret_cast<const unsigned*>(executable_base+swvr::game_profile::render_counter_rva);
    const auto invocation=*counter;
    const bool initializing=*(static_cast<const unsigned char*>(renderer)+0x2bf0)!=0;
    if(native_vr && !selected && caller_rva==(gameplay_preview?0x84077bULL:0x7b09acULL)) stop_native_xr("scene_changed");
    if(native_vr && selected) {
        // The two periodic maintenance frames deliberately remain mono. They
        // still count as menu frames for the lifecycle watchdog.
        native_vr->last_menu_frame=frame.load();
        if(GetTickCount64()-native_vr->started>=preview_duration_ms) {
            const auto owner=gameplay_preview?native_vr->camera_owner:nullptr;
            stop_native_xr("duration_elapsed");
            rebuild_visibility(renderer,graphics,scene,owner,false);
        }
    }
    if(!stereo_preview || !selected || !preview_start_requested || !wglGetCurrentContext() || initializing ||
       invocation%120==0 || invocation%120==119) {
        if(selected && gameplay_preview && native_vr && native_vr->current.id) {
            const auto owner=native_vr->camera_owner;
            try {native_vr->finish();} catch(...) {stop_native_xr("empty_frame_exception");}
            rebuild_visibility(renderer,graphics,scene,owner,false);
        }
        real_renderer(renderer,graphics,scene,commands,targets,camera0,camera1,camera2);
        record("render_end",0,0,0,caller);return;
    }
    try {
        if(!swvr::native_target_has_stereo_extent({static_cast<const unsigned char*>(targets),swvr::native_target_package_size})) {
            // The native renderer can briefly use 1x1 targets. Keep the
            // existing eye packages and submit an empty XR frame instead of
            // rebuilding them or splitting a one-pixel desktop viewport.
            if(native_vr) {
                if(!native_vr->target_suspended) {
                    native_vr->target_suspended=true;
                    xr_note("openxr_target_extent","suspended_tiny_source");
                }
                native_vr->reset_history();hand_interactions.reset();hand_locomotion.reset();
                native_vr->finish();
                if(gameplay_preview)rebuild_visibility(renderer,graphics,scene,native_vr->camera_owner,false);
            }
            real_renderer(renderer,graphics,scene,commands,targets,camera0,camera1,camera2);
            record("render_end",0,0,0,caller);return;
        }
        if(native_vr && native_vr->target_suspended) {
            native_vr->target_suspended=false;
            xr_note("openxr_target_extent","resumed_drawable_source");
        }
        bool xr_render{};
        if(openxr_preview) {
            if(!native_vr) {
                if(gameplay_preview) throw std::runtime_error("Gameplay renderer has no early XR frame");
                native_vr=new NativeVr;
            }
            if(independent_native_eyes && !native_vr->resources.created()) native_vr->create_resources(targets);
            xr_render=gameplay_preview?(native_vr->early_frame==frame.load() && native_vr->early_renderable):native_vr->begin();
            if(!native_vr->resources_ready(targets)) xr_render=false;
            if(!xr_render) {
                native_vr->finish();
                if(gameplay_preview) rebuild_visibility(renderer,graphics,scene,native_vr->camera_owner,false);
                real_renderer(renderer,graphics,scene,commands,targets,camera0,camera1,camera2);
                record("render_end",0,0,0,caller);return;
            }
            if(gameplay_preview) {
                if(!native_vr->base_cameras_valid) throw std::runtime_error("Gameplay base camera unavailable");
                if(!native_vr->frame_culls) {
                    // This function only recomputes visibility flags/counters
                    // in prepared lists. Keep the host's statistics if its own
                    // prepass was disabled; do not advance simulation.
                    rebuild_visibility(renderer,graphics,scene,native_vr->camera_owner,true);
                    ++native_vr->forced_culls;
                }
                if(native_vr->cull_failed || !native_vr->frame_culls) throw std::runtime_error("Stereo culling was not applied");
            }
        }
        using CameraBytes=std::array<unsigned char,swvr::game_profile::camera_size>;
        alignas(16) std::array<std::array<CameraBytes,3>,2> copies{};
        const std::array<void*,3> cameras{camera0,camera1,camera2};
        std::array<std::array<swvr::Fov,3>,2> lenses{};
        if(xr_render) for(unsigned eye=0;eye<2;++eye) for(unsigned camera=0;camera<3;++camera)
            lenses[eye][camera]=(gameplay_preview && camera==1 && native_vr->previous_eyes)?
                native_vr->previous_fovs[eye]:native_vr->current.eyes[eye].fov;
        float aspect{};
        std::memcpy(&aspect,static_cast<unsigned char*>(graphics)+0x900,sizeof(aspect));
        for(unsigned eye=0;eye<2;++eye) for(unsigned camera=0;camera<3;++camera) {
            if(!cameras[camera]) throw std::runtime_error("Null game camera");
            std::uintptr_t vtable{};std::memcpy(&vtable,cameras[camera],sizeof(vtable));
            if(vtable!=executable_base+swvr::game_profile::camera_vtable_rva)
                throw std::runtime_error("Unexpected game camera class");
            auto& copy=copies[eye][camera];std::memcpy(copy.data(),cameras[camera],copy.size());
            swvr::Matrix4d transform{};
            std::memcpy(transform.data(),copy.data()+swvr::game_profile::transform_offset,sizeof(transform));
            if(xr_render && gameplay_preview) transform=native_vr->head_anchors[camera];
            const auto eye_pose=xr_render?((gameplay_preview && camera==1 && native_vr->previous_eyes)?
                (*native_vr->previous_eyes)[eye]:native_vr->relative_eyes[eye]):swvr::Pose{{},{eye==0?-.032f:.032f,0,0}};
            transform=swvr::offset_game_camera(transform,eye_pose);
            std::memcpy(copy.data()+swvr::game_profile::transform_offset,transform.data(),sizeof(transform));
            real_camera_update(copy.data(),aspect); // Refresh every native cache, including double matrices.
            if(xr_render) swvr::apply_game_eye_projection(copy,lenses[eye][camera]);
        }
        static swvr::GlEyeTargets images;
        std::array<swvr::PixelRect,2> rectangles{};
        std::array<GLint,2> framebuffer{};
        std::vector<swvr::AvatarPaletteBinding> avatar_bindings;
        if(avatar_enabled && xr_render && gameplay_preview && avatar_identity)avatar_bindings=avatar_identity->palettes(scene);
        auto wheel_seat=wheel_enabled&&xr_render&&gameplay_preview?swvr::native_driver_seat(swvr::native_steering_seat()):swvr::DriverSeatSnapshot{};
        if(xr_render&&!native_vr->current.focused)wheel_seat={};
        const auto wheel_command=hand_steering.inspect();
        for(unsigned eye=0;eye<2;++eye) {
            active_eye_fov=xr_render?lenses[eye].data():nullptr;
            eye_projection_failed=false;eye_projection_updates=0;
            const auto render_time=NativeVr::Clock::now();
            const auto eye_targets=(xr_render && independent_native_eyes)?native_vr->resources.package(eye):targets;
            if(avatar_enabled && xr_render && gameplay_preview && native_vr->origin && avatar_identity) {
                const auto identity=avatar_identity->snapshot();
                auto avatar_targets=swvr::make_avatar_targets(native_vr->current,native_vr->view_origin,native_vr->head_anchors[0]);
                avatar_targets.actor=identity.valid?identity.actor:0;
                if(native_vr->avatar_body&&(native_vr->avatar_body->actor!=avatar_targets.actor||native_vr->avatar_body->variant!=identity.variant))native_vr->avatar_body.reset();
                if(!native_vr->avatar_body&&native_vr->stored_body&&avatar_targets.actor&&native_vr->stored_body->variant==identity.variant){
                    native_vr->avatar_body=swvr::bind_avatar_profile(*native_vr->stored_body,identity.variant,avatar_targets.actor,
                        native_vr->current.space_revision,++native_vr->avatar_calibration_generation);
                    if(native_vr->avatar_body)xr_note("avatar_profile","bound_to_live_local_actor");
                }
                if(eye==0&&!native_vr->keypad.snapshot().visible&&!native_vr->settings_menu.snapshot().visible) {
                    // Calibrate on the explicit signal, or automatically once a valid
                    // T-pose has been held for 1.5 s (once per hold). The hand
                    // skeletons flash green/red so the result is visible in the headset.
                    const bool requested=avatar_calibrate_request&&WaitForSingleObject(avatar_calibrate_request,0)==WAIT_OBJECT_0;
                    const char* issue=swvr::avatar_body_calibration_issue(avatar_targets,swvr::avatar_auto_t_pose_min_span);
                    const auto now=NativeVr::Clock::now();
                    auto& vr=*native_vr;
                    const bool was_holding=vr.t_pose_hold.holding();
                    if(issue)++vr.t_pose_issues[issue];else ++vr.t_pose_valid_frames;
                    const auto measurement=!issue&&vr.current.focused?swvr::calibrate_avatar_body(avatar_targets,1,identity.variant):std::nullopt;
                    const auto tracked_head=swvr::stereo_center({vr.current.eyes[0].pose,vr.current.eyes[1].pose}).position;
                    const bool stable_sample=vr.calibration_window.observe(measurement,tracked_head,now);
                    const auto hold=vr.t_pose_hold.update(stable_sample,
                        {avatar_targets.actor,vr.current.space_revision,identity.variant},now);
                    const bool automatic=hold.request;
                    if(hold.holding||was_holding||requested||vr.calibration_guided){
                        vr.calibration_feedback_until=now+std::chrono::milliseconds(1000);
                        vr.hand_overlay.status_text(vr.current.focused?(!issue&&!stable_sample?"TRZYMAJ RECE I GLOWE STABILNIE":swvr::avatar_calibration_hint(avatar_targets,issue)):"KALIBRACJA - BRAK FOKUSU VR",std::chrono::milliseconds(1000));
                        vr.hand_overlay.progress(hold.progress,std::chrono::milliseconds(1000));
                    }
                    // Yellow frame in the centre of view while a valid T-pose is being held.
                    if(hold.holding)vr.hand_overlay.status_frame({1.f,.85f,.1f},std::chrono::milliseconds(150));
                    if(requested||automatic) {
                        if(locomotion_requested)hand_locomotion.reset();
                        const auto stable=vr.current.focused&&stable_sample?vr.calibration_window.result(++native_vr->avatar_calibration_generation):std::nullopt;
                        const auto body=stable?std::optional<swvr::AvatarBodyCalibration>(stable->body):std::nullopt;
                        if(body) {
                            native_vr->avatar_body=body;native_vr->t_pose_hold.accepted();++native_vr->t_pose_accepted;
                            native_vr->head_recenter_pending=true;
                            native_vr->calibrated_head_position=stable->head_position;
                            native_vr->calibration_guided=false;
                            native_vr->hand_overlay.flash({.15f,1.f,.25f},std::chrono::milliseconds(1500));
                            native_vr->hand_overlay.status_frame({.15f,1.f,.25f},std::chrono::milliseconds(1500));
                            vr.hand_overlay.status_text("KALIBRACJA PRZYJETA",std::chrono::milliseconds(1500));
                            vr.hand_overlay.progress(1.f,std::chrono::milliseconds(1500));vr.calibration_feedback_until=now+std::chrono::milliseconds(1500);
                            vr.stored_body=swvr::make_avatar_profile(*body);
                            if(!avatar_profile_path.empty()&&!profile_staging_path.empty())try{
                                swvr::save_avatar_profile(avatar_profile_path,*vr.stored_body,profile_staging_path);xr_note("avatar_profile","saved");
                            }catch(const std::exception& e){xr_note("avatar_profile_error",e.what());}
                            const auto note=std::string("accepted ")+(automatic?"auto":"signal")+" wrist_span="+std::to_string(body->wrist_span)+" generation="+std::to_string(body->generation);
                            xr_note("avatar_calibration",note.c_str());
                        } else if(requested) {
                            native_vr->hand_overlay.flash({1.f,.15f,.1f},std::chrono::milliseconds(1000));
                            native_vr->hand_overlay.status_frame({1.f,.15f,.1f},std::chrono::milliseconds(1000));
                            xr_note("avatar_calibration",(std::string("rejected ")+(issue?issue:"unknown")).c_str());
                        } else {
                            vr.hand_overlay.status_text("USTABILIZUJ RECE I GLOWE",std::chrono::milliseconds(1000));
                        }
                    }
                }
                avatar_targets.body=native_vr->avatar_body;
                avatar_pose.begin_eye(std::move(avatar_targets),identity,native_transform(copies[eye][0].data()),avatar_bindings);
            }
            if(wheel_enabled&&xr_render)driver_wheel.begin_eye(wheel_seat,wheel_command,native_transform(copies[eye][0].data()),native_vr->current.id);
            real_renderer(renderer,graphics,scene,commands,eye_targets,copies[eye][0].data(),copies[eye][1].data(),copies[eye][2].data());
            avatar_pose.end_eye();driver_wheel.end_eye();
            if(xr_render){const auto milliseconds=NativeVr::elapsed(render_time);native_vr->render_ms+=milliseconds;native_vr->performance.render_eye.observe(milliseconds,native_vr->current.id);}
            active_eye_fov=nullptr;
            apply_head_mask(false);
            if(eye_projection_failed) throw std::runtime_error("Native eye projection could not be applied");
            if(xr_render && eye_projection_updates!=3) throw std::runtime_error("Expected three native eye projection updates");
            GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(0x8CA6,&framebuffer[eye]);
            rectangles[eye]={viewport[0],viewport[1],viewport[2],viewport[3]};
            const auto capture_time=NativeVr::Clock::now();
            if(eye==0) images.prepare(viewport[2],viewport[3]);
            images.capture(eye,rectangles[eye]);
            if(xr_render) native_vr->capture_ms+=NativeVr::elapsed(capture_time);
            record("stereo_eye",eye,framebuffer[eye],viewport[2],caller,nullptr,
                   reinterpret_cast<const float*>(copies[eye][0].data()+swvr::game_profile::view_offset));
        }
        if(framebuffer[0]!=framebuffer[1] || rectangles[0].width!=rectangles[1].width ||
           rectangles[0].height!=rectangles[1].height) throw std::runtime_error("Eye outputs differ in dimensions or target");
        const auto r=rectangles[1];
        if(xr_render) native_vr->finish(&images);
        if(xr_render && gameplay_preview) rebuild_visibility(renderer,graphics,scene,native_vr->camera_owner,false);
        record("stereo_invocations",invocation,*counter,0,caller);
        images.blit_to_bound_draw(0,{r.x,r.y,r.width/2,r.height});
        images.blit_to_bound_draw(1,{r.x+r.width/2,r.y,r.width-r.width/2,r.height});
        // Periodic downscaled left-eye snapshots during XR (every 30 s, at most 10).
        if(xr_render && native_vr) try {
            static unsigned snapshots{};
            const auto due=native_vr->started+30000ull*(snapshots+1);
            if(snapshots<10 && GetTickCount64()>=due) {
                const auto reduced=swvr::downscale_rgb(images.read_rgb(0),r.width,r.height,2);
                write_eye_image(diagnostic_base.wstring()+L".eye-"+std::to_wstring(++snapshots)+L".ppm",reduced.pixels,reduced.width,reduced.height);
                xr_note("eye_snapshot",("index="+std::to_string(snapshots)+" size="+std::to_string(reduced.width)+"x"+std::to_string(reduced.height)).c_str());
            }
        } catch(const std::exception& error) {xr_note("eye_snapshot_error",error.what());}
        static unsigned frames{};
        if(++frames==10) {
            write_eye_image(diagnostic_base.wstring()+L".left.ppm",images.read_rgb(0),r.width,r.height);
            write_eye_image(diagnostic_base.wstring()+L".right.ppm",images.read_rgb(1),r.width,r.height);
            std::lock_guard lock(log_mutex);
            if(output) {fprintf(output,"#stereo_preview\ttwo_native_views_saved\n");fflush(output);}
        }
    } catch(const std::exception& error) {
        active_eye_fov=nullptr;
        avatar_pose.end_eye();driver_wheel.end_eye();
        apply_head_mask(false);
        xr_note("stereo_preview_error",error.what());
        const auto owner=(gameplay_preview && native_vr)?native_vr->camera_owner:nullptr;
        stop_native_xr("exception");
        rebuild_visibility(renderer,graphics,scene,owner,false);
        stereo_preview=false;
        {
            std::lock_guard lock(log_mutex);
            if(output) {fprintf(output,"#stereo_preview_error\t%s\n",error.what());fflush(output);}
        }
        real_renderer(renderer,graphics,scene,commands,targets,camera0,camera1,camera2);
    }
    record("render_end",0,0,0,caller);
}
template<class T> PROC resolve(PROC address,T& original,T wrapper) {
    if(!address || address==reinterpret_cast<PROC>(1) || address==reinterpret_cast<PROC>(2) ||
       address==reinterpret_cast<PROC>(3) || address==reinterpret_cast<PROC>(-1)) return address;
    // Only substitute addresses from the first driver dispatch table. If another
    // context has a different implementation, preserve its pointer unchanged.
    std::lock_guard lock(resolve_mutex);
    if(!original) original=reinterpret_cast<T>(address);
    if(reinterpret_cast<PROC>(original)!=address) return address;
    return reinterpret_cast<PROC>(wrapper);
}
PROC WINAPI get(LPCSTR name) {
    const auto p=real_get(name);
    if(!name) return p;
    if(!strcmp(name,"glUseProgram")) return resolve<UseProgram>(p,real_use,use);
    if(!strcmp(name,"glBindFramebuffer")) return resolve<BindFramebuffer>(p,real_bind,bind);
    if(!strcmp(name,"glGetUniformLocation")) return resolve<UniformLocation>(p,real_location,location);
    if(!strcmp(name,"glUniformMatrix4fv")) return resolve<UniformMatrix>(p,real_matrix,matrix);
    if(!strcmp(name,"glShaderSource")) return resolve<ShaderSource>(p,real_shader_source,shader_source);
    if(!strcmp(name,"glCompileShader")) return resolve<ShaderOperation>(p,real_compile_shader,compile_shader);
    if(!strcmp(name,"glLinkProgram")) return resolve<ShaderOperation>(p,real_link_program,link_program);
    if(!strcmp(name,"glDeleteShader")) return resolve<ShaderOperation>(p,real_delete_shader,delete_shader);
    if(!strcmp(name,"glUniform1i")) return resolve<UniformInt>(p,real_uniform_int,uniform_int);
    if(!strcmp(name,"glUniform1f")) return resolve<UniformFloat>(p,real_uniform_float,uniform_float);
    return p;
}
void begin_capture() noexcept {
    try {
        std::lock_guard lock(log_mutex);
        if(!output) return;
        ++capture_epoch;events=0;written_events=0;
        // Keep previous captures in the same file; frame numbers never reset.
        fprintf(output,"#capture_epoch\t%llu\n#capture_start_frame\t%llu\n#state\tcapturing\n",
                capture_epoch.load(),frame.load());
        fprintf(output,"#uniform_cache_full\t%d\n",uniform_cache_full?1:0);
        // Shaders can have been created while the previous capture was capped.
        // Seed the names so the next matrix upload remains interpretable.
        for(const auto& [key,name]:known_uniforms) {
            const auto [context,program,location]=key;
            fprintf(output,"%llu\t%lu\t%p\tuniform\t%u\t0\t%u\t%d\t0\t%p\t%s\n",
                    frame.load(),GetCurrentThreadId(),reinterpret_cast<void*>(context),program,
                    program,location,static_cast<void*>(nullptr),name.c_str());
            ++events;++written_events;
        }
        fflush(output);
    } catch(...) {}
}
BOOL WINAPI swap(HDC dc) {
    if(preview_start_request && WaitForSingleObject(preview_start_request,0)==WAIT_OBJECT_0) {
        preview_start_requested=true;xr_note("openxr_control","start_requested");
    }
    if(gameplay_preview && native_vr && native_vr->current.id) {
        try {native_vr->finish();} catch(...) {stop_native_xr("unrendered_frame_exception");}
    }
    // If a loading/UI-only frame stops calling the measured menu renderer,
    // close while its owning context is still current instead of starving XR.
    if(native_vr && frame.load()>native_vr->last_menu_frame+1) stop_native_xr("menu_renderer_absent");
    if(native_vr && GetTickCount64()-native_vr->started>=preview_duration_ms) stop_native_xr("duration_elapsed");
    if(native_vr && preview_stop_request && WaitForSingleObject(preview_stop_request,0)==WAIT_OBJECT_0) {
        xr_note("openxr_control","stop_requested");stop_native_xr("user_stop");
    }
    poll_hand_button_shutdown();
    if(locomotion_cleanup_pending) {
        const auto state=swvr::native_locomotion_stats();
        if(!state.holding||state.faults||GetTickCount64()-locomotion_cleanup_started>=5000) {
            locomotion_cleanup_pending=false;
            const auto totals="packets="+std::to_string(state.packets)+" walk="+std::to_string(state.walk)+
                " sprint="+std::to_string(state.sprint)+" releases="+std::to_string(state.releases)+
                " holding="+std::to_string(state.holding)+" faults="+std::to_string(state.faults)+
                " lifecycle_releases="+std::to_string(state.lifecycle_releases)+" steering_packets="+std::to_string(state.steering_packets)+" steering_releases="+std::to_string(state.steering_releases)+
                " steering_nonzero_packets="+std::to_string(state.steering_nonzero_packets)+" steering_max_axis="+std::to_string(state.steering_max_axis)+" last_fault="+state.last_fault;
            xr_note("locomotion_totals",totals.c_str());
            if(hand_locomotion.arm_swing_stats().frames) {
                const auto swing=hand_locomotion.arm_swing_stats();
                const auto report="frames="+std::to_string(swing.frames)+" invalid_tracking="+std::to_string(swing.invalid_tracking)+
                    " fists_open="+std::to_string(swing.fists_open)+" outside_zone="+std::to_string(swing.outside_zone)+
                    " opposed_samples="+std::to_string(swing.opposed_samples)+" rejected_motion="+std::to_string(swing.rejected_motion)+
                    " rigid_turns="+std::to_string(swing.rigid_turns)+
                    " half_strokes="+std::to_string(swing.half_strokes)+" starts="+std::to_string(swing.starts)+" stops="+std::to_string(swing.stops);
                xr_note("arm_swing_totals",report.c_str());
            }
            if(state.holding||state.faults)xr_note("openxr_error","Locomotion did not release in a verified live input scope");
            else xr_note("locomotion_drain","complete");
        }
    }
    if(capture_request && WaitForSingleObject(capture_request,0)==WAIT_OBJECT_0) {
        begin_capture();
        if(hand_ray_active && swvr::rearm_native_hand_ray_log())
            xr_note("hand_ray_log","rearmed_by_capture_request");
        if(input_probe_active && swvr::rearm_native_input_probe_log())
            xr_note("input_probe_log","rearmed_by_capture_request");
    }
    record("swap",0,0,0,_ReturnAddress());++frame;return real_swap(dc);
}
swvr::GlAvatarPose::Draw avatar_draw() noexcept {
    if(avatar_enabled)try {return avatar_pose.before_draw();}catch(const std::exception& error) {
        avatar_failed(error.what());
    }
    return {};
}
swvr::GlDriverWheel::Draw wheel_draw() noexcept {
    if(wheel_enabled)try{return driver_wheel.before_draw();}catch(const std::exception& error){wheel_failed(error.what());}
    return {};
}
void APIENTRY elements(GLenum mode,GLsizei count,GLenum type,const void* indices) {
    apply_head_mask(head_eye_active());
    auto avatar=avatar_draw();
    auto wheel=wheel_draw();
    record("draw_elements",mode,count,type,_ReturnAddress());real_elements(mode,count,type,indices);
}
void APIENTRY arrays(GLenum mode,GLint first,GLsizei count) {
    apply_head_mask(head_eye_active());
    auto avatar=avatar_draw();
    auto wheel=wheel_draw();
    record("draw_arrays",mode,first,count,_ReturnAddress());real_arrays(mode,first,count);
}
void APIENTRY viewport(GLint x,GLint y,GLsizei width,GLsizei height) {
    real_viewport(x,y,width,height);record("viewport",width,height,0,_ReturnAddress());
}
template<class T> bool hook(T original,T replacement,T* trampoline) {
    return MH_CreateHook(reinterpret_cast<void*>(original),reinterpret_cast<void*>(replacement),
                          reinterpret_cast<void**>(trampoline))==MH_OK;
}
}
extern "C" __declspec(dllexport) DWORD WINAPI swvr_trace_start(void* log_path) {
    // Caller supplies an absolute writable log filename; no defaults in the game directory.
    if(ready || !log_path) return 1;
    output=_wfsopen(static_cast<const wchar_t*>(log_path),L"wb",_SH_DENYWR);
    if(!output) return 2;
    fprintf(output,"frame\tthread\tcontext\tevent\tprogram\tfbo\ta\tb\tc\tcaller\tdata\n");
    fprintf(output,"#exe_base\t%p\n#event_limit\t%llu\n",GetModuleHandleW(nullptr),limit);
    fprintf(output,"#process_id\t%lu\n#capture_epoch\t0\n",GetCurrentProcessId());
    const auto base=reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    executable_base=reinterpret_cast<std::uintptr_t>(base);
    diagnostic_base=static_cast<const wchar_t*>(log_path);
    wchar_t manual_option[8]{};
    preview_start_requested=!(GetEnvironmentVariableW(L"SWVR_XR_MANUAL_START",manual_option,8)>0 && !wcscmp(manual_option,L"1"));
    wchar_t duration_option[16]{};
    if(GetEnvironmentVariableW(L"SWVR_XR_SECONDS",duration_option,16)>0) {
        wchar_t* end{};const auto seconds=wcstoul(duration_option,&end,10);
        if(end && !*end && seconds>=10 && seconds<=3600) preview_duration_ms=seconds*1000ULL;
    }
    wchar_t trace_option[8]{};
    collect_events=!(GetEnvironmentVariableW(L"SWVR_TRACE_EVENTS",trace_option,8)>0 && !wcscmp(trace_option,L"0"));
    fprintf(output,"#trace_events\t%s\n",collect_events?"enabled":"disabled_for_timing");
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const auto hash=image_sha256();
    fprintf(output,"#exe_sha256\t%s\n#exe_timestamp\t%lu\n#exe_size\t%lu\n",
            hash.c_str(),nt->FileHeader.TimeDateStamp,nt->OptionalHeader.SizeOfImage);
    if(MH_Initialize()!=MH_OK) {fclose(output);output=nullptr;return 3;}
    if(!hook(&wglGetProcAddress,get,&real_get) || !hook(&SwapBuffers,swap,&real_swap) ||
       !hook(&glDrawElements,elements,&real_elements) || !hook(&glDrawArrays,arrays,&real_arrays) ||
       !hook(&glViewport,viewport,&real_viewport) || !hook(&wglDeleteContext,delete_context,&real_delete_context)) {
        MH_Uninitialize();fclose(output);output=nullptr;return 4;
    }
    wchar_t head_option[8]{},test_option[8]{},host_path[32768]{};
    GetModuleFileNameW(nullptr,host_path,32768);
    head_mask_fixture=GetEnvironmentVariableW(L"SWVR_HEAD_MASK_TEST",test_option,8)>0 && !wcscmp(test_option,L"1") &&
        std::filesystem::path(host_path).filename()==L"swvr_head_mask_injection_tests.exe";
    head_mask_requested=head_mask_fixture || (hash==swvr::game_profile::sha256 &&
        GetEnvironmentVariableW(L"SWVR_HEAD_MASK",head_option,8)>0 && !wcscmp(head_option,L"1"));
    if(head_mask_requested) {head_mask.configure(original_gl_proc);head_mask_enabled=true;}
    fprintf(output,"#head_mask_mode\t%s\n",head_mask_fixture?"isolated_test_host":head_mask_enabled?"vr_local_head":"off");
    if(hash==swvr::game_profile::sha256) {
        wchar_t scene_option[24]{};
        gameplay_preview=GetEnvironmentVariableW(L"SWVR_XR_SCENE",scene_option,24)>0 && !wcscmp(scene_option,L"gameplay");
        wchar_t limit_option[8]{};
        head_limit_enabled=!(GetEnvironmentVariableW(L"SWVR_HEAD_LIMIT",limit_option,8)>0 && !wcscmp(limit_option,L"0"));
        if(gameplay_preview) fprintf(output,"#head_limit_mode\t%s\n",head_limit_enabled?"neck_soft_ellipsoid":"off");
        wchar_t yaw_option[8]{};
        body_yaw_enabled=gameplay_preview && GetEnvironmentVariableW(L"SWVR_BODY_YAW",yaw_option,8)>0 && !wcscmp(yaw_option,L"1");
        if(gameplay_preview) fprintf(output,"#body_yaw_mode\t%s\n",body_yaw_enabled?"follow_headset_walking":"off");
        wchar_t locomotion_option[8]{};
        locomotion_requested=gameplay_preview && GetEnvironmentVariableW(L"SWVR_HAND_LOCOMOTION",locomotion_option,8)==1 && locomotion_option[0]==L'1';
        wchar_t locomotion_mode[32]{};
        const auto mode_length=GetEnvironmentVariableW(L"SWVR_LOCOMOTION_MODE",locomotion_mode,32);
        arm_swing_requested=mode_length<32 && !wcscmp(locomotion_mode,L"arm_swing");
        locomotion_mode_valid=mode_length==0||arm_swing_requested||(mode_length<32&&!wcscmp(locomotion_mode,L"joystick"));
        hand_locomotion.set_mode(arm_swing_requested?swvr::LocomotionMode::arm_swing:swvr::LocomotionMode::joystick);
        wchar_t extra_option[8]{};
        record_hands_requested=gameplay_preview&&GetEnvironmentVariableW(L"SWVR_RECORD_HANDS",extra_option,8)==1&&extra_option[0]==L'1';
        keypad_requested=gameplay_preview&&GetEnvironmentVariableW(L"SWVR_KEYPAD_PROTOTYPE",extra_option,8)==1&&extra_option[0]==L'1';
        if(gameplay_preview) {
            try {
                wchar_t settings_path[32768]{};
                const auto size=GetEnvironmentVariableW(L"SWVR_GESTURE_CONFIG",settings_path,32768);
                if(size>=32768)throw std::invalid_argument("Gesture configuration path too long");
                const auto settings=size?swvr::load_gesture_settings(settings_path):swvr::GestureSettings{};
                hand_locomotion.configure(settings);
                active_gesture_settings=settings;if(size)gesture_config_path=settings_path;
                hand_interactions.set_priority(static_cast<swvr::InteractionHand>(settings.input.pointing_priority));
                xr_note("gesture_config",size?std::filesystem::path(settings_path).string().c_str():"defaults");
                xr_note("gesture_settings",swvr::describe_gesture_settings(settings).c_str());
            }catch(const std::exception& e){locomotion_mode_valid=false;xr_note("gesture_config_error",e.what());}
        }
        if(gameplay_preview) {
            using namespace swvr::game_profile;
            const auto matches=[&](std::uintptr_t rva,const auto& prefix) {
                return std::memcmp(base+rva,prefix.data(),prefix.size())==0;
            };
            const auto owner_function=reinterpret_cast<CameraOwnerUpdate>(const_cast<unsigned char*>(base+camera_owner_update_rva));
            if(matches(camera_owner_update_rva,camera_owner_update_prefix) && matches(frustum_rva,frustum_prefix) &&
               matches(visibility_rva,visibility_prefix) && hook(owner_function,camera_owner_update,&real_camera_owner_update)) {
                const auto frustum_function=reinterpret_cast<Frustum>(const_cast<unsigned char*>(base+frustum_rva));
                if(hook(frustum_function,frustum,&real_frustum)) {
                    if(!hook(reinterpret_cast<Visibility>(const_cast<unsigned char*>(base+visibility_rva)),visibility,&native_visibility)) {
                        MH_RemoveHook(reinterpret_cast<void*>(frustum_function));real_frustum=nullptr;
                    }
                }
                if(!native_visibility) {MH_RemoveHook(reinterpret_cast<void*>(owner_function));real_camera_owner_update=nullptr;}
            }
            gameplay_profile_failed=!real_camera_owner_update || !real_frustum || !native_visibility;
            fprintf(output,"#gameplay_camera_probe\t%s\n",gameplay_profile_failed?"signature_or_hook_failure":"enabled");
        }
        wchar_t resource_option[8]{};
        independent_native_eyes=GetEnvironmentVariableW(L"SWVR_NATIVE_EYES",resource_option,8)>0 && !wcscmp(resource_option,L"1");
        if(independent_native_eyes) {
            using namespace swvr::game_profile;
            const auto matches=[&](std::uintptr_t rva,const auto& prefix) {
                return std::memcmp(base+rva,prefix.data(),prefix.size())==0;
            };
            if(matches(package_initialize_rva,package_initialize_prefix) && matches(package_resize_rva,package_resize_prefix) &&
               matches(package_destroy_rva,package_destroy_prefix) && matches(resource_release_rva,resource_release_prefix) &&
               hook(reinterpret_cast<swvr::NativePackageInitialize>(const_cast<unsigned char*>(base+package_initialize_rva)),
                    package_initialize,&resource_api.initialize)) {
                resource_api.resize=reinterpret_cast<swvr::NativePackageResize>(const_cast<unsigned char*>(base+package_resize_rva));
                resource_api.release=reinterpret_cast<swvr::NativeResourceRelease>(const_cast<unsigned char*>(base+resource_release_rva));
                resource_api.destroy_containers=reinterpret_cast<swvr::NativePackageDestroy>(const_cast<unsigned char*>(base+package_destroy_rva));
                fprintf(output,"#native_eye_resources_probe\tenabled\n");
            } else {
                independent_native_eyes=false;
                resource_profile_failed=true;
                fprintf(output,"#native_eye_resources_probe\tsignature_or_hook_failure\n");
            }
        }
        const auto address=base+swvr::game_profile::camera_update_rva;
        const auto& prefix=swvr::game_profile::camera_update_prefix;
        if(std::memcmp(address,prefix.data(),prefix.size())==0 &&
           hook(reinterpret_cast<CameraUpdate>(const_cast<unsigned char*>(address)),camera_update,&real_camera_update))
            fprintf(output,"#camera_probe\tenabled\n");
        else fprintf(output,"#camera_probe\tsignature_or_hook_failure\n");
        const auto renderer=base+swvr::game_profile::renderer_rva;
        const auto& renderer_prefix=swvr::game_profile::renderer_prefix;
        if(real_camera_update && std::memcmp(renderer,renderer_prefix.data(),renderer_prefix.size())==0 &&
           hook(reinterpret_cast<Renderer>(const_cast<unsigned char*>(renderer)),render,&real_renderer)) {
            wchar_t option[8]{};
            stereo_preview=GetEnvironmentVariableW(L"SWVR_STEREO_PREVIEW",option,8)>0 && !wcscmp(option,L"1");
            openxr_preview=GetEnvironmentVariableW(L"SWVR_OPENXR_PREVIEW",option,8)>0 && !wcscmp(option,L"1");
            if(openxr_preview) {
                wchar_t loader[32768]{};
                const auto length=GetEnvironmentVariableW(L"SWVR_OPENXR_LOADER",loader,32768);
                if(length && length<32768 && std::filesystem::path(loader).is_absolute()) {
                    xr_loader=loader;stereo_preview=true;
                } else {
                    openxr_preview=false;
                    fprintf(output,"#openxr_error\tabsolute_SWVR_OPENXR_LOADER_required\n");
                }
            }
            if(openxr_preview && (resource_profile_failed || gameplay_profile_failed)) {
                openxr_preview=false;stereo_preview=false;
                xr_note("openxr_error","Requested native resource or gameplay camera profile guard failed");
            }
            if(gameplay_preview && !openxr_preview) stereo_preview=false;
            fprintf(output,"#openxr_preview_mode\t%s\n#preview_duration_ms\t%llu\n",
                openxr_preview?(gameplay_preview?"experimental_gameplay":"experimental_menu"):"off",preview_duration_ms);
            fprintf(output,"#renderer_probe\tenabled\n#stereo_preview_mode\t%s\n",stereo_preview?(gameplay_preview?"experimental_gameplay":"experimental_menu"):"off");
        } else fprintf(output,"#renderer_probe\tsignature_or_hook_failure\n");
    } else fprintf(output,"#camera_probe\tunsupported_executable\n");
    wchar_t avatar_option[8]{};
    avatar_requested=GetEnvironmentVariableW(L"SWVR_AVATAR_POSE",avatar_option,8)==1 && avatar_option[0]==L'1';
    if(avatar_requested)try{
        avatar_profile_path=configured_path(L"SWVR_AVATAR_PROFILE");profile_staging_path=configured_path(L"SWVR_PROFILE_STAGING");
        if(!avatar_profile_path.empty()&&profile_staging_path.empty())throw std::invalid_argument("Missing absolute profile staging directory");
    }catch(const std::exception& e){avatar_profile_path.clear();profile_staging_path.clear();xr_note("avatar_profile_error",e.what());}
    if(avatar_requested)try {
        if(hash!=swvr::game_profile::sha256 || !gameplay_preview || !openxr_preview)
            throw std::runtime_error("Avatar pose requires verified Gameplay OpenXR");
        std::array<swvr::AvatarRig,2> rigs;
        const auto folder=std::filesystem::path(host_path).parent_path()/L"rom"/L"meshes";
        const std::array<const wchar_t*,2> files{L"survivor_female.anim",L"survivor_male.anim"};
        for(std::size_t i=0;i<2;++i) {
            std::ifstream input(folder/files[i],std::ios::binary);
            if(!input)throw std::runtime_error("Avatar animation asset not found");
            if(std::filesystem::file_size(folder/files[i])>16*1024*1024)throw std::runtime_error("Avatar animation asset exceeds supported size");
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});
            rigs[i]=swvr::read_avatar_rig(bytes);
        }
        avatar_identity=std::make_unique<swvr::NativeAvatarIdentity>(executable_base,true);
        avatar_pose.configure(original_gl_proc,std::move(rigs));avatar_pose.set_journal(120,20000);avatar_enabled=true;
        xr_note("avatar_pose","configured_survivor_head_arms");
    }catch(const std::exception& error) {avatar_enabled=false;xr_note("openxr_error",error.what());}
    const auto retirement_status=swvr::install_native_button_retirement(executable_base,hash==swvr::game_profile::sha256,diagnostic_base);
    xr_note("button_retirement",swvr::native_button_retirement_status_name(retirement_status));
    const bool retirement_failed=retirement_status!=swvr::NativeButtonRetirementStatus::disabled &&
        retirement_status!=swvr::NativeButtonRetirementStatus::installed && retirement_status!=swvr::NativeButtonRetirementStatus::already_installed;
    const auto buttons_status=retirement_failed?swvr::NativeHandButtonsStatus::bindings_failure:
        swvr::install_native_hand_buttons(executable_base,hash==swvr::game_profile::sha256,diagnostic_base,hand_interactions);
    xr_note("hand_buttons",swvr::native_hand_buttons_status_name(buttons_status));
    if(buttons_status==swvr::NativeHandButtonsStatus::installed || buttons_status==swvr::NativeHandButtonsStatus::already_installed)
        hand_buttons_active=true;
    else if(buttons_status!=swvr::NativeHandButtonsStatus::disabled) {
        openxr_preview=false;stereo_preview=false;
        xr_note("openxr_error","Requested hand-button backend could not be installed");
    }
    if(hand_buttons_active){
        const bool keypad_installed=swvr::install_native_vehicle_keypad(executable_base,hash==swvr::game_profile::sha256);
        swvr::enable_native_keypad_opening(keypad_installed);xr_note("vehicle_keypad",keypad_installed?"hooks_created":"installation_failed");
    }
    if(locomotion_requested) {
        xr_note("locomotion_mode",arm_swing_requested?"arm_swing":"joystick");
        const bool installed=locomotion_mode_valid&&body_yaw_enabled&&swvr::install_native_locomotion(
            executable_base,hash==swvr::game_profile::sha256,hand_locomotion,hand_buttons_active);
        const bool observing=installed&&(!hand_buttons_active||swvr::set_native_hand_lifecycle_callbacks(swvr::native_locomotion_lifecycle_callbacks()));
        if(observing){
            xr_note("locomotion","hooks_created");const bool steering_ready=swvr::enable_native_steering(hand_steering);
            xr_note("hand_steering",steering_ready?"ready_axis_1":"installation_failed");
            if(steering_ready)try{
                auto mesh=swvr::load_driver_seat_mesh(std::filesystem::path(host_path).parent_path()/L"rom"/L"meshes"/L"component_seat_racing.mesh");
                if(!swvr::install_native_driver_seat(executable_base,hash==swvr::game_profile::sha256))throw std::runtime_error("Driver seat observer could not be installed");
                driver_wheel.configure(original_gl_proc,std::move(mesh.points));wheel_requested=true;wheel_enabled=true;
                xr_note("driver_wheel","ready_local_driver_seat_geometry");
            }catch(const std::exception& error){xr_note("driver_wheel_error",error.what());}
        }
        else {openxr_preview=false;stereo_preview=false;xr_note("openxr_error","Requested locomotion backend could not be installed");}
    }
    const auto hand_ray_status=swvr::install_native_hand_ray(executable_base,hash==swvr::game_profile::sha256,diagnostic_base,
        hand_interactions,avatar_enabled?swvr::NativeHandRayCallbacks{nullptr,avatar_selection}:
            hand_buttons_active?swvr::native_hand_buttons_callbacks():swvr::NativeHandRayCallbacks{});
    fprintf(output,"#hand_ray_backend\t%s\n",swvr::native_hand_ray_status_name(hand_ray_status));
    if(hand_ray_status==swvr::NativeHandRayStatus::disabled && !hand_buttons_active) {
        const auto interaction_status=swvr::install_interaction_probe(executable_base,hash==swvr::game_profile::sha256,diagnostic_base);
        fprintf(output,"#interaction_probe\t%s\n",swvr::interaction_probe_status_name(interaction_status));
    } else if(hand_ray_status==swvr::NativeHandRayStatus::installed || hand_ray_status==swvr::NativeHandRayStatus::already_installed) {
        hand_ray_active=true;
        fprintf(output,"#interaction_probe\tskipped_shared_hooks_owned_by_hand_ray\n");
    } else {
        openxr_preview=false;stereo_preview=false;
        xr_note("openxr_error","Requested hand-ray backend could not be installed");
    }
    if(hand_buttons_active && (!hand_ray_active || !gameplay_preview)) {
        openxr_preview=false;stereo_preview=false;
        xr_note("openxr_error","Hand buttons require the gameplay hand-ray path");
    }
    if(avatar_requested && (!avatar_enabled || !hand_ray_active)) {
        openxr_preview=false;stereo_preview=false;
        xr_note("openxr_error","Avatar pose requires a configured rig and native Selection observer");
    }
    const auto input_probe_status=hand_buttons_active?swvr::NativeInputProbeStatus::disabled:
        swvr::install_native_input_probe(executable_base,hash==swvr::game_profile::sha256,diagnostic_base);
    xr_note("input_probe",swvr::native_input_probe_status_name(input_probe_status));
    if(input_probe_status==swvr::NativeInputProbeStatus::installed || input_probe_status==swvr::NativeInputProbeStatus::already_installed)
        input_probe_active=true;
    else if(input_probe_status!=swvr::NativeInputProbeStatus::disabled) {
        openxr_preview=false;stereo_preview=false;
        xr_note("openxr_error","Requested input probe could not be installed");
    }
    const auto event_name=L"Local\\StormworksVR.Capture."+std::to_wstring(GetCurrentProcessId());
    capture_request=CreateEventW(nullptr,FALSE,FALSE,event_name.c_str());
    if(avatar_enabled){
        const auto calibration_name=L"Local\\StormworksVR.AvatarCalibrate."+std::to_wstring(GetCurrentProcessId());
        avatar_calibrate_request=CreateEventW(nullptr,FALSE,FALSE,calibration_name.c_str());
        const auto clear_name=L"Local\\StormworksVR.AvatarProfileClear."+std::to_wstring(GetCurrentProcessId());
        avatar_profile_clear_request=CreateEventW(nullptr,FALSE,FALSE,clear_name.c_str());
        if(!avatar_calibrate_request){avatar_failed("Cannot create avatar calibration event");openxr_preview=false;stereo_preview=false;}
    }
    if(locomotion_requested){
        const auto make_control=[](const wchar_t* suffix){return CreateEventW(nullptr,FALSE,FALSE,(std::wstring(L"Local\\StormworksVR.")+suffix+L"."+std::to_wstring(GetCurrentProcessId())).c_str());};
        gesture_reload_request=make_control(L"GestureReload");joystick_mode_request=make_control(L"ModeJoystick");arm_mode_request=make_control(L"ModeArmSwing");
        xr_note("gesture_reload_control",gesture_reload_request&&joystick_mode_request&&arm_mode_request?"named_events":"unavailable");
    }
    if(keypad_requested){const auto name=L"Local\\StormworksVR.Keypad."+std::to_wstring(GetCurrentProcessId());keypad_request=CreateEventW(nullptr,FALSE,FALSE,name.c_str());xr_note("keypad_prototype",keypad_request?"ready_local_only":"control_unavailable");}
    if(!preview_start_requested) {
        const auto start_name=L"Local\\StormworksVR.Start."+std::to_wstring(GetCurrentProcessId());
        preview_start_request=CreateEventW(nullptr,FALSE,FALSE,start_name.c_str());
        fprintf(output,"#preview_start_control\t%s\n",preview_start_request?"named_event":"unavailable");
    }
    const auto stop_name=L"Local\\StormworksVR.Stop."+std::to_wstring(GetCurrentProcessId());
    preview_stop_request=CreateEventW(nullptr,FALSE,FALSE,stop_name.c_str());
    fprintf(output,"#preview_stop_control\t%s\n",preview_stop_request?"named_event":"unavailable");
    fprintf(output,"#capture_control\t%s\n",capture_request?"named_event":"unavailable");
    ready=true;
    if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK) {
        ready=false;MH_Uninitialize();fclose(output);output=nullptr;
        if(capture_request) {CloseHandle(capture_request);capture_request=nullptr;} return 5;
    }
    {
        std::lock_guard lock(log_mutex);
        fprintf(output,"#state\tinitialized\n");
        fflush(output); // Preserve startup evidence even if the host exits before its first swap.
    }
    return 0;
}
extern "C" __declspec(dllexport) DWORD WINAPI swvr_trace_flush(void*) {
    swvr::flush_interaction_probe();
    swvr::flush_native_hand_ray();
    swvr::flush_native_input_probe();
    swvr::flush_native_hand_buttons();
    swvr::flush_native_button_retirement();
    std::lock_guard lock(log_mutex);
    if(output) fflush(output);
    return static_cast<DWORD>(events.load());
}
extern "C" __declspec(dllexport) DWORD WINAPI swvr_trace_test_head_mask(void* enabled) {
    if(!head_mask_fixture) return 0;
    fixture_eye_draw=enabled!=nullptr;apply_head_mask(fixture_eye_draw);return 1;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
