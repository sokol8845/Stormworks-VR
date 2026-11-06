#include "steering_fixture.hpp"
#include <iostream>
#include <stdexcept>
using namespace swvr;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
struct Fixture {HandSteering steering;GestureSettings settings;std::uint64_t id{};HandSteering::Clock::time_point now{};std::uintptr_t seat{42};
    SteeringCommand step(float angle,bool left=true,bool right=true,bool allowed=true){auto f=steering_fixture::hands(angle,left,right);f.id=++id;f.predicted_display_time_ns+=static_cast<std::int64_t>(id)*20'000'000;now+=std::chrono::milliseconds(20);steering.publish(f,settings,seat,allowed,now);return steering.inspect(now);}
    void grip(){step(0,false,false);check(step(0).active,"two-hand grip did not arm");}
};
int main(){try{
    {Fixture f;check(!f.step(0).active,"closed hands on entry grabbed without opening");f.grip();for(int i=1;i<=15;++i)f.step(i*.05f);check(f.steering.inspect(f.now).axis>.35f,"clockwise turn did not steer right");
     const float before=f.steering.inspect(f.now).axis;auto s=f.step(.75f,false,true);check(s.active&&!s.hands[0]&&s.hands[1]&&std::abs(s.axis-before)<.05f,"releasing one hand jumped steering");
     for(int i=1;i<=8;++i)f.step(.75f+i*.05f,false,true);check(f.steering.inspect(f.now).axis>before,"one-hand steering failed");
     s=f.step(1.15f,false,false);check(!s.active&&s.axis==0,"both hands did not release");}
    {Fixture f;f.grip();for(int i=1;i<=15;++i)f.step(-i*.05f);check(f.steering.inspect(f.now).axis<-.35f,"left steering sign");f.step(-.75f,true,true,false);check(!f.steering.inspect(f.now).active&&!f.step(-.75f).active,"modal suspension reused a closed grip");}
    {Fixture f;f.grip();check(!f.step(2.f).active,"tracking jump generated a steering impulse");check(!f.step(0).active,"tracking jump rearmed closed hands");}
    {Fixture f;f.grip();check(!f.steering.inspect(f.now+std::chrono::milliseconds(101)).active,"stale steering stayed held");}
    {Fixture f;f.grip();auto sample=steering_fixture::hands(0);sample.id=f.id;sample.predicted_display_time_ns+=static_cast<std::int64_t>(f.id)*20'000'000;
     for(int i=0;i<6;++i){f.now+=std::chrono::milliseconds(20);f.steering.publish(sample,f.settings,42,true,f.now);}check(!f.steering.inspect(f.now).active,"duplicate samples refreshed stale steering");}
    {Fixture f;f.grip();f.seat=43;check(!f.step(0).active,"seat change reused grip");}
    {Fixture f;f.grip();auto sample=steering_fixture::hands(0);sample.id=f.id;sample.predicted_display_time_ns+=static_cast<std::int64_t>(f.id)*20'000'000;sample.focused=false;
     f.steering.publish(sample,f.settings,42,true,f.now);check(!f.steering.inspect(f.now).active,"duplicate unfocused frame retained grip");}
    {Fixture f;f.grip();auto sample=steering_fixture::hands(0);sample.id=++f.id;sample.predicted_display_time_ns+=static_cast<std::int64_t>(f.id)*20'000'000;sample.hands[0].active=false;
     f.now+=std::chrono::milliseconds(20);f.steering.publish(sample,f.settings,42,true,f.now);auto s=f.steering.inspect(f.now);check(s.active&&!s.hands[0]&&s.hands[1],"loss of one hand cancelled the other hand");}
    std::cout<<"PASS steering grip, rotation, release, tracking, focus and seat lifecycle\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
