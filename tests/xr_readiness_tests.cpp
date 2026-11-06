#include "swvr/xr_readiness.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
swvr::XrRuntimeFrame ready() {
    swvr::XrRuntimeFrame frame;
    frame.id=1;frame.running=frame.focused=frame.should_render=true;
    frame.position_valid=frame.orientation_valid=frame.position_tracked=frame.orientation_tracked=true;
    return frame;
}
}
int main(){try{
    using namespace std::chrono;
    using R=swvr::XrReadiness;
    const auto epoch=swvr::XrReadinessMonitor::Clock::time_point{};
    for(const auto cause:{R::session,R::focus,R::tracking,R::render}) {
        swvr::XrReadinessMonitor monitor;auto frame=ready();
        switch(cause) {
        case R::session:frame.running=false;break;
        case R::focus:frame.focused=false;break;
        case R::tracking:frame.position_tracked=false;break;
        case R::render:frame.should_render=false;break;
        default:break;
        }
        require(!monitor.observe(frame,epoch),"startup should be quiet");
        require(!monitor.observe(frame,epoch+milliseconds(4999)),"warning too early");
        auto n=monitor.observe(frame,epoch+seconds(5));
        require(n&&n->reason==cause&&n->waited==seconds(5),"missing cause at deadline");
        require(!monitor.observe(frame,epoch+seconds(34)),"warning spam");
        require(!monitor.observe(frame,epoch+seconds(1)),"clock reversal emitted notice");
        n=monitor.observe(frame,epoch+seconds(35));
        require(n&&n->waited==seconds(35),"repeat deadline changed");
        n=monitor.observe(ready(),epoch+seconds(36));
        require(n&&n->reason==R::ready&&n->waited==seconds(36),"missing recovery");
        require(!monitor.observe(ready(),epoch+seconds(37)),"duplicate recovery");
    }
    // Changing causes gets its own deadline, while still reporting recovery.
    swvr::XrReadinessMonitor monitor;auto frame=ready();frame.focused=false;
    monitor.observe(frame,epoch);require(monitor.observe(frame,epoch+seconds(5)).has_value(),"focus warning");
    frame.focused=true;frame.orientation_valid=false;
    require(!monitor.observe(frame,epoch+seconds(6)),"cause change warned immediately");
    require(!monitor.observe(frame,epoch+seconds(10)),"old deadline reused");
    auto n=monitor.observe(frame,epoch+seconds(11));require(n&&n->reason==R::tracking,"invalid orientation not diagnosed");
    n=monitor.observe(ready(),epoch+seconds(12));require(n&&n->reason==R::ready,"changed-cause recovery");
    swvr::XrReadinessMonitor brief;
    frame=ready();frame.id=0;brief.observe(frame,epoch);
    require(!brief.observe(ready(),epoch+seconds(1)),"brief interruption reports unnecessary recovery");
    // Fully hidden/stopped frames prioritize session/focus over renderability.
    swvr::XrReadinessMonitor hidden;frame={};hidden.observe(frame,epoch);
    n=hidden.observe(frame,epoch+seconds(5));require(n&&n->reason==R::session,"stopped frame priority");
    frame.running=true;hidden.observe(frame,epoch+seconds(6));
    n=hidden.observe(frame,epoch+seconds(11));require(n&&n->reason==R::focus,"hidden unfocused frame priority");
    std::cout<<"PASS: XR readiness causes, warning deadlines, recovery and clock reversal\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
