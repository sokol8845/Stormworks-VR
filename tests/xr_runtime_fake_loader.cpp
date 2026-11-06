// Deterministic OpenXR protocol double. No HMD/runtime installation is used.
#include <windows.h>
#include <GL/gl.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "xr_runtime_fake.hpp"
#include <cstring>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {
FakeXrState state;
bool acquired[2]{}, waited[2]{};
std::array<GLuint,2> gpu_images{};
std::array<XrView,2> located_views{};
std::filesystem::path report_path;
using BindBuffer=void(APIENTRY*)(GLenum,GLuint);
BindBuffer bind_buffer{};
std::wstring environment(const wchar_t* name) {
    const DWORD count=GetEnvironmentVariableW(name,nullptr,0);
    if(!count)return {};
    std::wstring value(count,L'\0');
    const auto length=GetEnvironmentVariableW(name,value.data(),count);
    if(!length || length>=count)return {};
    value.resize(length);return value;
}
int dimension(const wchar_t* name) {
    const auto value=environment(name);if(value.empty())return 0;
    wchar_t* end{};const long result=std::wcstol(value.c_str(),&end,10);
    if(end!=value.c_str()+value.size() || result<16 || result>4096)return -1;
    return static_cast<int>(result);
}
void write_report() noexcept {
    if(!state.gpu_mode || report_path.empty())return;
    FILE* out{};if(_wfopen_s(&out,report_path.c_str(),L"wb") || !out)return;
    std::fprintf(out,"{\n  \"fake_openxr\": true,\n  \"headset_test\": false,\n"
        "  \"width\": %d, \"height\": %d,\n  \"projection_frames\": %d,\n"
        "  \"valid_projection_frames\": %d,\n  \"empty_frames\": %d,\n  \"validation_errors\": %d,\n"
        "  \"first_projection_readback\": %s,\n  \"different_eye_pixels\": %s,\n"
        "  \"nonblack_rgb\": [%s, %s],\n  \"rgb_fnv1a64\": [\"%016llx\", \"%016llx\"],\n"
        "  \"diagnostic_hand_pixels\": [[%u, %u], [%u, %u]],\n"
        "  \"created_swapchains\": %d, \"destroyed_swapchains\": %d,\n"
        "  \"acquired_images\": %d, \"released_images\": %d,\n  \"invalid_releases\": %d\n}\n",
        state.gpu_width,state.gpu_height,state.projections,state.gpu_valid_projections,state.empty_frames,state.gpu_errors,
        state.gpu_readback?"true":"false",state.gpu_different_eyes?"true":"false",
        state.gpu_nonblack[0]?"true":"false",state.gpu_nonblack[1]?"true":"false",
        static_cast<unsigned long long>(state.gpu_checksum[0]),static_cast<unsigned long long>(state.gpu_checksum[1]),
        state.gpu_hand_pixels[0][0],state.gpu_hand_pixels[0][1],state.gpu_hand_pixels[1][0],state.gpu_hand_pixels[1][1],
        state.created_swapchains,state.destroyed_swapchains,state.acquired,state.released,state.invalid_releases);
    std::fclose(out);
}
bool first_projection_pixels() {
    if(state.gpu_readback)return true;
    if(!bind_buffer || !wglGetCurrentContext())return false;
    GLint texture{},pack{};glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(0x88ED,&pack);
    constexpr std::array<GLenum,6> fields{GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,
        GL_PACK_SKIP_PIXELS,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST};
    std::array<GLint,fields.size()> previous{};
    for(std::size_t i=0;i<fields.size();++i)glGetIntegerv(fields[i],&previous[i]);
    struct Restore {
        GLint texture,pack;const std::array<GLenum,6>& fields;const std::array<GLint,6>& previous;
        ~Restore(){for(std::size_t i=0;i<fields.size();++i)glPixelStorei(fields[i],previous[i]);
            bind_buffer(0x88EB,pack);glBindTexture(GL_TEXTURE_2D,texture);}
    } restore{texture,pack,fields,previous};
    bind_buffer(0x88EB,0);
    for(std::size_t i=0;i<fields.size();++i)glPixelStorei(fields[i],i==0?1:0);
    std::array<std::vector<unsigned char>,2> pixels;
    for(std::size_t i=0;i<2;++i) {
        pixels[i].resize(static_cast<std::size_t>(state.gpu_width)*state.gpu_height*3);
        glBindTexture(GL_TEXTURE_2D,gpu_images[i]);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_UNSIGNED_BYTE,pixels[i].data());
        std::uint64_t hash=14695981039346656037ULL;
        for(const auto channel:pixels[i]) {hash^=channel;hash*=1099511628211ULL;state.gpu_nonblack[i]|=channel!=0;}
        state.gpu_checksum[i]=hash;
        for(std::size_t offset=0;offset<pixels[i].size();offset+=3) {
            const auto r=pixels[i][offset],g=pixels[i][offset+1],b=pixels[i][offset+2];
            if(r<60 && g>225 && b>240) ++state.gpu_hand_pixels[i][0];
            if(r>240 && g>145 && g<190 && b<65) ++state.gpu_hand_pixels[i][1];
        }
        if(!report_path.empty()) {
            const auto path=report_path.wstring()+(i==0?L".left.ppm":L".right.ppm");
            FILE* image{};
            if(!_wfopen_s(&image,path.c_str(),L"wb") && image) {
                std::fprintf(image,"P6\n%d %d\n255\n",state.gpu_width,state.gpu_height);
                const auto stride=static_cast<std::size_t>(state.gpu_width)*3;
                for(int y=state.gpu_height-1;y>=0;--y) std::fwrite(pixels[i].data()+y*stride,1,stride,image);
                std::fclose(image);
            }
        }
    }
    state.gpu_different_eyes=pixels[0]!=pixels[1];state.gpu_readback=true;return true;
}
template<class T>T handle(std::uintptr_t id) {return reinterpret_cast<T>(id);}
int eye(XrSwapchain s) {return static_cast<int>(reinterpret_cast<std::uintptr_t>(s)-10);}
XrResult XRAPI_CALL enumerate_extensions(const char*,uint32_t capacity,uint32_t* count,XrExtensionProperties* out) {
    *count=state.hand_aim_extension?3:2;if(capacity) {
        strcpy_s(out[0].extensionName,XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
        strcpy_s(out[1].extensionName,XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        if(state.hand_aim_extension&&capacity>=3)strcpy_s(out[2].extensionName,XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
    }return XR_SUCCESS;
}
XrResult XRAPI_CALL create_instance(const XrInstanceCreateInfo* info,XrInstance* out) {
    try {
        for(uint32_t i=0;i<info->enabledExtensionCount;++i)if(std::strcmp(info->enabledExtensionNames[i],XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME)==0)state.hand_aim_enabled=true;
        if(environment(L"SWVR_FAKE_GL_IMAGES")==L"1") {
            if(gpu_images[0] || gpu_images[1])return XR_ERROR_CALL_ORDER_INVALID;
            state={};state.gpu_mode=true;acquired[0]=acquired[1]=waited[0]=waited[1]=false;
            const int width=dimension(L"SWVR_FAKE_WIDTH"),height=dimension(L"SWVR_FAKE_HEIGHT");
            if(width<0 || height<0)return XR_ERROR_VALIDATION_FAILURE;
            state.gpu_width=width?width:640;state.gpu_height=height?height:480;
            report_path=environment(L"SWVR_FAKE_REPORT");
            if(!report_path.empty() && !report_path.is_absolute())return XR_ERROR_VALIDATION_FAILURE;
            const auto entry=wglGetProcAddress("glBindBuffer");
            if(!entry || entry==reinterpret_cast<PROC>(-1) || entry==reinterpret_cast<PROC>(1) ||
               entry==reinterpret_cast<PROC>(2) || entry==reinterpret_cast<PROC>(3))return XR_ERROR_GRAPHICS_DEVICE_INVALID;
            bind_buffer=reinterpret_cast<BindBuffer>(entry);
        }
        *out=handle<XrInstance>(1);return XR_SUCCESS;
    }catch(...){return XR_ERROR_RUNTIME_FAILURE;}
}
XrResult XRAPI_CALL destroy_instance(XrInstance) {++state.destroyed_instances;write_report();return XR_SUCCESS;}
XrResult XRAPI_CALL instance_properties(XrInstance,XrInstanceProperties* out) {
    strcpy_s(out->runtimeName,state.gpu_mode?"SWVR FAKE GPU integration - NO HMD":"SWVR protocol double");return XR_SUCCESS;
}
XrResult XRAPI_CALL get_system(XrInstance,const XrSystemGetInfo*,XrSystemId* out) {*out=1;return XR_SUCCESS;}
XrResult XRAPI_CALL system_properties(XrInstance,XrSystemId,XrSystemProperties* out) {
    strcpy_s(out->systemName,"No headset");
    if(out->next) static_cast<XrSystemHandTrackingPropertiesEXT*>(out->next)->supportsHandTracking=XR_TRUE;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL graphics_requirements(XrInstance,XrSystemId,XrGraphicsRequirementsOpenGLKHR* out) {
    out->minApiVersionSupported=0;out->maxApiVersionSupported=XR_MAKE_VERSION(99,0,0);return XR_SUCCESS;
}
XrResult XRAPI_CALL create_session(XrInstance,const XrSessionCreateInfo*,XrSession* out) {*out=handle<XrSession>(2);return XR_SUCCESS;}
XrResult XRAPI_CALL destroy_session(XrSession) {++state.destroyed_sessions;return XR_SUCCESS;}
XrResult XRAPI_CALL create_space(XrSession,const XrReferenceSpaceCreateInfo*,XrSpace* out) {*out=handle<XrSpace>(3);return XR_SUCCESS;}
XrResult XRAPI_CALL destroy_space(XrSpace) {return XR_SUCCESS;}
XrResult XRAPI_CALL blend_modes(XrInstance,XrSystemId,XrViewConfigurationType,uint32_t capacity,uint32_t* count,XrEnvironmentBlendMode* out) {
    *count=1;if(capacity) *out=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;return XR_SUCCESS;
}
XrResult XRAPI_CALL create_hand(XrSession,const XrHandTrackerCreateInfoEXT* info,XrHandTrackerEXT* out) {
    *out=handle<XrHandTrackerEXT>(info->hand==XR_HAND_LEFT_EXT?20:21);return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_hand(XrHandTrackerEXT) {++state.destroyed_hands;return XR_SUCCESS;}
XrResult XRAPI_CALL config_views(XrInstance,XrSystemId,XrViewConfigurationType,uint32_t capacity,uint32_t* count,XrViewConfigurationView* out) {
    *count=2;if(capacity) for(int i=0;i<2;++i) {
        out[i].recommendedImageRectWidth=state.gpu_mode?state.gpu_width:32;
        out[i].recommendedImageRectHeight=state.gpu_mode?state.gpu_height:32;
    }return XR_SUCCESS;
}
XrResult XRAPI_CALL formats(XrSession,uint32_t capacity,uint32_t* count,int64_t* out) {
    *count=state.gpu_mode?2:1;if(capacity){out[0]=GL_RGBA8;if(state.gpu_mode && capacity>1)out[1]=0x8C43;}return XR_SUCCESS;
}
XrResult XRAPI_CALL create_swapchain(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out) {
    const int i=state.created_swapchains;if(i>=2)return XR_ERROR_LIMIT_REACHED;
    if(state.gpu_mode) {
        if(!wglGetCurrentContext() || !bind_buffer)return XR_ERROR_GRAPHICS_DEVICE_INVALID;
        if(info->sampleCount!=1 || info->arraySize!=1 || info->mipCount!=1 || info->faceCount!=1 ||
           info->width!=static_cast<uint32_t>(state.gpu_width) || info->height!=static_cast<uint32_t>(state.gpu_height) ||
           (info->format!=GL_RGBA8 && info->format!=0x8C43))return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
        GLint texture{},unpack{};glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(0x88EF,&unpack);
        bind_buffer(0x88EC,0);glGenTextures(1,&gpu_images[i]);glBindTexture(GL_TEXTURE_2D,gpu_images[i]);
        glTexImage2D(GL_TEXTURE_2D,0,static_cast<GLint>(info->format),state.gpu_width,state.gpu_height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812F);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812F);
        GLint width{};glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
        glBindTexture(GL_TEXTURE_2D,texture);bind_buffer(0x88EC,unpack);
        if(!gpu_images[i] || width!=state.gpu_width){if(gpu_images[i])glDeleteTextures(1,&gpu_images[i]);gpu_images[i]=0;return XR_ERROR_RUNTIME_FAILURE;}
    }
    *out=handle<XrSwapchain>(10+state.created_swapchains++);return XR_SUCCESS;
}
XrResult XRAPI_CALL destroy_swapchain(XrSwapchain s) {
    const int i=eye(s);if(i<0 || i>=2)return XR_ERROR_HANDLE_INVALID;
    if(state.gpu_mode && gpu_images[i]){glDeleteTextures(1,&gpu_images[i]);gpu_images[i]=0;}
    ++state.destroyed_swapchains;return XR_SUCCESS;
}
XrResult XRAPI_CALL images(XrSwapchain s,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out) {
    *count=1;if(capacity) reinterpret_cast<XrSwapchainImageOpenGLKHR*>(out)->image=state.gpu_mode?gpu_images[eye(s)]:static_cast<GLuint>(100+eye(s));return XR_SUCCESS;
}
XrResult XRAPI_CALL poll(XrInstance,XrEventDataBuffer* out) {
    int event{};
    if(state.initial_events<2) event=++state.initial_events==1?XR_SESSION_STATE_READY:XR_SESSION_STATE_FOCUSED;
    else if(state.pending_event) {event=state.pending_event;state.pending_event=0;}
    else return XR_EVENT_UNAVAILABLE;
    if(event==100) {
        XrEventDataReferenceSpaceChangePending change{XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING};
        change.session=handle<XrSession>(2);change.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;change.changeTime=state.display_time;
        std::memcpy(out,&change,sizeof(change));
    } else {
        XrEventDataSessionStateChanged change{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
        change.session=handle<XrSession>(2);change.state=static_cast<XrSessionState>(event);std::memcpy(out,&change,sizeof(change));
    }return XR_SUCCESS;
}
XrResult XRAPI_CALL begin_session(XrSession,const XrSessionBeginInfo*) {++state.begin_sessions;return XR_SUCCESS;}
XrResult XRAPI_CALL end_session(XrSession) {++state.end_sessions;return XR_SUCCESS;}
XrResult XRAPI_CALL request_exit(XrSession) {state.pending_event=XR_SESSION_STATE_STOPPING;return XR_SUCCESS;}
XrResult XRAPI_CALL wait_frame(XrSession,const XrFrameWaitInfo*,XrFrameState* out) {
    ++state.wait_frames;out->predictedDisplayTime=++state.display_time;out->predictedDisplayPeriod=state.display_period;out->shouldRender=state.should_render;return XR_SUCCESS;
}
XrResult XRAPI_CALL begin_frame(XrSession,const XrFrameBeginInfo*) {++state.begin_frames;return XR_SUCCESS;}
XrResult XRAPI_CALL locate_views(XrSession,const XrViewLocateInfo* info,XrViewState* flags,uint32_t,uint32_t* count,XrView* out) {
    state.eye_time=info->displayTime;*count=2;flags->viewStateFlags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
    if(state.valid_pose)flags->viewStateFlags|=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_POSITION_TRACKED_BIT;
    for(int i=0;i<2;++i) {
        out[i].pose={};out[i].pose.orientation.w=1;out[i].pose.position.x=i==0?-0.032f:0.032f;out[i].fov={-0.7f,0.7f,0.7f,-0.7f};
        if(state.gpu_mode) {
            const float ipd=i==0?-.032f:.032f;
            out[i].pose.orientation={0,std::sin(.04f),0,std::cos(.04f)};
            out[i].pose.position={.15f+ipd*std::cos(.08f),1.6f,-.2f-ipd*std::sin(.08f)};
            out[i].fov=i==0?XrFovf{-.85f,.65f,.8f,-.7f}:XrFovf{-.65f,.85f,.75f,-.8f};
            located_views[i]=out[i];
        }
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL locate_hands(XrHandTrackerEXT hand,const XrHandJointsLocateInfoEXT* info,XrHandJointLocationsEXT* out) {
    state.hand_times[reinterpret_cast<std::uintptr_t>(hand)-20]=info->time;out->isActive=state.active_hands;
    if(out->next){auto* aim=static_cast<XrHandTrackingAimStateFB*>(out->next);if(aim->type!=XR_TYPE_HAND_TRACKING_AIM_STATE_FB||!state.hand_aim_enabled)return XR_ERROR_VALIDATION_FAILURE;
        aim->status=state.system_menu_hands[reinterpret_cast<std::uintptr_t>(hand)-20]?XR_HAND_TRACKING_AIM_SYSTEM_GESTURE_BIT_FB:0;}
    for(uint32_t i=0;i<out->jointCount;++i) {
        out->jointLocations[i].pose.orientation.w=1;out->jointLocations[i].pose.position.x=0.25f;
        out->jointLocations[i].locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        out->jointLocations[i].radius=0.01f;
        if(state.gpu_mode) {
            const float side=reinterpret_cast<std::uintptr_t>(hand)==20?-1.f:1.f;
            const float base_x=.15f+side*.22f;
            XrVector3f point{base_x,1.38f,-.7f};
            if(i==XR_HAND_JOINT_WRIST_EXT) point={base_x,1.31f,-.66f};
            else if(i>=XR_HAND_JOINT_THUMB_METACARPAL_EXT && i<=XR_HAND_JOINT_THUMB_TIP_EXT)
                point={base_x-side*(.025f+.014f*(i-2)),1.36f+.017f*(i-2),-.7f};
            else if(i>=XR_HAND_JOINT_INDEX_METACARPAL_EXT) {
                const auto finger=(i-6)/5,joint=(i-6)%5;
                point={base_x+side*(-.032f+.023f*finger),1.38f+.024f*joint,-.7f-.008f*joint};
            }
            out->jointLocations[i].pose.position=point;
        }
    }return XR_SUCCESS;
}
XrResult XRAPI_CALL acquire_image(XrSwapchain s,const XrSwapchainImageAcquireInfo*,uint32_t* out) {
    const int i=eye(s);if(i==1 && state.fail_right_acquire)return XR_ERROR_RUNTIME_FAILURE;
    acquired[i]=true;++state.acquired;*out=0;return XR_SUCCESS;
}
XrResult XRAPI_CALL wait_image(XrSwapchain s,const XrSwapchainImageWaitInfo*) {
    const int i=eye(s);if(i==1 && state.timeout_right_wait)return XR_TIMEOUT_EXPIRED;
    waited[i]=true;++state.waited;return XR_SUCCESS;
}
XrResult XRAPI_CALL release_image(XrSwapchain s,const XrSwapchainImageReleaseInfo*) {
    const int i=eye(s);if(!acquired[i] || !waited[i]) {++state.invalid_releases;return XR_ERROR_CALL_ORDER_INVALID;}
    acquired[i]=waited[i]=false;++state.released;return XR_SUCCESS;
}
XrResult XRAPI_CALL end_frame(XrSession,const XrFrameEndInfo* info) {
    if(info->layerCount)++state.projections;else ++state.empty_frames;
    if(info->layerCount && info->layers && info->layers[0]) {
        const auto* submitted=reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
        if(submitted->viewCount==2 && submitted->views)for(int i=0;i<2;++i){state.last_view_x[i]=submitted->views[i].pose.position.x;state.last_fov_left[i]=submitted->views[i].fov.angleLeft;}
    }
    if(!state.gpu_mode || !info->layerCount)return XR_SUCCESS;
    bool valid=info->layerCount==1 && info->layers && info->layers[0] && info->displayTime==state.eye_time;
    if(valid) {
        const auto* layer=reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
        valid=layer->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION && layer->space==handle<XrSpace>(3) && layer->viewCount==2 && layer->views;
        if(valid)for(int i=0;i<2;++i) {
            const auto& view=layer->views[i];
            valid=valid && view.type==XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW &&
                std::memcmp(&view.pose,&located_views[i].pose,sizeof(view.pose))==0 &&
                std::memcmp(&view.fov,&located_views[i].fov,sizeof(view.fov))==0 &&
                view.subImage.swapchain==handle<XrSwapchain>(10+i) && view.subImage.imageArrayIndex==0 &&
                view.subImage.imageRect.offset.x==0 && view.subImage.imageRect.offset.y==0 &&
                view.subImage.imageRect.extent.width==state.gpu_width && view.subImage.imageRect.extent.height==state.gpu_height &&
                gpu_images[i] && glIsTexture(gpu_images[i]) && !acquired[i] && !waited[i];
        }
    }
    if(valid)try {valid=first_projection_pixels();}catch(...){valid=false;}
    if(!valid){++state.gpu_errors;return XR_ERROR_VALIDATION_FAILURE;}
    ++state.gpu_valid_projections;return XR_SUCCESS;
}
}
extern "C" __declspec(dllexport) FakeXrState* swvr_test_reset() {state={};acquired[0]=acquired[1]=waited[0]=waited[1]=false;return &state;}
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance,const char* name,PFN_xrVoidFunction* out) {
#define ENTRY(api,fn) if(std::strcmp(name,#api)==0) {*out=reinterpret_cast<PFN_xrVoidFunction>(fn);return XR_SUCCESS;}
    ENTRY(xrEnumerateInstanceExtensionProperties,enumerate_extensions)
    ENTRY(xrCreateInstance,create_instance) ENTRY(xrDestroyInstance,destroy_instance) ENTRY(xrGetInstanceProperties,instance_properties)
    ENTRY(xrGetSystem,get_system) ENTRY(xrGetSystemProperties,system_properties) ENTRY(xrGetOpenGLGraphicsRequirementsKHR,graphics_requirements)
    ENTRY(xrCreateSession,create_session) ENTRY(xrDestroySession,destroy_session) ENTRY(xrCreateReferenceSpace,create_space) ENTRY(xrDestroySpace,destroy_space)
    ENTRY(xrEnumerateEnvironmentBlendModes,blend_modes) ENTRY(xrCreateHandTrackerEXT,create_hand) ENTRY(xrDestroyHandTrackerEXT,destroy_hand)
    ENTRY(xrEnumerateViewConfigurationViews,config_views) ENTRY(xrEnumerateSwapchainFormats,formats)
    ENTRY(xrCreateSwapchain,create_swapchain) ENTRY(xrDestroySwapchain,destroy_swapchain) ENTRY(xrEnumerateSwapchainImages,images)
    ENTRY(xrPollEvent,poll) ENTRY(xrBeginSession,begin_session) ENTRY(xrEndSession,end_session) ENTRY(xrRequestExitSession,request_exit)
    ENTRY(xrWaitFrame,wait_frame) ENTRY(xrBeginFrame,begin_frame) ENTRY(xrLocateViews,locate_views) ENTRY(xrLocateHandJointsEXT,locate_hands)
    ENTRY(xrAcquireSwapchainImage,acquire_image) ENTRY(xrWaitSwapchainImage,wait_image) ENTRY(xrReleaseSwapchainImage,release_image) ENTRY(xrEndFrame,end_frame)
#undef ENTRY
    *out=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;
}
