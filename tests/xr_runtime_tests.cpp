#include "swvr/xr_runtime.hpp"
#include "xr_runtime_fake.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class F>void throws(F&& fn,const char* message) {bool caught{};try {fn();}catch(const std::exception&){caught=true;}require(caught,message);}
struct Context {
    HWND window{};HDC dc{};HGLRC gl{};
    Context() {
        window=CreateWindowExW(0,L"STATIC",L"SWVR hidden runtime test",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window!=nullptr,"hidden test window");dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pf{};pf.nSize=sizeof(pf);pf.nVersion=1;pf.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
        pf.iPixelType=PFD_TYPE_RGBA;pf.cColorBits=32;
        const int format=ChoosePixelFormat(dc,&pf);require(format && SetPixelFormat(dc,format,&pf),"GL pixel format");
        gl=wglCreateContext(dc);require(gl && wglMakeCurrent(dc,gl),"GL context");
    }
    ~Context() {wglMakeCurrent(nullptr,nullptr);if(gl)wglDeleteContext(gl);if(dc)ReleaseDC(window,dc);if(window)DestroyWindow(window);}
};
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"usage: swvr_xr_runtime_tests absolute-fake-loader.dll");Context context;
        const auto path=std::filesystem::absolute(argv[1]);const HMODULE loader=LoadLibraryW(path.c_str());require(loader!=nullptr,"fake loader DLL");
        const auto reset=reinterpret_cast<FakeXrState*(*)()>(GetProcAddress(loader,"swvr_test_reset"));require(reset!=nullptr,"fake reset API");
        auto* state=reset();swvr::XrRuntime runtime;const swvr::XrRuntimeOptions options{path,context.dc,context.gl};
        throws([&]{runtime.initialize({L"relative.dll",context.dc,context.gl});},"relative loader must fail");
        runtime.initialize(options);require(runtime.info().hand_tracking_supported,"hand capability");
        auto frame=runtime.begin_frame();require(frame.id && frame.running && frame.focused && frame.should_render,"ready/focused stereo");
        require(frame.predicted_display_period_ns==11'111'111,"runtime period missing");
        require(frame.eyes[0].texture==100 && frame.eyes[1].texture==101,"borrowed eye textures");
        require(frame.position_valid && frame.orientation_valid && frame.position_tracked && frame.orientation_tracked,"view flags");
        require(frame.hands[0].active && frame.hands[1].joints[25].position_valid && !frame.hands[0].joints[0].orientation_valid,"26 hand joint flags");
        require(state->eye_time==frame.predicted_display_time_ns && state->hand_times[0]==state->eye_time && state->hand_times[1]==state->eye_time,"one prediction time");
        throws([&]{runtime.begin_frame();},"nested frame rejected");
        throws([&]{runtime.end_frame(frame.id+1,{true,true});},"stale frame rejected");
        runtime.end_frame(frame.id,{true,true});require(state->released==2 && state->projections==1,"complete stereo release/submission");
        require(state->last_view_x[0]==-.032f && state->last_view_x[1]==.032f && state->last_fov_left[0]==-.7f,"located views not submitted by default");
        // A repeated image is submitted with the poses/FOV it was rendered with.
        frame=runtime.begin_frame();
        const swvr::XrViewOverride repeat{{swvr::Pose{{},{.5f,1.f,0}},swvr::Pose{{},{.6f,1.f,0}}},{swvr::Fov{-.3f,.3f,.3f,-.3f},swvr::Fov{-.4f,.3f,.3f,-.3f}}};
        runtime.end_frame(frame.id,{true,true},repeat);
        require(state->projections==2 && state->last_view_x[0]==.5f && state->last_view_x[1]==.6f && state->last_fov_left[0]==-.3f && state->last_fov_left[1]==-.4f,"view override not submitted");
        frame=runtime.begin_frame();runtime.end_frame(frame.id,{true,false});require(state->empty_frames==1 && state->projections==2,"partial stereo discarded");
        const int acquired_before=state->acquired;state->should_render=false;
        frame=runtime.begin_frame();require(frame.id && !frame.should_render && !frame.eyes[0].texture,"hidden frame still begins");runtime.end_frame(frame.id,{true,true});
        require(state->acquired==acquired_before,"shouldRender false does not acquire");state->should_render=true;state->valid_pose=false;
        frame=runtime.begin_frame();require(!frame.should_render && !frame.position_valid,"invalid head position suppresses render");runtime.cancel_frame();
        require(state->acquired==acquired_before,"invalid views do not acquire");state->valid_pose=true;state->active_hands=false;
        state->pending_event=4; // XR_SESSION_STATE_VISIBLE
        frame=runtime.begin_frame();require(!frame.focused && !frame.hands[0].active && !frame.hands[0].joints[0].position_valid,"focus loss and stale hands cleared");runtime.cancel_frame();
        state->active_hands=true;state->display_period=22'222'222;
        frame=runtime.begin_frame();require(!frame.focused&&frame.should_render&&frame.hands[0].active,"fixture must exercise unfocused but tracked hands");require(frame.predicted_display_period_ns==22'222'222,"changing runtime period ignored");runtime.end_frame(frame.id,{true,true});
        state->pending_event=5;frame=runtime.begin_frame();require(frame.focused&&frame.hands[0].active,"focus recovery missing");runtime.cancel_frame();
        state->pending_event=100;frame=runtime.begin_frame();require(frame.space_revision==1,"LOCAL-space change invalidates calibration");runtime.cancel_frame();
        bool wrong_thread_rejected{};std::thread foreign([&]{try {runtime.begin_frame();}catch(const std::logic_error&){wrong_thread_rejected=true;}});foreign.join();
        require(wrong_thread_rejected,"foreign thread rejected");
        runtime.request_exit();frame=runtime.begin_frame();require(!frame.id && !frame.running && state->end_sessions==1,"STOPPING ends session without waiting another frame");
        runtime.shutdown();require(state->destroyed_swapchains==2 && state->destroyed_hands==2 && state->destroyed_sessions==1 && state->destroyed_instances==1,"owned objects cleaned");
        state=reset();state->hand_aim_extension=true;runtime.initialize(options);
        require(runtime.info().hand_system_gesture_extension&&state->hand_aim_enabled,"advertised system gesture extension was not enabled");
        for(unsigned h=0;h<2;++h){state->system_menu_hands[h]=true;frame=runtime.begin_frame();
            require(!frame.hands[0].active&&!frame.hands[1].active,"reserved runtime gesture leaked into application hand input");runtime.cancel_frame();state->system_menu_hands[h]=false;}
        frame=runtime.begin_frame();require(frame.hands[0].active&&frame.hands[1].active,"hands did not return after the system gesture ended");runtime.cancel_frame();runtime.shutdown();
        for(const bool timeout:{false,true}) {
            state=reset();runtime.initialize(options);state->fail_right_acquire=!timeout;state->timeout_right_wait=timeout;
            throws([&]{runtime.begin_frame();},"injected image failure reported");
            require(state->released==1 && state->empty_frames==1 && state->projections==0 && state->invalid_releases==0,"image failure releases only waited image and submits empty");
            frame=runtime.begin_frame();require(!frame.id && frame.terminal,"failed runtime does not reuse images");runtime.shutdown();
        }
        state=reset();runtime.initialize(options);frame=runtime.begin_frame();runtime.shutdown();
        require(state->released==2 && state->empty_frames==1,"shutdown cancels an outstanding frame");
        FreeLibrary(loader);std::cout<<"OpenXR runtime protocol tests passed (fake loader, no HMD).\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
