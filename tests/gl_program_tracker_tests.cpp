#include "swvr/gl_program_tracker.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace swvr;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
using Tracker=GlProgramTracker<3>;
std::array<GlProgramInterest,3> answers{};
std::array<unsigned,3> asked{};
void* asked_context{};unsigned asked_program{};
GlProgramInterest query(std::size_t consumer,void* context,unsigned program) noexcept {
    ++asked[consumer];asked_context=context;asked_program=program;return answers[consumer];
}
unsigned questions(){return asked[0]+asked[1]+asked[2];}
void script(GlProgramInterest a,GlProgramInterest b,GlProgramInterest c){answers={a,b,c};asked={};}
int first{},second{};
void* const context=&first;void* const other=&second;
constexpr auto unknown=GlProgramInterest::unknown,none=GlProgramInterest::none,active=GlProgramInterest::active;

void binding_tests(){
    Tracker tracker;script(none,none,none);
    require(!tracker.current(context,1,1,query)&&!tracker.current(nullptr,1,1,query)&&!questions(),"an unbound tracker claimed knowledge");
    script(none,active,unknown);tracker.bind(context,7,1,1,query);
    require(asked==std::array<unsigned,3>{1,1,1}&&asked_context==context&&asked_program==7,"bind did not ask each consumer once about the bound program");
    require(tracker.current(context,1,1,query)&&tracker.program()==7,"bound program not reported for its context");
    require(!tracker.wants(0)&&tracker.wants(1)&&tracker.wants(2),"none must skip; active and unknown must call");
    require(!tracker.current(other,1,1,query)&&!tracker.current(nullptr,1,1,query),"another or missing context reused the binding");
    require(tracker.current(context,1,1,query)&&questions()==3,"looking at another context lost or re-asked the binding");
    // Re-binding the identical program keeps what was learnt.
    tracker.bind(context,7,1,1,query);require(questions()==3&&!tracker.wants(0),"identical re-bind asked again");
    script(active,none,none);tracker.bind(context,8,1,1,query);
    require(questions()==3&&tracker.program()==8&&tracker.wants(0)&&!tracker.wants(1)&&!tracker.wants(2),"a different program kept the previous answers");
    // glUseProgram(0), or a rejected first bind: nothing can be drawn with it.
    script(active,active,active);tracker.bind(context,0,1,1,query);
    require(!questions()&&tracker.current(context,1,1,query)&&!tracker.wants(0)&&!tracker.wants(1)&&!tracker.wants(2),"no program must skip every consumer without asking");
    tracker.bind(nullptr,5,1,1,query);require(!tracker.current(nullptr,1,1,query)&&!tracker.current(context,1,1,query),"a bind without a context was trusted");
    script(none,none,none);tracker.bind(context,5,1,1,query);tracker.forget();
    require(!tracker.current(context,1,1,query),"forget kept the binding");
}
void learning_tests(){
    Tracker tracker;script(unknown,unknown,active);tracker.bind(context,7,1,1,query);
    // A consumer call may have created its record: ask once more.
    answers[0]=none;tracker.taught(0,query);
    require(asked[0]==2&&!tracker.wants(0),"a consumer that learnt the program is irrelevant was not skipped");
    tracker.taught(0,query);require(asked[0]==2,"a settled consumer was asked again");
    // Still unknown after a teaching call: keep calling, stop asking.
    tracker.taught(1,query);require(asked[1]==2&&tracker.wants(1),"a still-unknown consumer must keep being called");
    answers[1]=none;tracker.taught(1,query);
    require(asked[1]==2&&tracker.wants(1),"an unknown consumer was asked a third time within one binding");
    tracker.taught(2,query);require(asked[2]==1&&tracker.wants(2),"an active consumer was asked again");
    // The next bind of that program starts over and now gets the answer.
    tracker.bind(context,9,1,1,query);tracker.bind(context,7,1,1,query);
    require(!tracker.wants(1),"a new binding did not re-ask the consumer");
}
void invalidation_tests(){
    Tracker tracker;script(none,none,none);tracker.bind(context,7,1,1,query);
    // A consumer cache changed (link, new uniform location): same binding, new answers.
    script(active,none,unknown);
    require(tracker.current(context,1,2,query)&&asked==std::array<unsigned,3>{1,1,1},"cache invalidation did not re-ask");
    require(tracker.wants(0)&&!tracker.wants(1)&&tracker.wants(2),"cache invalidation kept stale answers");
    require(tracker.current(context,1,2,query)&&questions()==3,"an unchanged cache generation re-asked");
    answers[2]=none;tracker.taught(2,query);require(!tracker.wants(2),"re-evaluation did not allow learning again");
    // The binding itself became untrustworthy (context deleted, handle reusable).
    script(none,none,none);
    require(!tracker.current(context,2,2,query)&&!questions(),"a stale binding generation was trusted");
    require(!tracker.current(context,2,2,query)&&!tracker.current(context,1,2,query),"a dropped binding came back without a bind");
    tracker.bind(context,7,2,2,query);require(tracker.current(context,2,2,query)&&!tracker.wants(0),"bind after invalidation failed");
    // Same program bound again after either generation moved must be re-asked.
    script(active,active,active);tracker.bind(context,7,2,3,query);require(questions()==3&&tracker.wants(0),"re-bind ignored a cache generation change");
    script(none,none,none);tracker.bind(context,7,3,3,query);require(questions()==3&&!tracker.wants(0),"re-bind ignored a binding generation change");
}
void validation_tests(){
    Tracker tracker;unsigned due{};
    for(unsigned i=1;i<=1024;++i)if(tracker.validation_due()){++due;require(i%256==0,"validation fell off its 256-call cadence");}
    require(due==4,"validation must be due once per 256 calls");
}
}
int main(){
    try{binding_tests();learning_tests();invalidation_tests();validation_tests();std::cout<<"GL program tracker binding, learning, invalidation and validation cadence passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
