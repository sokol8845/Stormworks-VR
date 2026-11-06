#include "swvr/tracking.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace swvr;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a,float b) { return std::abs(a-b)<1e-5f; }
HandSample hand(float gap, std::int64_t t) { return {true,true,true,true,t,{},{gap,0,0}}; }
int main() {
    try {
        const float s = std::sqrt(0.5f);
        const Pose seat{{0,s,0,s},{10,2,3}};
        const Pose origin{{},{1,1.5f,2}};
        const std::array<Pose,2> eyes{{{{},{0.968f,1.5f,2}},{{},{1.032f,1.5f,2}}}};
        const auto views = cockpit_views(seat,origin,eyes);
        const auto center=stereo_center(eyes);
        check(near(center.position.x,1) && near(center.position.y,1.5f),"Calibration must center both eyes");
        const auto antipodal=stereo_center({Pose{{0,s,0,s},{}},Pose{{0,-s,0,-s},{}}});
        check(near(antipodal.orientation.y,s) && near(antipodal.orientation.w,s),"Quaternion sign changed calibration");
        const float cant=.1f;
        const auto canted=stereo_center({Pose{{0,std::sin(cant),0,std::cos(cant)},{}},
                                        Pose{{0,-std::sin(cant),0,std::cos(cant)},{}}});
        check(near(canted.orientation.y,0) && near(canted.orientation.w,1),"Canted views must calibrate at midpoint");
        check(near(views[0].position.x,10) && near(views[0].position.z,3.032f),"Left eye rotated about seat");
        check(near(views[1].position.z,2.968f),"Right eye preserves IPD");
        const auto identity = compose(inverse(seat),seat);
        check(near(identity.position.x,0) && near(identity.position.y,0) && near(identity.position.z,0)
              && near(identity.orientation.w,1),"Pose inverse");
        const auto recentered = cockpit_views(seat,seat,{seat,seat});
        check(near(recentered[0].position.x,10),"Recenter identity");
        const auto view=view_matrix(seat);
        check(near(view[0]*10+view[4]*2+view[8]*3+view[12],0),"View transforms eye to origin X");
        check(near(view[1]*10+view[5]*2+view[9]*3+view[13],0),"View transforms eye to origin Y");
        check(near(view[2]*10+view[6]*2+view[10]*3+view[14],0),"View transforms eye to origin Z");
        const Fov fov{-.7f,.8f,.9f,-.6f};
        const auto proj=projection(fov,.1f,100.f);
        const auto ndc=[&](float x,float y,float z,int row) {
            return (proj[row]*x+proj[4+row]*y+proj[8+row]*z+proj[12+row])/(-z);
        };
        check(near(ndc(std::tan(fov.left),0,-1,0),-1),"Asymmetric left FOV boundary");
        check(near(ndc(std::tan(fov.right),0,-1,0),1),"Asymmetric right FOV boundary");
        check(near(ndc(0,std::tan(fov.up),-1,1),1),"Upper FOV boundary");
        check(near(ndc(0,std::tan(fov.down),-1,1),-1),"Lower FOV boundary");
        check(near(ndc(0,0,-.1f,2),-1) && near(ndc(0,0,-100.f,2),1),"OpenGL clip depth");
        PinchDetector p;
        check(p.update(hand(.01f,1),1)==PinchEvent::none,"Closed hand must not arm");
        check(p.update(hand(.05f,2),2)==PinchEvent::none,"Open to arm");
        check(p.update(hand(.02f,3),3)==PinchEvent::began,"Pinch begins");
        check(p.update(hand(.03f,4),4)==PinchEvent::held,"Hysteresis prevents chatter");
        check(p.update(hand(.03f,4),4)==PinchEvent::none,"Duplicate sample");
        auto lost = hand(.02f,5); lost.position_tracked=false;
        check(p.update(lost,5)==PinchEvent::cancelled,"Tracking loss cancels instead of clicking");
        check(p.update(hand(.02f,6),6)==PinchEvent::none,"Recovery cannot trigger pinch");
        p.update(hand(.05f,7),7); p.update(hand(.02f,8),8);
        check(p.update(hand(.05f,9),9)==PinchEvent::released,"Normal release");
        p.update(hand(.02f,10),10);
        check(p.update(hand(.02f,11),200'000'000)==PinchEvent::cancelled,"Stale data cancels");
        p.update(hand(.05f,12),12); p.update(hand(.02f,13),13);
        auto unfocused=hand(.02f,14); unfocused.focused=false;
        check(p.update(unfocused,14)==PinchEvent::cancelled,"Focus loss cancels");
        p.update(hand(.05f,15),15); p.update(hand(.02f,16),16);
        check(p.update(hand(.02f,15),17)==PinchEvent::cancelled,"Time reversal cancels");
        auto invalid=hand(std::numeric_limits<float>::quiet_NaN(),18);
        check(p.update(invalid,18)==PinchEvent::none,"NaN cannot arm");
        bool rejected=false;
        try { PinchDetector bad(.04f,.02f); } catch(const std::invalid_argument&) { rejected=true; }
        check(rejected,"Threshold validation");
        std::cout << "PASS: cockpit transforms, stereo offsets, pinch hysteresis and loss handling\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
