#include "swvr/gesture_control.hpp"
#include <iostream>
#include <stdexcept>
using namespace swvr;using namespace std::chrono;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){try{
    const auto now=GestureChangeQueue::Clock::time_point{};GestureChangeQueue q;
    GestureChange change;change.mode=LocomotionMode::arm_swing;change.settings.arm_swing.start_half_strokes=4;
    q.request(change,now);check(!q.take()&&q.poll(true,false,now+seconds(1))==GestureChangeStatus::waiting,"live hold allowed a mode change");
    bool rejected{};try{q.request(change,now+seconds(2));}catch(const std::logic_error&){rejected=true;}check(rejected,"new request reset a pending deadline");
    check(q.poll(false,false,now+seconds(2))==GestureChangeStatus::ready,"released movement did not permit change");
    const auto applied=q.take();check(applied&&applied->mode==LocomotionMode::arm_swing&&applied->settings.arm_swing.start_half_strokes==4&&!q.take(),"change lost settings or replayed");
    q.request(change,now);check(q.poll(true,false,now+seconds(5))==GestureChangeStatus::timeout&&!q.take()&&!q.pending(),"timeout applied a change");
    q.request(change,now);check(q.poll(false,true,now+seconds(1))==GestureChangeStatus::fault&&!q.take(),"native fault applied a change");
    auto bad=change;bad.settings.joystick.dead_zone_m=1;rejected=false;try{q.request(bad,now);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&!q.pending(),"invalid settings replaced state");
    q.request(change,now);check(q.poll(false,false,now-milliseconds(1))==GestureChangeStatus::timeout,"clock reversal unlocked update");
    std::cout<<"PASS gesture changes: drain barrier, settings, no replay, invalid input, deadline and fault\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
