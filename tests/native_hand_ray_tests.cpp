#include "swvr/native_hand_ray.hpp"
#include "swvr/hand_interaction.hpp"
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
using Selection=void(*)(void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t,const int*);
using Origin=void*(*)(void*,double*,std::uint8_t,std::uint8_t);
std::uintptr_t image_base{};
void* selection_detour{};void* origin_detour{};
std::atomic<unsigned> create_calls{},remove_calls{},selection_calls{},origin_calls{},unexpected_hook_calls{};
unsigned fail_create{};
std::array<unsigned char,0xab0> controller{};
std::array<std::uint64_t,4> contexts{0x1020304050607080ULL,0x2030405060708090ULL,0,0x4050607080901020ULL};
std::array<unsigned char,0x240> application_data{};
const std::array<double,3> native_head{1000000000.125,3,2000000000.25};
const std::array<float,3> native_forward{-.0f,.6f,.8f};
int character_token{};
swvr::HandInteractionBridge* bridge{};
std::uint64_t publication{};
enum Mode { normal,no_origin,wrong_origin,alternate_return,publish_during_origin,focus_loss,reset_during_origin,
            cancelled_then_recovered,pause_during_origin,slow_origin,throw_selection,nested,nested_wrong_caller,nonfinite_head,foreign_thread_origin,
            concurrent_hold,concurrent_second,benchmark_during_origin,application_changed_during_origin };
thread_local Mode active_mode=normal;
thread_local unsigned depth{};
thread_local const int* calling_a6{};
thread_local Mode calling_mode=normal;
std::atomic<bool> first_override{},second_entered{},first_done{};
std::atomic<unsigned> active_native{},peak_native{};
bool wait_flag(const std::atomic<bool>& flag,std::chrono::milliseconds timeout) {
    const auto end=std::chrono::steady_clock::now()+timeout;
    while(!flag.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<end)Sleep(1);
    return flag.load(std::memory_order_acquire);
}
struct CountNative {
    CountNative() {
        const auto count=active_native.fetch_add(1)+1;
        auto peak=peak_native.load();
        while(peak<count && !peak_native.compare_exchange_weak(peak,count)){}
    }
    ~CountNative(){active_native.fetch_sub(1);}
};
struct Observation {
    Mode mode{};unsigned depth{};std::array<double,3> origin{};std::array<float,3> forward{};bool returned_output{};
};
std::vector<Observation> observations;
std::mutex observations_mutex;
template<class T>void put(std::size_t offset,const T& value){std::memcpy(controller.data()+offset,&value,sizeof(value));}
template<class T>T get(std::size_t offset){T value{};std::memcpy(&value,controller.data()+offset,sizeof(value));return value;}
std::array<unsigned char,12> forward_bytes(){return get<std::array<unsigned char,12>>(0xaa0);}
struct CallbackObservation {
    Mode mode{};
    std::array<std::uint64_t,6> arguments{}; // Tokens only; never dereferenced later.
    swvr::HandInteractionSnapshot sample{};
    std::uintptr_t target{};
    bool used{},completed{},nested{};
    unsigned origins{};
};
struct ObserverState {
    std::atomic<unsigned> calls{};
    std::atomic<bool> failed{};
    std::mutex mutex;
    std::vector<CallbackObservation> records;
};
ObserverState observer,unexpected_observer;
void selection_complete(void* context,const swvr::NativeHandSelection& value) noexcept {
    auto& state=*static_cast<ObserverState*>(context);
    try {
        if(value.controller_live!=controller.data() || !value.a6_live || value.a6_live!=calling_a6 ||
           *value.a6_live!=static_cast<int>(calling_mode) || std::array{value.a2,value.a3,value.a4,value.a5}!=contexts ||
           depth!=0 || active_native!=0 || get<std::array<float,3>>(0xaa0)!=native_forward ||
           forward_bytes()!=std::bit_cast<std::array<unsigned char,12>>(native_forward) ||
           (value.completed && value.target_token!=get<std::uintptr_t>(0x520)) || (!value.completed && value.target_token!=0))state.failed=true;
        CallbackObservation record;
        record.mode=value.a6_live && value.a6_live==calling_a6?static_cast<Mode>(*value.a6_live):normal;
        record.arguments={reinterpret_cast<std::uintptr_t>(value.controller_live),value.a2,value.a3,value.a4,value.a5,
            reinterpret_cast<std::uintptr_t>(value.a6_live)};
        record.sample=value.sample;record.target=value.target_token;record.used=value.used_hand_ray;
        record.completed=value.completed;record.nested=value.nested_selection;record.origins=value.origin_calls;
        std::lock_guard lock(state.mutex);state.records.push_back(record);++state.calls;
    }catch(...){state.failed=true;}
}
bool same_sample(const swvr::HandInteractionSnapshot& a,const swvr::HandInteractionSnapshot& b) {
    return a.sequence==b.sequence && a.xr_frame_id==b.xr_frame_id && a.space_revision==b.space_revision &&
        a.cancellation_generation==b.cancellation_generation && a.pinch_gesture_id==b.pinch_gesture_id &&
        a.xr_time_ns==b.xr_time_ns && a.published_at==b.published_at && a.hand==b.hand && a.valid_ray==b.valid_ray &&
        a.pinch_down==b.pinch_down && a.ray.origin==b.ray.origin && a.ray.direction==b.ray.direction &&
        a.pinch==b.pinch && a.status==b.status;
}
CallbackObservation last_callback() {
    require(!observer.failed,"Observer ran with unrestored forward, invalid live ABI, or inside native/nested work");
    std::lock_guard lock(observer.mutex);require(!observer.records.empty(),"Selection observer was not called");return observer.records.back();
}
void assert_callback(unsigned before,Mode mode,const swvr::HandInteractionSnapshot& sample,bool used,
    bool completed=true,unsigned origins=1,bool nested_call=false) {
    require(observer.calls==before+1,"Known top-level Selection did not report exactly one completion");
    const auto value=last_callback();
    require(value.mode==mode && same_sample(value.sample,sample),"Observer replaced the bound Selection sample or live mode");
    require(value.used==used && value.completed==completed && value.origins==origins && value.nested==nested_call,
        "Observer lost used/completed/origin/nesting outcome");
}
swvr::Matrix4d world_base() {
    return {1,0,0,0,0,1,0,0,0,0,1,0,native_head[0],native_head[1],native_head[2],1};
}
swvr::XrRuntimeFrame make_frame(swvr::Vec3 tip={.25f,0,-.5f},float gap=.05f) {
    swvr::XrRuntimeFrame f{};f.id=++publication;f.predicted_display_time_ns=static_cast<std::int64_t>(publication)*10000000;
    f.running=f.focused=f.should_render=true;
    f.position_valid=f.orientation_valid=f.position_tracked=f.orientation_tracked=true;
    auto& h=f.hands[1];h.active=true;
    for(auto& j:h.joints){j.position_valid=j.position_tracked=true;}
    h.joints[swvr::xr_index_tip].pose.position=tip;
    h.joints[7].pose.position={tip.x-.05f,tip.y,tip.z+.2f};
    h.joints[swvr::xr_thumb_tip].pose.position={tip.x+gap,tip.y,tip.z};
    return f;
}
void publish(swvr::Vec3 tip={.25f,0,-.5f},float gap=.05f) {bridge->publish(make_frame(tip,gap),swvr::Pose{},world_base());}
void call_selection(Mode mode,bool expected_caller=true) {
    const int input=mode;
    struct RestoreCaller {
        const int* previous_input=calling_a6;Mode previous_mode=calling_mode;
        ~RestoreCaller(){calling_a6=previous_input;calling_mode=previous_mode;}
    } restore;
    calling_a6=&input;calling_mode=mode;
    const auto call=expected_caller?reinterpret_cast<Selection>(image_base+0x745a00):reinterpret_cast<Selection>(selection_detour);
    call(controller.data(),contexts[0],contexts[1],contexts[2],contexts[3],&input);
}
void* native_origin(void* character,double* output,std::uint8_t mode,std::uint8_t option) {
    require(character==&character_token && mode==0x81 && option==0xfe,"Origin ABI was not preserved");
    ++origin_calls;std::memcpy(output,native_head.data(),sizeof(native_head));
    if(active_mode==publish_during_origin)publish({.75f,.2f,-.2f});
    if(active_mode==focus_loss){auto f=make_frame();f.focused=false;bridge->publish(f,swvr::Pose{},world_base());}
    if(active_mode==reset_during_origin)bridge->reset();
    if(active_mode==cancelled_then_recovered){bridge->reset();publish();}
    if(active_mode==pause_during_origin)put<unsigned char>(0x494,1);
    if(active_mode==benchmark_during_origin)application_data[0x233]=1;
    if(active_mode==application_changed_during_origin) {
        const auto changed=image_base+0xb17bf8;std::memcpy(application_data.data(),&changed,sizeof(changed));
    }
    if(active_mode==slow_origin)Sleep(110);
    if(active_mode==nonfinite_head)output[0]=std::numeric_limits<double>::quiet_NaN();
    return active_mode==alternate_return?output+1:output;
}
void native_selection(void* selected,std::uint64_t a2,std::uint64_t a3,std::uint64_t a4,std::uint64_t a5,const int* a6) {
    require(selected==controller.data() && a6 && std::array{a2,a3,a4,a5}==contexts,"Selection ABI was not preserved");
    CountNative count_native;
    ++selection_calls;const auto previous_mode=active_mode;active_mode=static_cast<Mode>(*a6);++depth;
    struct RestoreMode {Mode previous;~RestoreMode(){active_mode=previous;--depth;}} restore{previous_mode};
    if(active_mode==no_origin)return;
    std::array<double,3> result{};
    const auto call=active_mode==wrong_origin?reinterpret_cast<Origin>(origin_detour):reinterpret_cast<Origin>(image_base+0x74f37d);
    auto returned=call(&character_token,result.data(),0x81,0xfe);
    require(returned==(active_mode==alternate_return?result.data()+1:result.data()),"Origin returned pointer changed");
    {
        std::lock_guard lock(observations_mutex);
        observations.push_back({active_mode,depth,result,get<std::array<float,3>>(0xaa0),returned==result.data()});
    }
    // These target writes are made only by the original native-function double.
    // The adapter may change ray inputs, never picked fields or action state.
    put<std::uintptr_t>(0x520,depth>1?0x3333:result==native_head?0x1111:0x2222);put<std::uintptr_t>(0x538,0x4444);
    if(active_mode==concurrent_hold) {
        // Publish only after all first-thread native/override writes. Without
        // serialization, the second thread then saves the first's override.
        first_override.store(true,std::memory_order_release);
        // With the production mutex the second cannot enter before we return;
        // this finite delay deliberately permits an overlapping attempted call.
        (void)wait_flag(second_entered,std::chrono::milliseconds(40));
    }
    if(active_mode==concurrent_second) {
        // With no mutex, first is waiting and does no controller accesses. The
        // signals order writes/restoration, so the regression does not rely on
        // simultaneous unsynchronized fake-object writes to manifest.
        second_entered.store(true,std::memory_order_release);
        require(wait_flag(first_done,std::chrono::milliseconds(500)),"First concurrent Selection did not finish");
    }
    if(active_mode==nested || active_mode==nested_wrong_caller) {
        const auto outer_forward=forward_bytes();
        publish({.75f,.2f,-.2f});
        const bool expected=active_mode==nested;
        call_selection(normal,expected);
        require(forward_bytes()==outer_forward,"Nested Selection did not restore the outer override");
    }
    if(active_mode==foreign_thread_origin) {
        bool native_on_worker{};
        std::thread worker([&] {
            std::array<double,3> worker_origin{};
            const auto returned_worker=reinterpret_cast<Origin>(image_base+0x74f37d)(&character_token,worker_origin.data(),0x81,0xfe);
            native_on_worker=returned_worker==worker_origin.data() && worker_origin==native_head;
        });worker.join();
        require(native_on_worker,"A foreign thread borrowed the active Selection scope");
    }
    if(active_mode==throw_selection)throw std::runtime_error("expected native Selection exception");
}
void initialize_image(unsigned char* image) {
    constexpr unsigned char selection[]{0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0x58,0xf9,0xff,0xff,0x48,0x81};
    constexpr unsigned char origin[]{0x40,0x55,0x53,0x57,0x48,0x8d,0xac,0x24,0x20,0xfe,0xff,0xff,0x48,0x81,0xec,0xe0,0x02,0x00,0x00,0x48,0x8b,0x05,0x56,0x4c};
    std::memcpy(image+0x74f260,selection,sizeof(selection));std::memcpy(image+0x4e33a0,origin,sizeof(origin));
}
std::array<RUNTIME_FUNCTION,2> thunk_functions{};
void initialize_thunks(unsigned char* image) {
    // Origin: four arguments remain in registers; return RVA is 74F38D.
    std::array<unsigned char,21> o{0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,0x28,0xc3};
    auto target=reinterpret_cast<std::uintptr_t>(origin_detour);std::memcpy(o.data()+6,&target,8);
    std::memcpy(image+0x74f37d,o.data(),o.size());
    // Selection: forward args5/6 from old RSP+28/+30 into new outgoing slots.
    // CALL's return address is exactly 745A24 (36 bytes after thunk start).
    std::array<unsigned char,41> s{0x48,0x83,0xec,0x38,0x48,0x8b,0x44,0x24,0x60,
        0x48,0x89,0x44,0x24,0x20,0x48,0x8b,0x44,0x24,0x68,0x48,0x89,0x44,0x24,0x28,
        0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,0x38,0xc3};
    target=reinterpret_cast<std::uintptr_t>(selection_detour);std::memcpy(s.data()+26,&target,8);
    std::memcpy(image+0x745a00,s.data(),s.size());
    // These generated non-leaf frames allocate stack. Register their actual
    // x64 unwind recipe so throw_selection tests unwind reliably through them.
    const std::array<unsigned char,8> origin_info{1,4,1,0,4,0x42,0,0},selection_info{1,4,1,0,4,0x62,0,0};
    std::memcpy(image+0x7f0000,origin_info.data(),origin_info.size());std::memcpy(image+0x7f0008,selection_info.data(),selection_info.size());
    thunk_functions={RUNTIME_FUNCTION{0x745a00,0x745a00+static_cast<DWORD>(s.size()),0x7f0008},
        RUNTIME_FUNCTION{0x74f37d,0x74f37d+static_cast<DWORD>(o.size()),0x7f0000}};
    require(RtlAddFunctionTable(thunk_functions.data(),static_cast<DWORD>(thunk_functions.size()),image_base)!=0,
        "Could not register generated hand-ray thunk unwind metadata");
    FlushInstructionCache(GetCurrentProcess(),image,0x800000);
}
void clear_controller() {
    controller.fill(0);put(0xaa0,native_forward);application_data.fill(0);
    contexts[2]=reinterpret_cast<std::uintptr_t>(application_data.data());
    const auto vtable=image_base+0xb17cc0;std::memcpy(application_data.data(),&vtable,sizeof(vtable));
}
void assert_hand(const Observation& o,const swvr::HandInteractionSnapshot& expected) {
    for(unsigned i=0;i<3;++i) {
        require(o.origin[i]==expected.ray.origin[i],"Hand world origin lost precision or changed bound snapshot");
        require(std::abs(o.forward[i]-expected.ray.direction[i])<1e-7,"Hand direction changed or was reach-scaled");
    }
    require(std::abs(std::hypot(o.forward[0],o.forward[1],o.forward[2])-1)<1e-6,"Native hand direction is not unit length");
}
void assert_native(const Observation& o) {require(o.origin==native_head && o.forward==native_forward,"Blocked call changed native ray inputs");}
std::vector<std::string> fields(const std::string& row) {
    std::vector<std::string> out;std::size_t from{};
    for(;;){const auto to=row.find('\t',from);out.push_back(row.substr(from,to-from));if(to==std::string::npos)return out;from=to+1;}
}
int default_observer_case() {
    swvr::HandInteractionBridge hand_bridge;bridge=&hand_bridge;
    auto image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    require(image!=nullptr,"Default-observer fake image allocation failed");image_base=reinterpret_cast<std::uintptr_t>(image);initialize_image(image);
    const auto directory=std::filesystem::current_path()/"artifacts";std::filesystem::create_directories(directory);
    const auto trace=directory/("native-hand-ray-default-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    SetEnvironmentVariableW(L"SWVR_HAND_RAY",L"1");fail_create=2;
    require(swvr::install_native_hand_ray(image_base,true,trace.wstring()+L"-failed",*bridge,{&unexpected_observer,selection_complete})==
        swvr::NativeHandRayStatus::hook_failure,"Default-observer failed-install setup did not fail");
    fail_create=0;create_calls=0;
    // The original four-argument API installs no observer, even after a failed
    // attempt carrying a callback. Failure must not publish callback state.
    require(swvr::install_native_hand_ray(image_base,true,trace,*bridge)==swvr::NativeHandRayStatus::installed,
        "Source-compatible default installation failed");
    initialize_thunks(image);clear_controller();publish();const auto expected=bridge->inspect();call_selection(normal);
    assert_hand(observations.back(),expected);require(!observer.calls && !unexpected_observer.calls,
        "Default installation invoked an observer leaked from a failed attempt");
    RtlDeleteFunctionTable(thunk_functions.data());VirtualFree(image,0,MEM_RELEASE);
    SetEnvironmentVariableW(L"SWVR_HAND_RAY",nullptr);return 0;
}
void test_default_observer_child() {
    std::array<wchar_t,32768> executable{};
    const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    require(length && length<executable.size(),"Cannot resolve the fixture executable");
    std::wstring command=L"\""+std::wstring(executable.data(),length)+L"\" --no-observer";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    require(CreateProcessW(executable.data(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0,
        "Cannot start default-observer fixture child");
    const auto wait=WaitForSingleObject(process.hProcess,10000);DWORD exit_code{};
    const bool completed=wait==WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess,&exit_code)!=0;
    if(!completed)TerminateProcess(process.hProcess,2);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    require(completed && exit_code==0,"Source-compatible no-observer fixture child failed");
}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID detour,LPVOID* original) {
    ++create_calls;if(fail_create && create_calls==fail_create)return MH_ERROR_MEMORY_ALLOC;
    const auto rva=reinterpret_cast<std::uintptr_t>(target)-image_base;
    if(rva==0x74f260){selection_detour=detour;*original=reinterpret_cast<void*>(native_selection);}
    else if(rva==0x4e33a0){origin_detour=detour;*original=reinterpret_cast<void*>(native_origin);}
    else {++unexpected_hook_calls;return MH_ERROR_NOT_EXECUTABLE;}
    return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID){++remove_calls;return MH_OK;}
int main(int argc,char** argv) {
    using swvr::NativeHandRayStatus;
    try {
        if(argc==2 && std::string(argv[1])=="--no-observer")return default_observer_case();
        swvr::HandInteractionBridge hand_bridge;bridge=&hand_bridge;
        const auto directory=std::filesystem::current_path()/"artifacts";std::filesystem::create_directories(directory);
        const auto name="native-hand-ray-test-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
        const auto trace=directory/name;
        auto image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x800000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
        require(image!=nullptr,"Fake executable allocation failed");image_base=reinterpret_cast<std::uintptr_t>(image);initialize_image(image);
        SetEnvironmentVariableW(L"SWVR_HAND_RAY",L"0");
        require(!swvr::rearm_native_hand_ray_log(),"Rearm succeeded without an installed log");
        require(swvr::install_native_hand_ray(image_base,true,trace,*bridge)==NativeHandRayStatus::disabled && !create_calls,"Default created hooks");
        SetEnvironmentVariableW(L"SWVR_HAND_RAY",L"123456789012345");
        require(swvr::install_native_hand_ray(image_base,true,trace,*bridge)==NativeHandRayStatus::disabled && !create_calls,"Long environment value accepted");
        SetEnvironmentVariableW(L"SWVR_HAND_RAY",L"1");
        require(swvr::install_native_hand_ray(image_base,false,trace,*bridge)==NativeHandRayStatus::unsupported_profile && !create_calls,"SHA guard");
        image[0x74f260]^=1;
        require(swvr::install_native_hand_ray(image_base,true,trace,*bridge)==NativeHandRayStatus::signature_mismatch && !create_calls,"Signature guard");image[0x74f260]^=1;
        require(swvr::install_native_hand_ray(image_base,true,"relative",*bridge)==NativeHandRayStatus::io_failure,"Relative output accepted");
        const auto occupied=directory/(name+"-occupied");{std::ofstream f(occupied.wstring()+L".hand-ray-native.tsv");f<<"sentinel";}
        require(swvr::install_native_hand_ray(image_base,true,occupied,*bridge)==NativeHandRayStatus::io_failure && !create_calls,"Existing output overwritten");
        fail_create=2;
        require(swvr::install_native_hand_ray(image_base,true,directory/(name+"-failed"),*bridge,{&unexpected_observer,selection_complete})==NativeHandRayStatus::hook_failure && remove_calls==1,"Partial hooks not removed");
        fail_create=0;create_calls=0;
        require(swvr::install_native_hand_ray(image_base,true,trace,*bridge,{&observer,selection_complete})==NativeHandRayStatus::installed && create_calls==2,"Install did not create exactly two hooks");
        require(swvr::install_native_hand_ray(image_base,true,trace,*bridge,{&unexpected_observer,selection_complete})==NativeHandRayStatus::already_installed && create_calls==2,"Double installation");
        initialize_thunks(image);clear_controller();const auto native_bytes=forward_bytes();
        publish();const auto expected=bridge->inspect();const auto first_callback=observer.calls.load();
        call_selection(normal);assert_hand(observations.back(),expected);assert_callback(first_callback,normal,expected,true);
        require(last_callback().target==0x2222,"Observer did not receive the completed native target");
        require(forward_bytes()==native_bytes,"Forward bytes not restored exactly");
        require(get<std::uintptr_t>(0x520)==0x2222 && get<std::uintptr_t>(0x538)==0x4444,"Original Selection did not compute target");
        std::array<double,3> outside{};
        const auto before_outside=observer.calls.load();
        reinterpret_cast<Origin>(image_base+0x74f37d)(&character_token,outside.data(),0x81,0xfe);
        require(outside==native_head && forward_bytes()==native_bytes,"Origin outside Selection was changed");
        require(observer.calls==before_outside,"Origin alone invoked Selection completion");
        for(bool caller:{false,true}) {
            publish();const auto sample=bridge->inspect();const auto before_callback=observer.calls.load();
            call_selection(caller?wrong_origin:normal,caller);assert_native(observations.back());
            if(caller)assert_callback(before_callback,wrong_origin,sample,false,true,0);
            else require(observer.calls==before_callback,"Unknown Selection caller invoked the observer");
        }
        const auto calls_before=origin_calls.load();publish();const auto no_origin_sample=bridge->inspect();
        const auto before_no_origin=observer.calls.load();call_selection(no_origin);
        require(origin_calls==calls_before && forward_bytes()==native_bytes,"Early native return changed forward");
        assert_callback(before_no_origin,no_origin,no_origin_sample,false,true,0);
        publish();call_selection(alternate_return);assert_native(observations.back());require(!observations.back().returned_output,"Original return pointer not preserved");
        for(std::size_t offset:{0x494,0x49c,0x49f,0x4bc,0x4bd}) {
            clear_controller();publish();const auto sample=bridge->inspect();const auto before_callback=observer.calls.load();
            put<unsigned char>(offset,1);call_selection(normal);assert_native(observations.back());
            assert_callback(before_callback,normal,sample,false);
            require(forward_bytes()==native_bytes,"UI/death/sleep flag leaked mutation");
        }
        clear_controller();publish();application_data[0x233]=1;call_selection(normal);assert_native(observations.back());
        clear_controller();publish();application_data[0]^=1;call_selection(normal);assert_native(observations.back());
        clear_controller();publish();contexts[2]=0;call_selection(normal);assert_native(observations.back());
        clear_controller();publish();contexts[2]=1;call_selection(normal,false);assert_native(observations.back());
        for(Mode m:{benchmark_during_origin,application_changed_during_origin}) {
            clear_controller();publish();call_selection(m);assert_native(observations.back());
            require(forward_bytes()==native_bytes,"Application/benchmark transition leaked mutation");
        }
        clear_controller();publish();const auto bound=bridge->inspect();const auto before_publication=observer.calls.load();
        call_selection(publish_during_origin);assert_hand(observations.back(),bound);
        assert_callback(before_publication,publish_during_origin,bound,true);
        require(bridge->inspect().sequence>bound.sequence,"New-pose-during-selection fixture did not publish");
        for(Mode m:{focus_loss,reset_during_origin,cancelled_then_recovered,pause_during_origin,slow_origin}) {
            clear_controller();publish();const auto sample=bridge->inspect();const auto before_callback=observer.calls.load();
            call_selection(m);assert_native(observations.back());require(forward_bytes()==native_bytes,"Loss/stale restore");
            assert_callback(before_callback,m,sample,false);
        }
        clear_controller();publish({3.25f,0,0});call_selection(normal);assert_native(observations.back());
        publish({3,0,0});const auto boundary=bridge->inspect();call_selection(normal);assert_hand(observations.back(),boundary);
        publish();Sleep(110);call_selection(normal);assert_native(observations.back());
        auto stale=make_frame();bridge->publish(stale,swvr::Pose{},world_base(),swvr::HandInteractionClock::now()-std::chrono::milliseconds(101));
        call_selection(normal);assert_native(observations.back());
        {   // Gaze fallback: with no pointing hand the selection follows the head view,
            // and is never reported as a hand selection (it cannot certify a pinch).
            clear_controller();auto f=make_frame();f.hands[1].active=false;bridge->publish(f,swvr::Pose{},world_base());
            const auto gaze=bridge->inspect();require(!gaze.valid_ray&&gaze.valid_gaze,"gaze fixture");
            const auto before_gaze=observer.calls.load();call_selection(normal);
            const auto& o=observations.back();
            for(unsigned i=0;i<3;++i) {
                require(o.origin[i]==gaze.gaze.origin[i],"gaze origin not used for selection");
                require(std::abs(o.forward[i]-gaze.gaze.direction[i])<1e-7,"gaze direction not used for selection");
            }
            require(forward_bytes()==native_bytes,"gaze selection left the native forward modified");
            require(observer.calls==before_gaze+1&&!last_callback().used,"gaze selection reported as a hand selection");
        }
        bridge->reset();clear_controller();publish();bool exception_seen{};const auto exception_sample=bridge->inspect();
        const auto before_exception=observer.calls.load();
        try {call_selection(throw_selection);}catch(const std::runtime_error&){exception_seen=true;}
        require(exception_seen && forward_bytes()==native_bytes,"Native exception lost or forward not restored");
        assert_callback(before_exception,throw_selection,exception_sample,true,false);
        require(last_callback().target==0,"Incomplete Selection certified a partial target");
        reinterpret_cast<Origin>(image_base+0x74f37d)(&character_token,outside.data(),0x81,0xfe);
        require(outside==native_head,"Exception left a dangling TLS Selection scope");
        publish();const auto thread_bound=bridge->inspect();call_selection(foreign_thread_origin);assert_hand(observations.back(),thread_bound);
        for(Mode m:{nested,nested_wrong_caller}) {
            clear_controller();publish();const auto outer=bridge->inspect();const auto before=observations.size();
            const auto before_callback=observer.calls.load();call_selection(m);
            require(observations.size()==before+2,"Nested fixture call count");assert_hand(observations[before],outer);
            if(m==nested)assert_hand(observations[before+1],bridge->inspect());else assert_native(observations[before+1]);
            require(forward_bytes()==native_bytes,"Nested Selection leaked controller direction");
            assert_callback(before_callback,m,outer,true,true,1,true);
            require(last_callback().target==0x3333,"Observer did not report actual final target after nested overwrite");
        }
        {
            // Both threads use the same controller. The second attempt starts
            // while the first override is active; serialized callbacks must not
            // overlap and final bytes must be the original, not another ray.
            clear_controller();publish();const auto expected_concurrent=bridge->inspect();
            const auto before=observations.size();const auto before_callbacks=observer.calls.load();
            peak_native=0;first_override=false;second_entered=false;first_done=false;
            std::atomic<bool> worker_failed{};
            std::thread first([&] {
                try {call_selection(concurrent_hold);}catch(...){worker_failed=true;}
                first_done.store(true,std::memory_order_release);
            });
            std::thread second([&] {
                if(!wait_flag(first_override,std::chrono::milliseconds(500))){worker_failed=true;return;}
                try {call_selection(concurrent_second);}catch(...){worker_failed=true;}
            });
            first.join();second.join();
            require(!worker_failed && first_override && second_entered,"Concurrent Selection fixture failed");
            require(peak_native==1 && active_native==0,"Concurrent native Selection transactions overlapped");
            require(observations.size()==before+2,"Concurrent calls skipped original Selection");
            assert_hand(observations[before],expected_concurrent);assert_hand(observations[before+1],expected_concurrent);
            require(forward_bytes()==native_bytes,"Concurrent Selection restored another transaction's temporary forward");
            require(observer.calls==before_callbacks+2 && !observer.failed,"Migrated top-level calls lost observer or restoration");
        }
        clear_controller();publish();call_selection(nonfinite_head);
        require(std::isnan(observations.back().origin[0]) && forward_bytes()==native_bytes,"Nonfinite native head was overwritten");
        bridge->reset();publish({},.05f);publish({},.01f);call_selection(normal);
        require(bridge->consume().pinch==swvr::PinchEvent::began,"Pointing backend consumed a gesture transition");
        swvr::flush_native_hand_ray();
        std::ifstream stream(trace.wstring()+L".hand-ray-native.tsv");std::string line;std::vector<std::vector<std::string>> records;
        while(std::getline(stream,line))if(!line.empty() && line[0]!='#' && line.rfind("seq\t",0)!=0)records.push_back(fields(line));
        require(!records.empty() && records.front().size()==27 && records.front()[3]=="745a24" && records.front()[8]=="hand_ray" &&
                records.front()[20]=="2222" && records.front()[21]=="4444","Native hand diagnostic values missing");
        bool saw_cancel{},saw_stale{},saw_stale_entry{},saw_wrong{},saw_radius{},saw_benchmark{},saw_application{},saw_pinch{};
        for(const auto& r:records){saw_cancel|=r[8]=="snapshot_cancelled";saw_stale|=r[8]=="stale_at_use";
            saw_stale_entry|=r[8]=="stale";
            saw_wrong|=r[8]=="selection_caller";saw_radius|=r[8]=="hand_origin_outside_head_reach";
            saw_benchmark|=r[8]=="benchmark_mode";saw_application|=r[8]=="application_profile";
            saw_pinch|=r[25]!="0" && r[26]=="1";}
        require(saw_cancel&&saw_stale&&saw_stale_entry&&saw_wrong&&saw_radius&&saw_benchmark&&saw_application,"Blocked outcomes were not recorded");
        require(saw_pinch,"Source gesture identity and pinch state were not recorded");
        const auto before_cap=selection_calls.load();const auto callbacks_before_cap=observer.calls.load();
        for(unsigned i=0;i<10005;++i)call_selection(no_origin);
        require(selection_calls==before_cap+10005,"Diagnostic cap suppressed original work");
        require(observer.calls==callbacks_before_cap+10005,"Diagnostic cap suppressed Selection completion callbacks");
        clear_controller();publish();const auto after_cap=bridge->inspect();const auto before_after_cap=observer.calls.load();
        call_selection(normal);assert_hand(observations.back(),after_cap);assert_callback(before_after_cap,normal,after_cap,true);
        swvr::flush_native_hand_ray();std::ifstream capped(trace.wstring()+L".hand-ray-native.tsv");unsigned count{};bool limit{};
        while(std::getline(capped,line)){if(line=="#state\tlimit_reached")limit=true;else if(!line.empty()&&line[0]!='#'&&line.rfind("seq\t",0)!=0)++count;}
        require(count==10000 && limit,"Native ray log cap failed");
        require(swvr::rearm_native_hand_ray_log(),"Capped diagnostic log could not be rearmed");
        clear_controller();publish();const auto after_rearm=bridge->inspect();call_selection(normal);assert_hand(observations.back(),after_rearm);
        call_selection(no_origin);swvr::flush_native_hand_ray();
        std::ifstream rearmed(trace.wstring()+L".hand-ray-native.tsv");
        records.clear();bool rearm_marker{};
        while(std::getline(rearmed,line)) {
            if(line=="#epoch\t1\trearm\t10000")rearm_marker=true;
            else if(!line.empty()&&line[0]!='#'&&line.rfind("seq\t",0)!=0)records.push_back(fields(line));
        }
        require(rearm_marker && records.size()==10002,"Rearm replaced the old log or failed to append new records");
        require(records[9999][0]=="10000" && records[9999][23]=="0" && records[9999][24]=="10000" &&
                records[10000][0]=="10001" && records[10000][23]=="1" && records[10000][24]=="1" &&
                records[10001][0]=="10002" && records[10001][23]=="1" && records[10001][24]=="2",
                "Rearm lost monotonic sequence or failed to reset only epoch rows");
        require(records[10000][8]=="hand_ray","Pointing behavior did not survive diagnostic rearm");
        require(!unexpected_hook_calls && create_calls==2,"Backend attempted an action or unexpected hook");
        require(forward_bytes()==native_bytes,"Final controller direction differs byte-for-byte");
        require(!observer.failed && !unexpected_observer.calls,"Installation replaced or leaked callback state");
        test_default_observer_child();
        RtlDeleteFunctionTable(thunk_functions.data());VirtualFree(image,0,MEM_RELEASE);SetEnvironmentVariableW(L"SWVR_HAND_RAY",nullptr);
        std::cout<<"Native hand ray tests passed: ABI/guards, scoped world ray, exact restore, cancellation/freshness, nested calls and no action hooks.\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
