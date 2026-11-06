// Standalone hardware integration test. Does not attach to Stormworks.
#include <windows.h>
#include <GL/gl.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "swvr/tracking.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
volatile std::sig_atomic_t interrupted=0;
void interrupt(int) { interrupted=1; }
void check(XrResult result, const char* operation) {
    if (XR_FAILED(result)) throw std::runtime_error(std::string(operation)+": XrResult "+std::to_string(result));
}
template<class T> T structure(XrStructureType type) { T value{}; value.type=type; return value; }
swvr::Pose pose(XrPosef p) { return {{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},
                                   {p.position.x,p.position.y,p.position.z}}; }
swvr::Vec3 vec(XrVector3f p) { return {p.x,p.y,p.z}; }
struct Eye {
    XrSwapchain swapchain{};
    int width{},height{};
    GLuint framebuffer{},depth{};
    std::vector<XrSwapchainImageOpenGLKHR> images;
};
class Demo {
    HMODULE loader{};
    HWND window{};
    HDC dc{};
    HGLRC context{};
    XrInstance instance{};
    XrSession session{};
    XrSpace space{};
    std::array<XrHandTrackerEXT,2> hands{};
    std::array<Eye,2> eyes{};
    PFN_xrGetInstanceProcAddr get_proc{};
    bool hand_support{},running{},focused{};
    std::uint64_t submitted{},tracked_head{};
    std::array<std::uint64_t,2> tracked_hands{};
    std::array<swvr::PinchDetector,2> pinches{};
    using Gen = void(APIENTRY*)(GLsizei,GLuint*);
    using Bind = void(APIENTRY*)(GLenum,GLuint);
    using Delete = void(APIENTRY*)(GLsizei,const GLuint*);
    Gen gen_fb{},gen_rb{};
    Bind bind_fb{},bind_rb{};
    Delete delete_fb{},delete_rb{};
    void(APIENTRY* attach_texture)(GLenum,GLenum,GLenum,GLuint,GLint){};
    void(APIENTRY* rb_storage)(GLenum,GLenum,GLsizei,GLsizei){};
    void(APIENTRY* attach_rb)(GLenum,GLenum,GLenum,GLuint){};
    GLenum(APIENTRY* fb_status)(GLenum){};
    static constexpr GLenum framebuffer=0x8D40, renderbuffer=0x8D41;
    template<class T> T xr(const char* name) {
        PFN_xrVoidFunction address{};
        check(get_proc(instance,name,&address),name);
        if (!address) throw std::runtime_error(std::string("Missing ")+name);
        return reinterpret_cast<T>(address);
    }
    template<class T> T gl(const char* name) {
        auto p=wglGetProcAddress(name);
        if (!p || p==reinterpret_cast<PROC>(1) || p==reinterpret_cast<PROC>(2) ||
            p==reinterpret_cast<PROC>(3) || p==reinterpret_cast<PROC>(-1))
            throw std::runtime_error(std::string("Missing OpenGL function ")+name);
        return reinterpret_cast<T>(p);
    }
#define XR(name) xr<PFN_##name>(#name)
    void graphics() {
        window=CreateWindowExW(0,L"STATIC",L"Stormworks VR diagnostic context",WS_OVERLAPPEDWINDOW,
                               0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if (!window) throw std::runtime_error("CreateWindow failed");
        dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize=sizeof(pfd); pfd.nVersion=1;
        pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=32; pfd.cDepthBits=24;
        const auto format=ChoosePixelFormat(dc,&pfd);
        if (!format || !SetPixelFormat(dc,format,&pfd)) throw std::runtime_error("Pixel format failed");
        context=wglCreateContext(dc);
        if (!context || !wglMakeCurrent(dc,context)) throw std::runtime_error("OpenGL context failed");
        std::cout << "OpenGL: " << glGetString(GL_VERSION) << '\n';
        gen_fb=gl<Gen>("glGenFramebuffers"); gen_rb=gl<Gen>("glGenRenderbuffers");
        bind_fb=gl<Bind>("glBindFramebuffer"); bind_rb=gl<Bind>("glBindRenderbuffer");
        delete_fb=gl<Delete>("glDeleteFramebuffers"); delete_rb=gl<Delete>("glDeleteRenderbuffers");
        attach_texture=gl<decltype(attach_texture)>("glFramebufferTexture2D");
        rb_storage=gl<decltype(rb_storage)>("glRenderbufferStorage");
        attach_rb=gl<decltype(attach_rb)>("glFramebufferRenderbuffer");
        fb_status=gl<decltype(fb_status)>("glCheckFramebufferStatus");
    }
    void draw(const XrView& view, const std::array<std::array<XrHandJointLocationEXT,26>,2>& joints,
              const std::array<bool,2>& active) {
        const auto projection=swvr::projection({view.fov.angleLeft,view.fov.angleRight,
                                              view.fov.angleUp,view.fov.angleDown},.05f,100.f);
        const auto camera=swvr::view_matrix(pose(view.pose));
        glMatrixMode(GL_PROJECTION); glLoadMatrixf(projection.data());
        glMatrixMode(GL_MODELVIEW); glLoadMatrixf(camera.data());
        glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        glClearColor(.025f,.035f,.055f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        // A metre grid and a panel in LOCAL space make scale/parallax visible.
        glColor3f(.18f,.3f,.4f); glBegin(GL_LINES);
        for (int i=-5;i<=5;++i) {
            const float n=static_cast<float>(i);
            glVertex3f(n,-1.3f,-5); glVertex3f(n,-1.3f,5);
            glVertex3f(-5,-1.3f,n); glVertex3f(5,-1.3f,n);
        }
        glEnd();
        glColor3f(.1f,.3f,.2f); glBegin(GL_QUADS);
        glVertex3f(-.5f,-.4f,-1); glVertex3f(.5f,-.4f,-1);
        glVertex3f(.5f,.3f,-1); glVertex3f(-.5f,.3f,-1); glEnd();
        glPointSize(9);
        for (std::size_t h=0;h<2;++h) if (active[h]) {
            glColor3f(h==0?.2f:1.f,.7f,h==0?1.f:.2f);
            glBegin(GL_POINTS);
            for (const auto& j:joints[h]) if (j.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
                glVertex3f(j.pose.position.x,j.pose.position.y,j.pose.position.z);
            glEnd();
        }
    }
public:
    void graphics_check() {
        graphics();
        auto& e=eyes[0];
        gen_fb(1,&e.framebuffer); bind_fb(framebuffer,e.framebuffer);
        GLuint texture{};glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,0x8C43,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        attach_texture(framebuffer,0x8CE0,GL_TEXTURE_2D,texture,0);
        gen_rb(1,&e.depth);bind_rb(renderbuffer,e.depth);rb_storage(renderbuffer,0x81A6,64,64);
        attach_rb(framebuffer,0x8D00,renderbuffer,e.depth);
        if(fb_status(framebuffer)!=0x8CD5) throw std::runtime_error("Graphics check: incomplete FBO");
        auto view=structure<XrView>(XR_TYPE_VIEW);
        view.pose.orientation.w=1;view.fov={-.8f,.8f,.8f,-.8f};
        glViewport(0,0,64,64);glEnable(0x8DB9);
        draw(view,{},{});
        std::array<unsigned char,4> center{},corner{};
        glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,center.data());
        glReadPixels(0,63,1,1,GL_RGBA,GL_UNSIGNED_BYTE,corner.data());
        const auto error=glGetError();glDeleteTextures(1,&texture);
        if(error!=GL_NO_ERROR) throw std::runtime_error("Graphics check GL error: "+std::to_string(error));
        if(center[1]<=center[0] || center[1]<=center[2] || center[1]<=corner[1])
            throw std::runtime_error("Graphics check: panel pixel readback did not match scene");
        std::cout<<"PASS graphics check: offscreen scene, sRGB/depth framebuffer and readback (no headset)\n";
    }
    ~Demo() {
        // Best-effort teardown; no exception may escape a destructor.
        try {
            if (instance) {
                for(auto h:hands) if(h) XR(xrDestroyHandTrackerEXT)(h);
                for(auto& e:eyes) if(e.swapchain) XR(xrDestroySwapchain)(e.swapchain);
                if(space) XR(xrDestroySpace)(space);
                if(session) XR(xrDestroySession)(session);
                XR(xrDestroyInstance)(instance);
            }
        } catch (...) {}
        if(context) {
            wglMakeCurrent(dc,context);
            for(auto& e:eyes) {
                if(e.framebuffer && delete_fb) delete_fb(1,&e.framebuffer);
                if(e.depth && delete_rb) delete_rb(1,&e.depth);
            }
            wglMakeCurrent(nullptr,nullptr); wglDeleteContext(context);
        }
        if(dc) ReleaseDC(window,dc);
        if(window) DestroyWindow(window);
        if(loader) FreeLibrary(loader);
    }
    void initialize(const std::filesystem::path& library) {
        loader=LoadLibraryExW(library.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!loader) throw std::runtime_error("Cannot load OpenXR loader: "+std::to_string(GetLastError()));
        get_proc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));
        if(!get_proc) throw std::runtime_error("Loader has no xrGetInstanceProcAddr");
        uint32_t count{};
        check(XR(xrEnumerateInstanceExtensionProperties)(nullptr,0,&count,nullptr),"extensions count");
        std::vector<XrExtensionProperties> extensions(count,structure<XrExtensionProperties>(XR_TYPE_EXTENSION_PROPERTIES));
        check(XR(xrEnumerateInstanceExtensionProperties)(nullptr,count,&count,extensions.data()),"extensions");
        const auto has=[&](const char* name) {return std::any_of(extensions.begin(),extensions.end(),
            [&](const auto& e){return std::string(e.extensionName)==name;});};
        if(!has(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) throw std::runtime_error("Runtime has no OpenGL support");
        std::vector<const char*> enabled{XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
        const bool hand_extension=has(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        if(hand_extension) enabled.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        auto create=structure<XrInstanceCreateInfo>(XR_TYPE_INSTANCE_CREATE_INFO);
        strcpy_s(create.applicationInfo.applicationName,"Stormworks VR hardware demo");
        create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
        create.enabledExtensionCount=static_cast<uint32_t>(enabled.size()); create.enabledExtensionNames=enabled.data();
        check(XR(xrCreateInstance)(&create,&instance),"xrCreateInstance");
        auto properties=structure<XrInstanceProperties>(XR_TYPE_INSTANCE_PROPERTIES);
        check(XR(xrGetInstanceProperties)(instance,&properties),"runtime properties");
        std::cout<<"Runtime: "<<properties.runtimeName<<'\n';
        auto system_info=structure<XrSystemGetInfo>(XR_TYPE_SYSTEM_GET_INFO);
        system_info.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId system{};
        check(XR(xrGetSystem)(instance,&system_info,&system),"xrGetSystem");
        auto hand_properties=structure<XrSystemHandTrackingPropertiesEXT>(XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT);
        auto system_properties=structure<XrSystemProperties>(XR_TYPE_SYSTEM_PROPERTIES);
        system_properties.next=hand_extension?&hand_properties:nullptr;
        check(XR(xrGetSystemProperties)(instance,system,&system_properties),"system properties");
        hand_support=hand_properties.supportsHandTracking==XR_TRUE;
        std::cout<<"System: "<<system_properties.systemName<<"; hand tracking: "<<hand_support<<'\n';
        auto requirements=structure<XrGraphicsRequirementsOpenGLKHR>(XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR);
        check(XR(xrGetOpenGLGraphicsRequirementsKHR)(instance,system,&requirements),"GL requirements");
        graphics();
        GLint major{},minor{}; glGetIntegerv(0x821B,&major); glGetIntegerv(0x821C,&minor);
        const XrVersion version=XR_MAKE_VERSION(major,minor,0);
        if(version<requirements.minApiVersionSupported || version>requirements.maxApiVersionSupported)
            throw std::runtime_error("GL version outside runtime requirements");
        auto binding=structure<XrGraphicsBindingOpenGLWin32KHR>(XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR);
        binding.hDC=dc; binding.hGLRC=context;
        auto session_info=structure<XrSessionCreateInfo>(XR_TYPE_SESSION_CREATE_INFO);
        session_info.systemId=system; session_info.next=&binding;
        check(XR(xrCreateSession)(instance,&session_info,&session),"xrCreateSession");
        auto reference=structure<XrReferenceSpaceCreateInfo>(XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
        reference.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL; reference.poseInReferenceSpace.orientation.w=1;
        check(XR(xrCreateReferenceSpace)(session,&reference,&space),"local reference space");
        if(hand_support) for(std::size_t i=0;i<2;++i) {
            auto hi=structure<XrHandTrackerCreateInfoEXT>(XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT);
            hi.hand=i==0?XR_HAND_LEFT_EXT:XR_HAND_RIGHT_EXT; hi.handJointSet=XR_HAND_JOINT_SET_DEFAULT_EXT;
            check(XR(xrCreateHandTrackerEXT)(session,&hi,&hands[i]),"hand tracker");
        }
        check(XR(xrEnumerateViewConfigurationViews)(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr),"views count");
        if(count!=2) throw std::runtime_error("Expected exactly two stereo views");
        std::array<XrViewConfigurationView,2> configs;
        configs.fill(structure<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW));
        check(XR(xrEnumerateViewConfigurationViews)(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,configs.data()),"views");
        check(XR(xrEnumerateSwapchainFormats)(session,0,&count,nullptr),"formats count");
        std::vector<int64_t> formats(count);
        check(XR(xrEnumerateSwapchainFormats)(session,count,&count,formats.data()),"formats");
        if(std::find(formats.begin(),formats.end(),0x8C43)==formats.end()) throw std::runtime_error("sRGB RGBA8 swapchain unavailable");
        for(std::size_t i=0;i<2;++i) {
            auto& e=eyes[i]; e.width=static_cast<int>(configs[i].recommendedImageRectWidth);
            e.height=static_cast<int>(configs[i].recommendedImageRectHeight);
            auto si=structure<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
            si.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT; si.format=0x8C43;
            si.sampleCount=1; si.width=e.width; si.height=e.height; si.faceCount=si.arraySize=si.mipCount=1;
            check(XR(xrCreateSwapchain)(session,&si,&e.swapchain),"swapchain");
            check(XR(xrEnumerateSwapchainImages)(e.swapchain,0,&count,nullptr),"images count");
            e.images.assign(count,structure<XrSwapchainImageOpenGLKHR>(XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR));
            check(XR(xrEnumerateSwapchainImages)(e.swapchain,count,&count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data())),"images");
            gen_fb(1,&e.framebuffer); gen_rb(1,&e.depth); bind_rb(renderbuffer,e.depth);
            rb_storage(renderbuffer,0x81A6,e.width,e.height); // GL_DEPTH_COMPONENT24
        }
    }
    void run(int seconds) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
        bool exit_requested=false,quit=false;
        auto exit_time=deadline;
        while(!quit) {
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message); DispatchMessageW(&message);}
            for(;;) {
                auto event=structure<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
                auto result=XR(xrPollEvent)(instance,&event);
                if(result==XR_EVENT_UNAVAILABLE) break;
                check(result,"event");
                if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {quit=true;break;}
                if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                    const auto state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
                    focused=state==XR_SESSION_STATE_FOCUSED;
                    if(!focused) for(auto& p:pinches) p.reset();
                    std::cout<<"Session state: "<<state<<'\n';
                    if(state==XR_SESSION_STATE_READY) {
                        auto begin=structure<XrSessionBeginInfo>(XR_TYPE_SESSION_BEGIN_INFO);
                        begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        check(XR(xrBeginSession)(session,&begin),"begin session"); running=true;
                    } else if(state==XR_SESSION_STATE_STOPPING) {
                        check(XR(xrEndSession)(session),"end session"); running=false; quit=true;
                    } else if(state==XR_SESSION_STATE_EXITING || state==XR_SESSION_STATE_LOSS_PENDING) quit=true;
                }
            }
            if(quit) break;
            const auto now=std::chrono::steady_clock::now();
            if(interrupted || now>=deadline) {
                if(!running) break;
                if(!exit_requested) {check(XR(xrRequestExitSession)(session),"request exit");exit_requested=true;exit_time=now;}
                if(now-exit_time>std::chrono::seconds(3)) break;
            }
            if(!running) {std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
            auto wait=structure<XrFrameWaitInfo>(XR_TYPE_FRAME_WAIT_INFO);
            auto frame=structure<XrFrameState>(XR_TYPE_FRAME_STATE);
            check(XR(xrWaitFrame)(session,&wait,&frame),"wait frame");
            auto begin=structure<XrFrameBeginInfo>(XR_TYPE_FRAME_BEGIN_INFO);
            check(XR(xrBeginFrame)(session,&begin),"begin frame");
            auto locate=structure<XrViewLocateInfo>(XR_TYPE_VIEW_LOCATE_INFO);
            locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            locate.displayTime=frame.predictedDisplayTime; locate.space=space;
            auto view_state=structure<XrViewState>(XR_TYPE_VIEW_STATE);
            std::array<XrView,2> views; views.fill(structure<XrView>(XR_TYPE_VIEW));
            uint32_t count{};
            check(XR(xrLocateViews)(session,&locate,&view_state,2,&count,views.data()),"locate views");
            const auto valid=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
            const bool render=frame.shouldRender && count==2 && (view_state.viewStateFlags&valid)==valid;
            if((view_state.viewStateFlags & (XR_VIEW_STATE_POSITION_TRACKED_BIT|XR_VIEW_STATE_ORIENTATION_TRACKED_BIT))==
                (XR_VIEW_STATE_POSITION_TRACKED_BIT|XR_VIEW_STATE_ORIENTATION_TRACKED_BIT)) ++tracked_head;
            std::array<std::array<XrHandJointLocationEXT,26>,2> joints{};
            std::array<bool,2> active{};
            for(std::size_t h=0;h<2;++h) if(hands[h]) {
                auto li=structure<XrHandJointsLocateInfoEXT>(XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT);
                li.baseSpace=space;li.time=frame.predictedDisplayTime;
                auto locations=structure<XrHandJointLocationsEXT>(XR_TYPE_HAND_JOINT_LOCATIONS_EXT);
                locations.jointCount=26;locations.jointLocations=joints[h].data();
                check(XR(xrLocateHandJointsEXT)(hands[h],&li,&locations),"locate hand joints");
                active[h]=locations.isActive==XR_TRUE;
                const auto& thumb=joints[h][XR_HAND_JOINT_THUMB_TIP_EXT];
                const auto& index=joints[h][XR_HAND_JOINT_INDEX_TIP_EXT];
                const auto flags=thumb.locationFlags&index.locationFlags;
                swvr::HandSample sample{active[h],(flags&XR_SPACE_LOCATION_POSITION_VALID_BIT)!=0,
                    (flags&XR_SPACE_LOCATION_POSITION_TRACKED_BIT)!=0,focused,frame.predictedDisplayTime,
                    vec(thumb.pose.position),vec(index.pose.position)};
                if(sample.active && sample.position_valid && sample.position_tracked && focused) ++tracked_hands[h];
                const auto gesture=pinches[h].update(sample,frame.predictedDisplayTime);
                if(gesture!=swvr::PinchEvent::none && gesture!=swvr::PinchEvent::held)
                    std::cout<<"Hand "<<h<<" pinch event "<<static_cast<int>(gesture)<<'\n';
            }
            std::array<XrCompositionLayerProjectionView,2> layer_views;
            layer_views.fill(structure<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW));
            if(render) for(std::size_t i=0;i<2;++i) {
                auto& e=eyes[i]; uint32_t image{};
                auto acquire=structure<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
                check(XR(xrAcquireSwapchainImage)(e.swapchain,&acquire,&image),"acquire image");
                auto wi=structure<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);wi.timeout=XR_INFINITE_DURATION;
                check(XR(xrWaitSwapchainImage)(e.swapchain,&wi),"wait image");
                bind_fb(framebuffer,e.framebuffer);
                attach_texture(framebuffer,0x8CE0,GL_TEXTURE_2D,e.images.at(image).image,0);
                attach_rb(framebuffer,0x8D00,renderbuffer,e.depth);
                if(fb_status(framebuffer)!=0x8CD5) throw std::runtime_error("Incomplete framebuffer");
                glViewport(0,0,e.width,e.height);glEnable(0x8DB9); // GL_FRAMEBUFFER_SRGB
                draw(views[i],joints,active);glFlush();
                auto release=structure<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
                check(XR(xrReleaseSwapchainImage)(e.swapchain,&release),"release image");
                layer_views[i].pose=views[i].pose;layer_views[i].fov=views[i].fov;
                layer_views[i].subImage.swapchain=e.swapchain;
                layer_views[i].subImage.imageRect.extent={e.width,e.height};
            }
            auto layer=structure<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
            layer.space=space;layer.viewCount=2;layer.views=layer_views.data();
            const XrCompositionLayerBaseHeader* layers[]{reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
            auto end=structure<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
            end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            end.layerCount=render?1:0;end.layers=render?layers:nullptr;
            check(XR(xrEndFrame)(session,&end),"end frame");
            if(render) ++submitted;
        }
        std::cout<<"SUMMARY stereo_frames="<<submitted<<" head_tracked_frames="<<tracked_head
                 <<" left_hand_tracked_samples="<<tracked_hands[0]
                 <<" right_hand_tracked_samples="<<tracked_hands[1]<<'\n';
        if(!submitted) throw std::runtime_error("No stereo frames submitted; hardware test not passed");
        if(!tracked_head || !tracked_hands[0] || !tracked_hands[1])
            throw std::runtime_error("Stereo ran but head/both hands were not all tracked; hardware test incomplete");
    }
};
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==2 && std::wstring(argv[1])==L"--graphics-check") {Demo demo;demo.graphics_check();return 0;}
        if(argc<2 || argc>3) {std::cerr<<"Usage: swvr_xr_demo.exe ABSOLUTE_OPENXR_LOADER_PATH [seconds:1..600]\n";return 2;}
        const int seconds=argc==3?std::stoi(argv[2]):30;
        if(seconds<1 || seconds>600) throw std::runtime_error("Duration must be 1..600 seconds");
        const auto logs=std::filesystem::absolute("artifacts/steamvr-logs");
        std::filesystem::create_directories(logs);SetEnvironmentVariableW(L"VR_LOG_PATH",logs.c_str());
        std::signal(SIGINT,interrupt);
        Demo demo; demo.initialize(std::filesystem::absolute(argv[1]));demo.run(seconds);
        return 0;
    } catch(const std::exception& error) {std::cerr<<"ERROR: "<<error.what()<<'\n';return 1;}
}
