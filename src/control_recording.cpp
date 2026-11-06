#include "swvr/control_recording.hpp"
#include "swvr/hand_recording.hpp"
#include "swvr/hand_turn.hpp"
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace swvr {
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void frame(std::ostream& out,const XrRuntimeFrame& f){write_hand_sample(out,{0,false,false,f});}
std::string line(std::istream& in){std::string s;char c{};while(in.get(c)){if(c=='\n')return s;require(s.size()<16384,"Control row too large");s+=c;}throw std::runtime_error("Incomplete control recording");}
}
void ControlRecording::open(const std::filesystem::path& path,std::uint64_t limit){
    require(!stream_.is_open()&&path.is_absolute()&&!std::filesystem::exists(path)&&limit>=1024&&limit<=1024ULL*1024*1024,"Invalid control recording path or limit");
    stream_.open(path,std::ios::binary);require(bool(stream_),"Cannot open control recording");bytes_=0;limit_=limit;failed_=limited_=false;settings_.reset();cpu_ms_=0;put("SWVR_CONTROLS 3\n");
}
void ControlRecording::put(const std::string& text){if(!active())return;if(bytes_+text.size()+10>limit_){limited_=true;return;}stream_<<text;bytes_+=text.size();if(!stream_)failed_=true;}
template<class F>void ControlRecording::record(F&& fn){if(!active())return;const auto start=std::chrono::steady_clock::now();try{std::ostringstream out;out.imbue(std::locale::classic());out.precision(17);fn(out);put(out.str());}catch(...){failed_=true;}cpu_ms_+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
void ControlRecording::panel_open(unsigned id,const XrRuntimeFrame& f,const PanelSnapshot& p,PanelOptions options,unsigned preferred,std::span<const PanelButton> buttons)noexcept{
    record([&](auto& out){frame(out,f);out<<"O "<<id<<' '<<preferred<<' '<<options.offset.x<<' '<<options.offset.y<<' '<<options.offset.z<<' '<<options.follow_head<<' '<<options.compact<<' '<<options.allow_dwell<<' '<<options.dwell_ms<<' '<<options.scale<<' '<<options.dwell_grace_ms<<' '<<std::quoted(p.title)<<' '<<buttons.size();
        for(const auto& b:buttons)out<<' '<<b.id<<' '<<std::quoted(b.label)<<' '<<b.x<<' '<<b.y<<' '<<b.width<<' '<<b.height<<' '<<b.enabled;out<<'\n';});}
void ControlRecording::panel_update(unsigned id,const XrRuntimeFrame& f,std::optional<int> action,const PanelSnapshot& p)noexcept{
    record([&](auto& out){frame(out,f);out<<"U "<<id<<' '<<action.has_value()<<' '<<action.value_or(0)<<' '<<p.visible<<' '<<p.hover<<' '<<p.dwell_progress<<'\n';});}
void ControlRecording::panel_cancel(unsigned id)noexcept{record([&](auto& out){out<<"X "<<id<<'\n';});}
void ControlRecording::panel_priority(unsigned id,unsigned hand)noexcept{record([&](auto& out){out<<"P "<<id<<' '<<hand<<'\n';});}
void ControlRecording::panel_options(unsigned id,PanelOptions o)noexcept{record([&](auto& out){out<<"E "<<id<<' '<<o.offset.x<<' '<<o.offset.y<<' '<<o.offset.z<<' '<<o.follow_head<<' '<<o.compact<<' '<<o.allow_dwell<<' '<<o.dwell_ms<<' '<<o.scale<<' '<<o.dwell_grace_ms<<'\n';});}
void ControlRecording::panel_inspection(unsigned id,bool enabled,int toggle)noexcept{record([&](auto& out){out<<"H "<<id<<' '<<enabled<<' '<<toggle<<'\n';});}
void ControlRecording::turn(const XrRuntimeFrame& f,const GestureSettings& settings,bool allowed,double delta,bool reserved)noexcept{
    record([&](auto& out){if(!settings_||*settings_!=settings){write_hand_configuration(out,settings,LocomotionMode::joystick);settings_=settings;}
        frame(out,f);out<<"T "<<allowed<<' '<<delta<<' '<<reserved<<'\n';});}
void ControlRecording::turn_reset()noexcept{record([](auto& out){out<<"R\n";});}
void ControlRecording::close()noexcept{if(!stream_.is_open())return;try{if(!failed_)stream_<<(limited_?"END_LIMIT\n":"END\n");stream_.flush();if(!stream_)failed_=true;stream_.close();if(stream_.fail())failed_=true;}catch(...){failed_=true;}}
ControlReplayStats replay_controls(std::istream& input,std::ostream& output){
    const auto header=line(input);const bool v3=header=="SWVR_CONTROLS 3",v2=v3||header=="SWVR_CONTROLS 2";require(v2||header=="SWVR_CONTROLS 1","Unsupported controls schema");std::array<PointingPanel,3> panels;HandTurn turn;GestureSettings settings;bool configured{},has_frame{};XrRuntimeFrame f;ControlReplayStats stats;
    output<<"xr_frame\txr_time_ns\tevent\tpanel\texpected\tactual\tmismatch\n";output.precision(17);
    for(;;){const auto first=input.peek();if(first=='F'||first=='C'){const auto record=read_hand_record(input);if(record.kind==HandRecordKind::configuration){settings=record.settings;configured=true;}else{require(record.kind==HandRecordKind::sample,"Invalid controls sample");f=record.sample.frame;has_frame=true;}continue;}
        const auto row=line(input);if(row=="END"||row=="END_LIMIT"){require(input.peek()==std::char_traits<char>::eof(),"Trailing control data");stats.limited=row=="END_LIMIT";break;}
        std::istringstream in(row);in.imbue(std::locale::classic());char kind{};in>>kind;bool mismatch{};double expected{},actual{};unsigned id{};
        if(kind=='T'){bool allowed{},reserved{};in>>allowed>>expected>>reserved;require(configured&&has_frame&&std::isfinite(expected),"Invalid turn record");actual=turn.update(f,settings,allowed);mismatch=std::abs(actual-expected)>1e-7||turn.using_hand()!=reserved;++stats.turn_checks;}
        else if(kind=='R')turn.reset();
        else{in>>id;require(id<panels.size(),"Invalid panel id");auto& panel=panels[id];
            if(kind=='O'){unsigned preferred{},count{};PanelOptions options;std::string title;in>>preferred>>options.offset.x>>options.offset.y>>options.offset.z>>options.follow_head>>options.compact>>options.allow_dwell;
                options.dwell_grace_ms=0;if(v2)in>>options.dwell_ms>>options.scale;if(v3)in>>options.dwell_grace_ms;in>>std::quoted(title)>>count;
                require(has_frame&&preferred<2&&count<=32,"Invalid panel open");std::vector<PanelButton> buttons(count);for(auto& b:buttons)in>>b.id>>std::quoted(b.label)>>b.x>>b.y>>b.width>>b.height>>b.enabled;
                panel.set_preferred_hand(preferred);require(panel.open(f,buttons,title,options),"Recorded panel cannot be opened");}
            else if(kind=='U'){bool present{},visible{};int action{},hover{};float progress{};in>>present>>action>>visible>>hover>>progress;require(has_frame&&std::isfinite(progress),"Invalid panel update");const auto result=panel.update(f);expected=present?action:-1;actual=result.value_or(-1);
                mismatch=result!=(present?std::optional<int>(action):std::nullopt)||panel.snapshot().visible!=visible||panel.snapshot().hover!=hover||std::abs(panel.snapshot().dwell_progress-progress)>1e-5f;++stats.panel_checks;}
            else if(kind=='P'){unsigned hand{};in>>hand;panel.set_preferred_hand(hand);}
            else if(kind=='E'){require(v3,"Panel options require controls schema 3");PanelOptions o;in>>o.offset.x>>o.offset.y>>o.offset.z>>o.follow_head>>o.compact>>o.allow_dwell>>o.dwell_ms>>o.scale>>o.dwell_grace_ms;require(bool(in)&&panel.reconfigure(o),"Invalid panel options");}
            else if(kind=='H'){require(v3,"Inspection requires controls schema 3");bool enabled{};int toggle{};in>>enabled>>toggle;require(bool(in),"Invalid inspection options");panel.inspect_only(enabled,toggle);}
            else if(kind=='X')panel.cancel();else throw std::runtime_error("Unknown control event");}
        require(bool(in),"Malformed control event");in>>std::ws;require(in.eof(),"Trailing control fields");stats.mismatches+=mismatch;
        output<<f.id<<'\t'<<f.predicted_display_time_ns<<'\t'<<kind<<'\t'<<id<<'\t'<<expected<<'\t'<<actual<<'\t'<<mismatch<<'\n';
    }
    output<<(stats.mismatches?"#diverged":stats.limited?"#prefix":"#complete")<<"\tturn_checks="<<stats.turn_checks<<" panel_checks="<<stats.panel_checks<<" mismatches="<<stats.mismatches<<" limited="<<stats.limited<<'\n';return stats;
}
}
