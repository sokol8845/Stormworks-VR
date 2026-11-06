#include "swvr/numeric_keypad.hpp"
#include "swvr/native_keypad_request.hpp"
#include <charconv>
#include <cmath>
namespace swvr {
const std::array<KeypadButton,16>& numeric_keypad_buttons() noexcept {
    static constexpr std::array<KeypadButton,16> buttons{{
        {KeypadKey::seven,"7",-.14f,.14f,.12f,.065f},{KeypadKey::eight,"8",0,.14f,.12f,.065f},{KeypadKey::nine,"9",.14f,.14f,.12f,.065f},
        {KeypadKey::four,"4",-.14f,.06f,.12f,.065f},{KeypadKey::five,"5",0,.06f,.12f,.065f},{KeypadKey::six,"6",.14f,.06f,.12f,.065f},
        {KeypadKey::one,"1",-.14f,-.02f,.12f,.065f},{KeypadKey::two,"2",0,-.02f,.12f,.065f},{KeypadKey::three,"3",.14f,-.02f,.12f,.065f},
        {KeypadKey::minus,"-",-.14f,-.10f,.12f,.065f},{KeypadKey::zero,"0",0,-.10f,.12f,.065f},{KeypadKey::decimal,".",.14f,-.10f,.12f,.065f},
        {KeypadKey::erase,"DEL",-.14f,-.18f,.12f,.065f},{KeypadKey::clear,"CLR",0,-.18f,.12f,.065f},{KeypadKey::accept,"OK",.14f,-.18f,.12f,.065f},
        {KeypadKey::cancel,"ANULUJ",0,-.26f,.40f,.06f}}};return buttons;
}
bool NumericKeypad::open(const XrRuntimeFrame& frame,float distance_m){
    GestureSettings settings;settings.input.menu_distance_m=distance_m;return open(frame,settings);
}
bool NumericKeypad::open(const XrRuntimeFrame& frame,const GestureSettings& settings){
    std::vector<PanelButton> buttons;for(const auto& key:numeric_keypad_buttons())buttons.push_back({static_cast<int>(key.key),std::string(key.label),key.x,key.y,key.width,key.height});
    PanelOptions options;options.offset.z=-settings.input.menu_distance_m;options.offset.y=settings.comfort.seated?-.12f:-.05f;options.scale=settings.input.menu_scale;options.dwell_ms=settings.input.dwell_ms;
    if(!panel_.open(frame,buttons,"KLAWIATURA - PROTOTYP",options))return false;result_.reset();clicked_=false;vehicle_=false;channels_=1;channel_=0;edited_={};text_={};return true;
}
bool NumericKeypad::open_vehicle(const XrRuntimeFrame& frame,const GestureSettings& settings,unsigned type,std::array<float,2> values){
    if((type!=5&&type!=6)||!std::isfinite(values[0])||!std::isfinite(values[1]))return false;
    if(!open(frame,settings))return false;
    auto buttons=panel_.snapshot().buttons;
    // open() has scaled the snapshot; use the unscaled layout for a new panel.
    buttons.clear();for(const auto& key:numeric_keypad_buttons())buttons.push_back({static_cast<int>(key.key),std::string(key.label),key.x,key.y,key.width,key.height});
    if(type==6){buttons.back().x=-.105f;buttons.back().width=.19f;buttons.push_back({static_cast<int>(KeypadKey::channel),"POLE 1 / 2",.105f,-.26f,.19f,.06f});}
    PanelOptions options;options.offset={0,settings.comfort.seated?-.12f:-.05f,-settings.input.menu_distance_m};options.scale=settings.input.menu_scale;options.dwell_ms=settings.input.dwell_ms;
    if(!panel_.open(frame,buttons,type==6?"DUZA KLAWIATURA POJAZDU":"KLAWIATURA POJAZDU",options))return false;
    vehicle_=true;channels_=type==6?2:1;initial_=values;
    for(unsigned i=0;i<2;++i){char buffer[64]{};const auto r=std::to_chars(buffer,buffer+sizeof(buffer),values[i]);text_[i]=std::string(buffer,r.ptr);}
    panel_.set_value(text_[0]);panel_.set_notice(channels_==2?"POLE 1 / 2 - PIERWSZA CYFRA ZASTEPUJE":"PIERWSZA CYFRA ZASTEPUJE WARTOSC");return true;
}
void NumericKeypad::cancel(){if(panel_.snapshot().visible)result_=KeypadResult{false,{}};panel_.cancel();}
std::optional<KeypadResult> NumericKeypad::take_result(){auto result=std::move(result_);result_.reset();return result;}
void NumericKeypad::activate(KeypadKey key){
    auto value=panel_.snapshot().value;
    if(vehicle_&&key==KeypadKey::channel){text_[channel_]=value;channel_=1-channel_;panel_.set_value(text_[channel_]);panel_.set_notice(channel_?"POLE 2 / 2":"POLE 1 / 2");return;}
    if(vehicle_&&key==KeypadKey::accept){
        text_[channel_]=value;auto values=initial_;
        try {for(unsigned i=0;i<channels_;++i)if(edited_[i])values[i]=parse_keypad_decimal(text_[i]);}
        catch(...){panel_.set_notice("NIEPOPRAWNA LICZBA - POPRAW POLE");return;}
        result_=KeypadResult{true,text_[0],values};panel_.cancel();return;
    }
    if(vehicle_&&key!=KeypadKey::cancel){
        if(!edited_[channel_]&&key!=KeypadKey::erase)value.clear();
        edited_[channel_]=true;
    }
    static constexpr std::string_view digits="789456123";const auto index=static_cast<unsigned>(key);
    if(index<digits.size()){if(value.size()<16)value+=digits[index];panel_.set_value(value);return;}
    switch(key){
    case KeypadKey::zero:if(value.size()<16)value+='0';break;
    case KeypadKey::minus:if(value.starts_with('-'))value.erase(0,1);else if(value.size()<16)value.insert(0,1,'-');break;
    case KeypadKey::decimal:if(value.size()<16&&value.find('.')==std::string::npos)value+='.';break;
    case KeypadKey::erase:if(!value.empty())value.pop_back();break;
    case KeypadKey::clear:value.clear();break;
    case KeypadKey::accept:if(value.find_first_of("0123456789")!=std::string::npos){result_=KeypadResult{true,value};panel_.cancel();}break;
    case KeypadKey::cancel:cancel();break;
    default:break;
    }
    panel_.set_value(value);
}
void NumericKeypad::update(const XrRuntimeFrame& frame){
    const bool visible=panel_.snapshot().visible;const auto key=panel_.update(frame);
    if(visible&&!panel_.snapshot().visible)result_=KeypadResult{false,{}};
    if(key&&*key>=0&&*key<=static_cast<int>(KeypadKey::channel)){clicked_=true;activate(static_cast<KeypadKey>(*key));}
}
}
