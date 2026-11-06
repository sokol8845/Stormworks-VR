#include "swvr/xr_runtime.hpp"
#include "swvr/hand_input_guard.hpp"
#include <windows.h>
#include <GL/gl.h>
#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_OPENGL
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace swvr {
static_assert(xr_thumb_tip==XR_HAND_JOINT_THUMB_TIP_EXT);
static_assert(xr_index_tip==XR_HAND_JOINT_INDEX_TIP_EXT);
static_assert(xr_index_proximal==XR_HAND_JOINT_INDEX_PROXIMAL_EXT);
static_assert(xr_hand_joint_count==XR_HAND_JOINT_COUNT_EXT);
namespace {
template<class T>T structure(XrStructureType type) {T value{};value.type=type;return value;}
void check(XrResult result,const char* operation) {
    if(XR_FAILED(result)) throw std::runtime_error(std::string(operation)+": XrResult "+std::to_string(result));
}
Pose convert(XrPosef p) {
    return {{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},
            {p.position.x,p.position.y,p.position.z}};
}
}
struct XrRuntime::Impl {
    HMODULE loader{};
    DWORD owner_thread{};
    HDC dc{};
    HGLRC context{};
    PFN_xrGetInstanceProcAddr get_proc{};
    XrInstance instance{};
    XrSession session{};
    XrSpace space{};
    XrRuntimeInfo information{};
    bool initialized{},running{},focused{},terminal{},exit_requested{},active_frame{};
    std::uint64_t sequence{},space_revision{};
    XrRuntimeFrame current_frame{};
    std::array<XrView,2> views{};
    std::array<XrHandTrackerEXT,2> hands{};
    std::vector<XrTime> pending_space_changes;
    struct Eye {
        XrSwapchain swapchain{};
        int width{},height{};
        std::vector<XrSwapchainImageOpenGLKHR> images;
        bool acquired{},waited{};
    };
    std::array<Eye,2> eyes{};
    template<class T>T function(const char* name) const {
        PFN_xrVoidFunction result{};
        if(!get_proc) throw std::logic_error("OpenXR loader is not initialized");
        check(get_proc(instance,name,&result),name);
        if(!result) throw std::runtime_error(std::string("Missing OpenXR entry point ")+name);
        return reinterpret_cast<T>(result);
    }
#define XR(name) function<PFN_##name>(#name)
    bool owner_current() const noexcept {
        return owner_thread==GetCurrentThreadId() && context &&
               wglGetCurrentContext()==context && wglGetCurrentDC()==dc;
    }
    void require_owner() const {
        if(!owner_current()) throw std::logic_error("OpenXR call requires its owner thread and current game GL context");
    }
    void require_initialized() const {
        require_owner();
        if(!initialized) throw std::logic_error("OpenXR session is not initialized");
    }
    void release_images() {
        // A failed wait is not a successful acquire-for-writing. Never release
        // an image for which xrWaitSwapchainImage has not succeeded.
        for(auto& eye:eyes) if(eye.acquired && eye.waited) {
            auto release=structure<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
            check(XR(xrReleaseSwapchainImage)(eye.swapchain,&release),"xrReleaseSwapchainImage");
            eye.acquired=eye.waited=false;
        }
    }
    void finish(std::array<bool,2> rendered,const XrViewOverride* override_views=nullptr) {
        const bool submit=current_frame.should_render && rendered[0] && rendered[1] &&
                          eyes[0].acquired && eyes[0].waited && eyes[1].acquired && eyes[1].waited;
        if(eyes[0].waited || eyes[1].waited) glFlush();
        release_images();
        std::array<XrCompositionLayerProjectionView,2> layer_views;
        layer_views.fill(structure<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW));
        for(std::size_t i=0;i<2;++i) {
            layer_views[i].pose=views[i].pose;layer_views[i].fov=views[i].fov;
            if(override_views) {
                const auto& p=override_views->poses[i];const auto& f=override_views->fov[i];
                layer_views[i].pose.orientation={p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w};
                layer_views[i].pose.position={p.position.x,p.position.y,p.position.z};
                layer_views[i].fov={f.left,f.right,f.up,f.down};
            }
            layer_views[i].subImage.swapchain=eyes[i].swapchain;
            layer_views[i].subImage.imageRect.extent={eyes[i].width,eyes[i].height};
        }
        auto layer=structure<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
        layer.space=space;layer.viewCount=2;layer.views=layer_views.data();
        const XrCompositionLayerBaseHeader* layers[]{reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
        auto end=structure<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
        end.displayTime=current_frame.predicted_display_time_ns;
        end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount=submit?1:0;end.layers=submit?layers:nullptr;
        const auto result=XR(xrEndFrame)(session,&end);
        active_frame=false;current_frame={};
        check(result,"xrEndFrame");
    }
    void cancel() noexcept {
        if(!active_frame || !owner_current()) return;
        try {finish({false,false});}
        catch(...) {terminal=true;focused=false;active_frame=false;current_frame={};}
    }
    void cleanup() noexcept {
        if(!owner_current()) return; // Do not touch a GL context owned by another thread.
        cancel();
        if(instance) {
            for(auto& hand:hands) if(hand) {
                try {XR(xrDestroyHandTrackerEXT)(hand);} catch(...) {} hand=XR_NULL_HANDLE;
            }
            for(auto& eye:eyes) if(eye.swapchain) {
                try {XR(xrDestroySwapchain)(eye.swapchain);} catch(...) {} eye={};
            }
            if(space) {try {XR(xrDestroySpace)(space);} catch(...) {} space=XR_NULL_HANDLE;}
            if(session) {try {XR(xrDestroySession)(session);} catch(...) {} session=XR_NULL_HANDLE;}
            try {XR(xrDestroyInstance)(instance);} catch(...) {} instance=XR_NULL_HANDLE;
        }
        if(loader) {FreeLibrary(loader);loader=nullptr;}
        get_proc=nullptr;initialized=running=focused=active_frame=false;
    }
    ~Impl() {cleanup();}
    void events() {
        for(;;) {
            auto event=structure<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
            const auto result=XR(xrPollEvent)(instance,&event);
            if(result==XR_EVENT_UNAVAILABLE) return;
            check(result,"xrPollEvent");
            if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                terminal=true;focused=false;return;
            }
            if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                const auto& change=*reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&event);
                if(change.session==session && change.referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL)
                    pending_space_changes.push_back(change.changeTime);
            }
            if(event.type!=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
            const auto& changed=*reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            if(changed.session!=session) continue;
            focused=changed.state==XR_SESSION_STATE_FOCUSED;
            if(changed.state==XR_SESSION_STATE_READY && !running) {
                auto begin=structure<XrSessionBeginInfo>(XR_TYPE_SESSION_BEGIN_INFO);
                begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                check(XR(xrBeginSession)(session,&begin),"xrBeginSession");running=true;
            } else if(changed.state==XR_SESSION_STATE_STOPPING && running) {
                check(XR(xrEndSession)(session),"xrEndSession");running=false;
            } else if(changed.state==XR_SESSION_STATE_EXITING || changed.state==XR_SESSION_STATE_LOSS_PENDING) {
                terminal=true;running=false;focused=false;
            }
        }
    }
    void initialize(const XrRuntimeOptions& options) {
        if(initialized || loader) throw std::logic_error("OpenXR runtime already initialized");
        if(!options.loader_path.is_absolute()) throw std::invalid_argument("OpenXR loader path must be absolute");
        owner_thread=GetCurrentThreadId();dc=static_cast<HDC>(options.native_dc);
        context=static_cast<HGLRC>(options.native_gl_context);require_owner();
        information={};terminal=exit_requested=false;pending_space_changes.clear();
        try {
            loader=LoadLibraryExW(options.loader_path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(!loader) throw std::runtime_error("Cannot load OpenXR loader: Win32 "+std::to_string(GetLastError()));
            get_proc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));
            if(!get_proc) throw std::runtime_error("OpenXR loader has no xrGetInstanceProcAddr");
            uint32_t count{};
            check(XR(xrEnumerateInstanceExtensionProperties)(nullptr,0,&count,nullptr),"extensions count");
            std::vector<XrExtensionProperties> extensions(count,structure<XrExtensionProperties>(XR_TYPE_EXTENSION_PROPERTIES));
            check(XR(xrEnumerateInstanceExtensionProperties)(nullptr,count,&count,extensions.data()),"extensions");
            const auto has=[&](const char* name) {return std::any_of(extensions.begin(),extensions.end(),
                [&](const auto& extension){return std::strcmp(extension.extensionName,name)==0;});};
            if(!has(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) throw std::runtime_error("Runtime has no OpenGL support");
            information.hand_tracking_extension=has(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
            information.hand_system_gesture_extension=information.hand_tracking_extension&&has(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
            std::vector<const char*> enabled{XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
            if(information.hand_tracking_extension) enabled.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
            if(information.hand_system_gesture_extension)enabled.push_back(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
            auto create=structure<XrInstanceCreateInfo>(XR_TYPE_INSTANCE_CREATE_INFO);
            strcpy_s(create.applicationInfo.applicationName,"Stormworks VR");
            strcpy_s(create.applicationInfo.engineName,"Stormworks VR integration");
            create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
            create.enabledExtensionCount=static_cast<uint32_t>(enabled.size());create.enabledExtensionNames=enabled.data();
            check(XR(xrCreateInstance)(&create,&instance),"xrCreateInstance");
            auto properties=structure<XrInstanceProperties>(XR_TYPE_INSTANCE_PROPERTIES);
            check(XR(xrGetInstanceProperties)(instance,&properties),"xrGetInstanceProperties");
            information.runtime_name=properties.runtimeName;
            auto system_info=structure<XrSystemGetInfo>(XR_TYPE_SYSTEM_GET_INFO);
            system_info.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;XrSystemId system{};
            check(XR(xrGetSystem)(instance,&system_info,&system),"xrGetSystem");
            auto hand_properties=structure<XrSystemHandTrackingPropertiesEXT>(XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT);
            auto system_properties=structure<XrSystemProperties>(XR_TYPE_SYSTEM_PROPERTIES);
            system_properties.next=information.hand_tracking_extension?&hand_properties:nullptr;
            check(XR(xrGetSystemProperties)(instance,system,&system_properties),"xrGetSystemProperties");
            information.system_name=system_properties.systemName;
            information.hand_tracking_supported=information.hand_tracking_extension && hand_properties.supportsHandTracking;
            auto requirements=structure<XrGraphicsRequirementsOpenGLKHR>(XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR);
            check(XR(xrGetOpenGLGraphicsRequirementsKHR)(instance,system,&requirements),"xrGetOpenGLGraphicsRequirementsKHR");
            GLint major{},minor{};glGetIntegerv(0x821B,&major);glGetIntegerv(0x821C,&minor);
            const auto version=XR_MAKE_VERSION(major,minor,0);
            if(version<requirements.minApiVersionSupported || version>requirements.maxApiVersionSupported)
                throw std::runtime_error("Current game OpenGL version is outside runtime requirements");
            auto binding=structure<XrGraphicsBindingOpenGLWin32KHR>(XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR);
            binding.hDC=dc;binding.hGLRC=context;
            auto session_info=structure<XrSessionCreateInfo>(XR_TYPE_SESSION_CREATE_INFO);
            session_info.systemId=system;session_info.next=&binding;
            check(XR(xrCreateSession)(instance,&session_info,&session),"xrCreateSession");
            auto reference=structure<XrReferenceSpaceCreateInfo>(XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
            reference.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;reference.poseInReferenceSpace.orientation.w=1;
            check(XR(xrCreateReferenceSpace)(session,&reference,&space),"xrCreateReferenceSpace");
            check(XR(xrEnumerateEnvironmentBlendModes)(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"blend modes count");
            std::vector<XrEnvironmentBlendMode> modes(count);
            check(XR(xrEnumerateEnvironmentBlendModes)(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,count,&count,modes.data()),"blend modes");
            if(std::find(modes.begin(),modes.end(),XR_ENVIRONMENT_BLEND_MODE_OPAQUE)==modes.end())
                throw std::runtime_error("Runtime does not support opaque stereo");
            if(information.hand_tracking_supported) for(std::size_t i=0;i<2;++i) {
                auto hi=structure<XrHandTrackerCreateInfoEXT>(XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT);
                hi.hand=i==0?XR_HAND_LEFT_EXT:XR_HAND_RIGHT_EXT;hi.handJointSet=XR_HAND_JOINT_SET_DEFAULT_EXT;
                check(XR(xrCreateHandTrackerEXT)(session,&hi,&hands[i]),"xrCreateHandTrackerEXT");
            }
            check(XR(xrEnumerateViewConfigurationViews)(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"view count");
            if(count!=2) throw std::runtime_error("Expected two primary stereo views");
            std::array<XrViewConfigurationView,2> configs;
            configs.fill(structure<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW));
            check(XR(xrEnumerateViewConfigurationViews)(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,configs.data()),"views");
            if(count!=2) throw std::runtime_error("Stereo view count changed");
            check(XR(xrEnumerateSwapchainFormats)(session,0,&count,nullptr),"format count");
            std::vector<int64_t> formats(count);
            check(XR(xrEnumerateSwapchainFormats)(session,count,&count,formats.data()),"formats");
            const auto available=[&](int64_t value){return std::find(formats.begin(),formats.end(),value)!=formats.end();};
            information.srgb=available(0x8C43);information.color_format=information.srgb?0x8C43:GL_RGBA8;
            if(!available(information.color_format)) throw std::runtime_error("No RGBA8 or sRGB RGBA8 OpenXR color format");
            for(std::size_t i=0;i<2;++i) {
                auto& eye=eyes[i];
                if(!configs[i].recommendedImageRectWidth || !configs[i].recommendedImageRectHeight ||
                   configs[i].recommendedImageRectWidth>16384 || configs[i].recommendedImageRectHeight>16384)
                    throw std::runtime_error("Invalid OpenXR recommended image dimensions");
                eye.width=static_cast<int>(configs[i].recommendedImageRectWidth);
                eye.height=static_cast<int>(configs[i].recommendedImageRectHeight);
                auto si=structure<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
                si.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
                si.format=information.color_format;
                si.sampleCount=1;si.width=eye.width;si.height=eye.height;si.faceCount=si.arraySize=si.mipCount=1;
                check(XR(xrCreateSwapchain)(session,&si,&eye.swapchain),"xrCreateSwapchain");
                check(XR(xrEnumerateSwapchainImages)(eye.swapchain,0,&count,nullptr),"image count");
                if(!count) throw std::runtime_error("OpenXR swapchain has no images");
                eye.images.assign(count,structure<XrSwapchainImageOpenGLKHR>(XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR));
                check(XR(xrEnumerateSwapchainImages)(eye.swapchain,count,&count,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())),"swapchain images");
            }
            initialized=true;
        } catch(...) {cleanup();throw;}
    }
    XrRuntimeFrame begin() {
        require_initialized();
        if(active_frame) throw std::logic_error("Previous OpenXR frame is still open");
        try {if(!terminal) events();}
        catch(...) {terminal=true;focused=false;throw;}
        XrRuntimeFrame frame;frame.running=running;frame.focused=focused;frame.terminal=terminal;
        frame.space_revision=space_revision;
        if(!running || terminal) return frame;
        auto wait=structure<XrFrameWaitInfo>(XR_TYPE_FRAME_WAIT_INFO);
        auto state=structure<XrFrameState>(XR_TYPE_FRAME_STATE);
        auto begin=structure<XrFrameBeginInfo>(XR_TYPE_FRAME_BEGIN_INFO);
        try {
            check(XR(xrWaitFrame)(session,&wait,&state),"xrWaitFrame");
            check(XR(xrBeginFrame)(session,&begin),"xrBeginFrame");
        } catch(...) {terminal=true;focused=false;throw;}
        active_frame=true;frame.id=++sequence;frame.predicted_display_time_ns=state.predictedDisplayTime;
        frame.predicted_display_period_ns=state.predictedDisplayPeriod;
        current_frame=frame;
        try {
            for(auto it=pending_space_changes.begin();it!=pending_space_changes.end();) {
                if(*it<=state.predictedDisplayTime) {++space_revision;it=pending_space_changes.erase(it);}
                else ++it;
            }
            frame.space_revision=space_revision;
            auto locate=structure<XrViewLocateInfo>(XR_TYPE_VIEW_LOCATE_INFO);
            locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            locate.displayTime=state.predictedDisplayTime;locate.space=space;
            auto view_state=structure<XrViewState>(XR_TYPE_VIEW_STATE);uint32_t count{};
            views.fill(structure<XrView>(XR_TYPE_VIEW));
            check(XR(xrLocateViews)(session,&locate,&view_state,2,&count,views.data()),"xrLocateViews");
            frame.position_valid=(view_state.viewStateFlags&XR_VIEW_STATE_POSITION_VALID_BIT)!=0;
            frame.orientation_valid=(view_state.viewStateFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT)!=0;
            frame.position_tracked=(view_state.viewStateFlags&XR_VIEW_STATE_POSITION_TRACKED_BIT)!=0;
            frame.orientation_tracked=(view_state.viewStateFlags&XR_VIEW_STATE_ORIENTATION_TRACKED_BIT)!=0;
            frame.should_render=state.shouldRender && count==2 && frame.position_valid && frame.orientation_valid;
            std::array<bool,2> system_gestures{};
            for(std::size_t i=0;i<2;++i) {
                frame.eyes[i].pose=convert(views[i].pose);
                frame.eyes[i].fov={views[i].fov.angleLeft,views[i].fov.angleRight,views[i].fov.angleUp,views[i].fov.angleDown};
                frame.eyes[i].width=eyes[i].width;frame.eyes[i].height=eyes[i].height;
                if(hands[i]) {
                    std::array<XrHandJointLocationEXT,xr_hand_joint_count> joints{};
                    auto li=structure<XrHandJointsLocateInfoEXT>(XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT);
                    li.baseSpace=space;li.time=state.predictedDisplayTime;
                    auto locations=structure<XrHandJointLocationsEXT>(XR_TYPE_HAND_JOINT_LOCATIONS_EXT);
                    auto aim=structure<XrHandTrackingAimStateFB>(XR_TYPE_HAND_TRACKING_AIM_STATE_FB);
                    if(information.hand_system_gesture_extension)locations.next=&aim;
                    locations.jointCount=static_cast<uint32_t>(joints.size());locations.jointLocations=joints.data();
                    check(XR(xrLocateHandJointsEXT)(hands[i],&li,&locations),"xrLocateHandJointsEXT");
                    system_gestures[i]=(aim.status&(XR_HAND_TRACKING_AIM_SYSTEM_GESTURE_BIT_FB|XR_HAND_TRACKING_AIM_MENU_PRESSED_BIT_FB))!=0;
                    frame.hands[i].active=locations.isActive==XR_TRUE;
                    if(frame.hands[i].active) for(std::size_t j=0;j<joints.size();++j) {
                        const auto flags=joints[j].locationFlags;
                        frame.hands[i].joints[j]={convert(joints[j].pose),joints[j].radius,
                            (flags&XR_SPACE_LOCATION_POSITION_VALID_BIT)!=0,(flags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)!=0,
                            (flags&XR_SPACE_LOCATION_POSITION_TRACKED_BIT)!=0,(flags&XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)!=0};
                    }
                }
            }
            suppress_system_hand_input(frame,system_gestures);
            if(frame.should_render) for(std::size_t i=0;i<2;++i) {
                auto& eye=eyes[i];uint32_t index{};
                auto acquire=structure<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
                check(XR(xrAcquireSwapchainImage)(eye.swapchain,&acquire,&index),"xrAcquireSwapchainImage");eye.acquired=true;
                auto wi=structure<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);wi.timeout=XR_INFINITE_DURATION;
                const auto result=XR(xrWaitSwapchainImage)(eye.swapchain,&wi);
                if(result==XR_TIMEOUT_EXPIRED) throw std::runtime_error("Infinite OpenXR image wait unexpectedly timed out");
                check(result,"xrWaitSwapchainImage");eye.waited=true;
                frame.eyes[i].texture=eye.images.at(index).image;
                if(!frame.eyes[i].texture) throw std::runtime_error("OpenXR returned a zero texture name");
            }
            current_frame=frame;return frame;
        } catch(...) {terminal=true;focused=false;cancel();throw;}
    }
#undef XR
};
XrRuntime::XrRuntime():impl_(std::make_unique<Impl>()) {}
XrRuntime::~XrRuntime()=default;
void XrRuntime::initialize(const XrRuntimeOptions& options) {impl_->initialize(options);}
const XrRuntimeInfo& XrRuntime::info() const {return impl_->information;}
XrRuntimeFrame XrRuntime::begin_frame() {return impl_->begin();}
void XrRuntime::end_frame(std::uint64_t id,std::array<bool,2> rendered) {
    impl_->require_initialized();
    if(!id || !impl_->active_frame || id!=impl_->current_frame.id) throw std::logic_error("Stale or absent OpenXR frame id");
    try {impl_->finish(rendered);} catch(...) {impl_->terminal=true;impl_->focused=false;impl_->cancel();throw;}
}
void XrRuntime::end_frame(std::uint64_t id,std::array<bool,2> rendered,const XrViewOverride& views) {
    impl_->require_initialized();
    if(!id || !impl_->active_frame || id!=impl_->current_frame.id) throw std::logic_error("Stale or absent OpenXR frame id");
    try {impl_->finish(rendered,&views);} catch(...) {impl_->terminal=true;impl_->focused=false;impl_->cancel();throw;}
}
void XrRuntime::cancel_frame() noexcept {impl_->cancel();}
void XrRuntime::request_exit() {
    impl_->require_initialized();
    if(impl_->running && !impl_->exit_requested) {
        check(impl_->function<PFN_xrRequestExitSession>("xrRequestExitSession")(impl_->session),"xrRequestExitSession");
        impl_->exit_requested=true;
    }
}
void XrRuntime::shutdown() {
    if(!impl_->loader && !impl_->initialized) return;
    impl_->require_owner();impl_->cleanup();impl_=std::make_unique<Impl>();
}
}
