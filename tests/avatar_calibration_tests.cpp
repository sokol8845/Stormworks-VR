#include "swvr/avatar_calibration.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
}
int main(){try{
    using namespace std::chrono;
    const auto epoch=swvr::AvatarCalibrationHold::Clock::time_point{};
    const swvr::AvatarCalibrationContext context{7,3,swvr::AvatarVariant::male};
    auto fill=[&](swvr::AvatarCalibrationHold& hold,int first,int last,auto ctx){
        for(int t=first;t<=last;t+=100)require(!hold.update(true,ctx,epoch+milliseconds(t)).request,"early calibration");
    };
    swvr::AvatarCalibrationHold hold;
    fill(hold,0,1400,context);
    auto result=hold.update(true,context,epoch+milliseconds(1500));
    require(result.progress==1.f,"completed T-pose progress missing");
    require(result.holding&&result.request&&hold.holds()==1&&hold.longest()==milliseconds(1500),"full hold not accepted");
    hold.accepted();
    require(!hold.update(true,context,epoch+milliseconds(1600)).request,"same hold repeated");
    require(!hold.update(false,context,epoch+milliseconds(1900)).holding,"latched hold rearmed during short dropout");
    require(!hold.update(true,context,epoch+milliseconds(1901)).request&&hold.holds()==2,"long dropout did not rearm");

    // Brief losses count toward the hold, but cannot trigger while invalid.
    swvr::AvatarCalibrationHold brief;fill(brief,0,1300,context);
    const auto progress=brief.update(false,context,epoch+milliseconds(1400)).progress;
    require(progress>.86f&&progress<.87f,"tracking gap fabricated valid progress");
    require(!brief.update(false,context,epoch+milliseconds(1500)).request,"invalid sample calibrated");
    require(brief.update(true,context,epoch+milliseconds(1600)).request,"300 ms dropout broke hold");

    // No invalid frames arrive when rendering is suspended. The first valid
    // frame afterwards must still reset, instead of accepting elapsed wall time.
    swvr::AvatarCalibrationHold suspended;fill(suspended,0,1200,context);
    result=suspended.update(true,context,epoch+seconds(20));
    require(result.holding&&!result.request&&suspended.holds()==2&&suspended.longest()==milliseconds(1200),"render suspension counted as T-pose");
    fill(suspended,20100,21400,context);
    require(suspended.update(true,context,epoch+milliseconds(21500)).request,"fresh hold after suspension failed");

    for(int change=0;change<3;++change) {
        swvr::AvatarCalibrationHold changed;fill(changed,0,1400,context);auto next=context;
        if(change==0)++next.actor;
        if(change==1)++next.space_revision;
        if(change==2)next.variant=swvr::AvatarVariant::female;
        require(!changed.update(true,next,epoch+milliseconds(1500)).request&&changed.holds()==2,"context change inherited hold");
    }
    swvr::AvatarCalibrationHold reset;fill(reset,0,1400,context);reset.reset();
    require(!reset.update(true,context,epoch+milliseconds(1500)).request,"session reset inherited hold");
    auto no_actor=context;no_actor.actor=0;
    require(!reset.update(true,no_actor,epoch+seconds(5)).holding,"missing actor armed calibration");
    swvr::AvatarCalibrationHold reversed;fill(reversed,0,1400,context);
    require(!reversed.update(true,context,epoch+milliseconds(100)).request,"clock reversal requested calibration");
    require(reversed.update(true,context,epoch+milliseconds(1500)).request,"clock reversal damaged valid hold");
    swvr::AvatarTargets targets;
    require(std::string(swvr::avatar_calibration_hint(targets,"hand_not_tracked"))=="POKAZ LEWA DLON","missing left hand hint wrong");
    targets.hands[0]=swvr::Matrix4d{};
    require(std::string(swvr::avatar_calibration_hint(targets,"hand_not_tracked"))=="POKAZ PRAWA DLON","missing right hand hint wrong");
    std::cout<<"PASS: T-pose deadlines, tracking gaps, render suspension, identity and LOCAL changes\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
