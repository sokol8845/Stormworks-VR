#include "swvr/native_keypad_request.hpp"
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace swvr {
namespace {
void require(bool good,const char* why){if(!good)throw std::invalid_argument(why);}
bool fresh(const NativeKeypadSession& s,NativeKeypadSession::Clock::time_point now){
    return s.recognized&&s.visible&&s.focused&&s.generation&&s.owner&&s.world&&s.actor&&s.ui&&s.target&&s.vehicle&&
        (s.button_type==5||s.button_type==6)&&std::isfinite(s.current[0])&&std::isfinite(s.current[1])&&
        now>=s.observed_at&&now-s.observed_at<=std::chrono::milliseconds(100);
}
bool same(const NativeKeypadSession& a,const NativeKeypadSession& b){return a.generation==b.generation&&a.owner==b.owner&&a.world==b.world&&a.actor==b.actor&&a.ui==b.ui&&a.target==b.target&&a.vehicle==b.vehicle&&a.section==b.section&&a.grid==b.grid&&a.button_type==b.button_type&&std::bit_cast<std::uint32_t>(a.current[0])==std::bit_cast<std::uint32_t>(b.current[0])&&std::bit_cast<std::uint32_t>(a.current[1])==std::bit_cast<std::uint32_t>(b.current[1]);}
}
float parse_keypad_decimal(std::string_view text){
    require(text.size()<=32,"Keypad value too long");if(text.empty())return 0.f;
    bool digit{},dot{};for(std::size_t i=0;i<text.size();++i){const char c=text[i];if(c>='0'&&c<='9'){digit=true;continue;}if(c=='-'&&i==0)continue;if(c=='.'&&!dot){dot=true;continue;}throw std::invalid_argument("Expected ASCII decimal");}
    require(digit,"Keypad value has no digits");float value{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&std::isfinite(value),"Invalid finite keypad value");return value;
}
NativeKeypadSubmission::NativeKeypadSubmission(bool sha,NativeKeypadApi api):api_(api){
    require(sha,"Keypad adapter requires full executable SHA proof");require(api.image_base&&api.image_base<=std::numeric_limits<std::uintptr_t>::max()-keypad_request_vtable_rva&&api.request_vtable==api.image_base+keypad_request_vtable_rva&&api.allocate&&api.destroy_unsubmitted&&api.enqueue&&api.validate_live,"Invalid keypad native port");
}
void NativeKeypadSubmission::prepare(const NativeKeypadSession& current,std::string_view first,std::optional<std::string_view> second,NativeKeypadSession::Clock::time_point now){
    require(state_==KeypadSubmissionState::empty,"Keypad submission already used");require(fresh(current,now),"Unrecognized or stale keypad session");
    const std::array<float,2> values{parse_keypad_decimal(first),current.button_type==5?current.current[1]:(require(second.has_value(),"Large keypad requires both channels"),parse_keypad_decimal(*second))};
    prepare_values(current,values,now);
}
void NativeKeypadSubmission::prepare_values(const NativeKeypadSession& current,std::array<float,2> values,NativeKeypadSession::Clock::time_point now){
    require(state_==KeypadSubmissionState::empty&&fresh(current,now),"Unrecognized or stale keypad session");
    if(current.button_type==5)values[1]=current.current[1];
    require(std::isfinite(values[0])&&std::isfinite(values[1]),"Keypad requires finite values");
    certificate_=current;
    try{for(unsigned channel=0;channel<2;++channel){requests_[channel]=static_cast<NativeKeypadRequest*>(api_.allocate(api_.context,sizeof(NativeKeypadRequest)));if(!requests_[channel])throw std::runtime_error("Keypad native allocation failed");
        *requests_[channel]={api_.request_vtable,11,0,current.vehicle,current.section,current.grid,values[channel],static_cast<std::int32_t>(channel),0};}
        require(api_.validate_live(api_.context,certificate_),"Keypad session changed during allocation");state_=KeypadSubmissionState::prepared;
    }catch(...){state_=KeypadSubmissionState::partial;discard_remaining();throw;}
}
void NativeKeypadSubmission::submit(const NativeKeypadSession& current,void* queue,NativeKeypadSession::Clock::time_point now){
    require(state_==KeypadSubmissionState::prepared,"Keypad handoff may not be retried");require(queue&&fresh(current,now)&&same(current,certificate_),"Keypad live session changed");
    for(unsigned channel=0;channel<2;++channel){
        if(!api_.validate_live(api_.context,certificate_)){if(channel)state_=KeypadSubmissionState::partial;throw std::runtime_error("Keypad live scope invalidated before handoff");}
        auto* request=requests_[channel];requests_[channel]=nullptr;state_=KeypadSubmissionState::uncertain;
        api_.enqueue(api_.context,queue,request);state_=channel?KeypadSubmissionState::submitted:KeypadSubmissionState::partial;
    }
}
void NativeKeypadSubmission::discard_remaining(){
    bool failed{};for(auto& slot:requests_)if(slot){auto* request=slot;slot=nullptr;try{api_.destroy_unsubmitted(api_.context,request);}catch(...){failed=true;}}
    if(failed){state_=KeypadSubmissionState::uncertain;throw std::runtime_error("Keypad discard outcome uncertain");}
    if(state_==KeypadSubmissionState::empty||state_==KeypadSubmissionState::prepared)state_=KeypadSubmissionState::discarded;
}
}
