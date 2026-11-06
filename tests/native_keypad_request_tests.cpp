#include "swvr/native_keypad_request.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace swvr;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F f){bool bad{};try{f();}catch(const std::exception&){bad=true;}check(bad,"unsafe keypad operation accepted");}
struct Port {
    std::vector<NativeKeypadRequest> sent;
    unsigned allocated{},freed{},validation{},fail_allocate{},throw_push{},invalidate_after{};bool live{true};
    static void* allocate(void* c,std::size_t size){auto& p=*static_cast<Port*>(c);check(size==48,"wrong request size");if(++p.allocated==p.fail_allocate)return nullptr;return new NativeKeypadRequest;}
    static void destroy(void* c,NativeKeypadRequest* request){++static_cast<Port*>(c)->freed;delete request;}
    static void enqueue(void* c,void* queue,NativeKeypadRequest* request){auto& p=*static_cast<Port*>(c);check(queue==&p,"queue substituted");p.sent.push_back(*request);destroy(c,request);if(p.throw_push==p.sent.size())throw std::runtime_error("uncertain handoff");}
    static bool validate(void* c,const NativeKeypadSession&){auto& p=*static_cast<Port*>(c);++p.validation;return p.live&&(!p.invalidate_after||p.validation<p.invalidate_after);}
    NativeKeypadApi api(){return {this,0x140000000,0x140b11460,allocate,destroy,enqueue,validate};}
};
NativeKeypadSession session(){NativeKeypadSession s;s.generation=1;s.owner=1;s.world=2;s.actor=3;s.ui=4;s.target=5;s.vehicle=7;s.section=8;s.grid={9,-10,11};s.button_type=6;s.current={1.5f,-2.f};s.observed_at=NativeKeypadSession::Clock::now();s.recognized=s.visible=s.focused=true;return s;}
int main(){try{
    check(parse_keypad_decimal("")==0&&parse_keypad_decimal("-.5")==-.5f,"decimal input");for(const char* value:{"nan","inf","1e3","1x","1,2","--1",".","+1"," 1","9999999999999999999999999999999999999999"})rejects([&]{parse_keypad_decimal(value);});
    {Port p;rejects([&]{NativeKeypadSubmission invalid(false,p.api());});check(!p.allocated,"false SHA invoked native port");}
    for(unsigned type:{5u,6u}){Port p;auto s=session();s.button_type=type;NativeKeypadSubmission batch(true,p.api());batch.prepare(s,"12.25",std::string_view("-8"));check(p.sent.empty()&&p.allocated==2,"enqueue before full preparation");batch.submit(s,&p);
        check(p.sent.size()==2&&p.sent[0].type==11&&p.sent[0].metadata==0&&p.sent[0].vehicle_id==7&&p.sent[0].section_index==8&&p.sent[0].grid==s.grid&&p.sent[0].channel==0&&p.sent[1].channel==1&&p.sent[0].value==12.25f&&p.sent[1].value==(type==5?-2.f:-8.f),"native keypad protocol/channel preservation wrong");
        rejects([&]{batch.submit(s,&p);});batch.discard_remaining();check(p.freed==2,"submitted request was destroyed twice");}
    {Port p;auto s=session();NativeKeypadSubmission batch(true,p.api());rejects([&]{batch.prepare(s,"1",{});});check(!p.allocated,"large keypad accepted an absent second field");}
    for(unsigned fail:{1u,2u}){Port p;p.fail_allocate=fail;auto s=session();NativeKeypadSubmission batch(true,p.api());rejects([&]{batch.prepare(s,"1",std::string_view("2"));});check(p.sent.empty()&&p.freed==fail-1,"partial allocation leaked");batch.discard_remaining();}
    for(unsigned uncertain:{1u,2u}){Port p;p.throw_push=uncertain;auto s=session();NativeKeypadSubmission batch(true,p.api());batch.prepare(s,"1",std::string_view("2"));rejects([&]{batch.submit(s,&p);});rejects([&]{batch.submit(s,&p);});batch.discard_remaining();check(p.freed==2&&p.sent.size()==uncertain,"uncertain handoff was retried or double-freed");}
    {Port p;auto s=session();NativeKeypadSubmission batch(true,p.api());batch.prepare(s,"1",std::string_view("2"));auto changed=s;++changed.generation;rejects([&]{batch.submit(changed,&p);});batch.discard_remaining();check(p.freed==2&&p.sent.empty(),"stale Show session submitted");}
    {Port p;auto s=session();NativeKeypadSubmission batch(true,p.api());batch.prepare(s,"1",std::string_view("2"));p.invalidate_after=3;rejects([&]{batch.submit(s,&p);});batch.discard_remaining();check(p.sent.size()==1&&p.freed==2&&batch.state()==KeypadSubmissionState::partial,"mid-pair invalidation retried or leaked");}
    {Port p;auto s=session();NativeKeypadSubmission batch(true,p.api());batch.prepare(s,"1",std::string_view("2"));batch.discard_remaining();check(p.sent.empty()&&p.freed==2&&batch.state()==KeypadSubmissionState::discarded,"cancel submitted native data");}
    for(int field=0;field<10;++field){Port p;auto s=session();NativeKeypadSubmission batch(true,p.api());batch.prepare(s,"1",std::string_view("2"));auto changed=s;
        if(field==0)++changed.owner;if(field==1)++changed.world;if(field==2)++changed.actor;if(field==3)++changed.ui;if(field==4)++changed.target;
        if(field==5)++changed.vehicle;if(field==6)++changed.section;if(field==7)++changed.grid[0];if(field==8)changed.visible=false;if(field==9)changed.focused=false;
        rejects([&]{batch.submit(changed,&p);});batch.discard_remaining();check(p.sent.empty()&&p.freed==2,"foreign UI/actor or closed session submitted");}
    {Port p;auto s=session();NativeKeypadSubmission batch(true,p.api());rejects([&]{batch.prepare(s,"1",std::string_view("2"),s.observed_at+std::chrono::milliseconds(101));});check(!p.allocated,"stale snapshot allocated requests");}
    std::cout<<"PASS keypad integration port: type11/two channels, live session, finite decimal, cancellation, allocation and uncertain handoff\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
