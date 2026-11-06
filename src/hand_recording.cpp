#include "swvr/hand_recording.hpp"
#include <bit>
#include <charconv>
#include <locale>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace swvr {
namespace {
struct CpuTimer {double& total;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();~CpuTimer(){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}};
const int schema_slot=std::ios_base::xalloc();
const int sequence_slot=std::ios_base::xalloc();
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string row(std::istream& in){
    std::string value;char c{};
    while(in.get(c)){if(c=='\n'){if(!value.empty()&&value.back()=='\r')value.pop_back();return value;}require(value.size()<16384,"Hand recording row too large");value.push_back(c);}
    throw std::runtime_error("Hand recording missing END or truncated row");
}
void word(std::ostream& out,float f){out<<' '<<std::hex<<std::bit_cast<std::uint32_t>(f)<<std::dec;}
float real(std::istream& in){
    std::string token;require(static_cast<bool>(in>>token)&&token.size()<=8,"Invalid float bits");
    std::uint32_t bits{};const auto result=std::from_chars(token.data(),token.data()+token.size(),bits,16);
    require(result.ec==std::errc{}&&result.ptr==token.data()+token.size(),"Invalid float bits");return std::bit_cast<float>(bits);
}
void pose(std::ostream& out,const Pose& p){for(float f:{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w,p.position.x,p.position.y,p.position.z})word(out,f);}
Pose pose(std::istream& in){Pose p;p.orientation={real(in),real(in),real(in),real(in)};p.position={real(in),real(in),real(in)};return p;}
unsigned flags(bool a,bool b,bool c,bool d){return unsigned(a)|(unsigned(b)<<1)|(unsigned(c)<<2)|(unsigned(d)<<3);}
unsigned bounded(std::istream& in,unsigned maximum){unsigned value{};require(static_cast<bool>(in>>value)&&value<=maximum,"Invalid hand recording flag");return value;}
void done(std::istream& in){in>>std::ws;require(in.eof(),"Unexpected hand recording data");}
std::string ini(const GestureSettings& s){
    return format_gesture_settings_ini(s);
}
}
void write_hand_recording_header(std::ostream& out,unsigned version){require(version>=1&&version<=3,"Unsupported hand recording schema");out<<"SWVR_HANDS "<<version<<'\n';}
void read_hand_recording_header(std::istream& in){const auto header=row(in);require(header=="SWVR_HANDS 1"||header=="SWVR_HANDS 2"||header=="SWVR_HANDS 3","Unsupported hand recording schema");in.iword(schema_slot)=header.back()-'0';in.iword(sequence_slot)=0;}
void write_hand_configuration(std::ostream& out,const GestureSettings& settings,LocomotionMode mode){
    validate_gesture_settings(settings);require(mode==LocomotionMode::joystick||mode==LocomotionMode::arm_swing,"Invalid recording mode");
    out<<"C "<<(mode==LocomotionMode::arm_swing?"arm_swing":"joystick")<<'\n'<<ini(settings)<<"END_CONFIG\n";
}
void write_hand_sample(std::ostream& out,const HandRecordingSample& s){
    require(s.elapsed_ns>=0,"Negative recording timestamp");const auto& f=s.frame;
    out<<"F "<<s.elapsed_ns<<' '<<unsigned(s.walking_allowed)<<' '<<unsigned(s.suspended)<<' '<<f.id<<' '<<f.predicted_display_time_ns<<' '<<f.space_revision<<' '
       <<flags(f.running,f.focused,f.terminal,f.should_render)<<' '<<flags(f.position_valid,f.orientation_valid,f.position_tracked,f.orientation_tracked);
    for(const auto& eye:f.eyes){pose(out,eye.pose);for(float value:{eye.fov.left,eye.fov.right,eye.fov.up,eye.fov.down})word(out,value);}
    for(const auto& hand:f.hands){out<<' '<<unsigned(hand.active);for(const auto& j:hand.joints){pose(out,j.pose);word(out,j.radius);out<<' '<<flags(j.position_valid,j.orientation_valid,j.position_tracked,j.orientation_tracked);}}
    out<<'\n';
}
HandRecord read_hand_record(std::istream& input){
    auto line=row(input);HandRecord record;
    if(line=="END"){require(input.peek()==std::char_traits<char>::eof(),"Data after recording END");return record;}
    if(input.iword(schema_slot)>=2){
        require(line.starts_with("Q "),"Journal event envelope missing");std::istringstream q(line.substr(2));
        require(static_cast<bool>(q>>record.event.sequence>>record.event.observed_ns>>record.event.source_ns)&&record.event.observed_ns>=0&&record.event.source_ns>=0,"Invalid journal timestamp");done(q);
        require(record.event.sequence==static_cast<std::uint64_t>(input.iword(sequence_slot))+1&&record.event.sequence<2147483647,"Journal sequence gap");input.iword(sequence_slot)=static_cast<long>(record.event.sequence);record.journal=true;line=row(input);
    }
    if(line.starts_with("C ")) {
        record.kind=HandRecordKind::configuration;
        require(line=="C joystick"||line=="C arm_swing","Invalid hand recording mode");record.mode=line=="C joystick"?LocomotionMode::joystick:LocomotionMode::arm_swing;
        std::string text;
        for(;;){const auto config=row(input);if(config=="END_CONFIG")break;require(text.size()+config.size()<16384,"Recording configuration too large");text+=config+'\n';}
        GestureSettings defaults;if(input.iword(schema_slot)<3){defaults.joystick.activation=JoystickActivation::middle_pinch;defaults.arm_swing.activation=RunActivation::alternating;}
        record.settings=parse_gesture_settings(text,defaults);record.event.kind=LocomotionEventKind::configuration;record.event.settings=record.settings;record.event.arm_swing=record.mode==LocomotionMode::arm_swing;return record;
    }
    if(record.journal&&!line.starts_with("F ")){
        std::istringstream e(line);char kind{};e>>kind;record.kind=HandRecordKind::event;
        switch(kind){
        case 'R':record.event.kind=LocomotionEventKind::reset;require(static_cast<bool>(e>>std::quoted(record.event.reason)),"Invalid reset reason");break;
        case 'G':record.event.kind=LocomotionEventKind::gate;require(static_cast<bool>(e>>std::quoted(record.event.reason)),"Invalid gate reason");break;
        case 'S':record.event.kind=LocomotionEventKind::suspend;record.event.flag=bounded(e,1)!=0;break;
        case 'I':record.event.kind=LocomotionEventKind::inspect;require(static_cast<bool>(e>>record.event.bits),"Invalid inspected mask");break;
        case 'N':record.event.kind=LocomotionEventKind::native_state;require(static_cast<bool>(e>>record.event.blocks>>record.event.physical[0]>>record.event.physical[1]),"Invalid native state");break;
        default:throw std::runtime_error("Unknown journal event");
        }
        require(record.event.reason.size()<=128&&record.event.reason.find_first_of("\r\n") == std::string::npos,"Invalid journal reason");done(e);return record;
    }
    require(line.starts_with("F "),"Unknown hand recording record");record.kind=HandRecordKind::sample;
    std::istringstream in(line.substr(2));in.imbue(std::locale::classic());auto& s=record.sample;auto& f=s.frame;
    require(static_cast<bool>(in>>s.elapsed_ns)&&s.elapsed_ns>=0,"Invalid recording timestamp");
    s.walking_allowed=bounded(in,1)!=0;s.suspended=bounded(in,1)!=0;
    require(static_cast<bool>(in>>f.id>>f.predicted_display_time_ns>>f.space_revision),"Invalid frame identity");
    const auto state=bounded(in,15),tracking=bounded(in,15);
    f.running=(state&1)!=0;f.focused=(state&2)!=0;f.terminal=(state&4)!=0;f.should_render=(state&8)!=0;
    f.position_valid=(tracking&1)!=0;f.orientation_valid=(tracking&2)!=0;f.position_tracked=(tracking&4)!=0;f.orientation_tracked=(tracking&8)!=0;
    for(auto& eye:f.eyes){eye.pose=pose(in);eye.fov={real(in),real(in),real(in),real(in)};}
    for(auto& hand:f.hands){hand.active=bounded(in,1)!=0;for(auto& j:hand.joints){j.pose=pose(in);j.radius=real(in);const auto bits=bounded(in,15);
        j.position_valid=(bits&1)!=0;j.orientation_valid=(bits&2)!=0;j.position_tracked=(bits&4)!=0;j.orientation_tracked=(bits&8)!=0;}}
    done(in);record.event.kind=LocomotionEventKind::sample;record.event.sample=record.sample;return record;
}
void write_locomotion_event(std::ostream& out,const LocomotionEvent& e){
    require(e.sequence&&e.observed_ns>=0&&e.source_ns>=0,"Invalid journal envelope");require(e.reason.size()<=128&&e.reason.find_first_of("\r\n")==std::string::npos,"Invalid journal reason");
    out<<"Q "<<e.sequence<<' '<<e.observed_ns<<' '<<e.source_ns<<'\n';
    switch(e.kind){
    case LocomotionEventKind::configuration:write_hand_configuration(out,e.settings,e.arm_swing?LocomotionMode::arm_swing:LocomotionMode::joystick);break;
    case LocomotionEventKind::sample:write_hand_sample(out,e.sample);break;
    case LocomotionEventKind::reset:out<<"R "<<std::quoted(e.reason)<<'\n';break;
    case LocomotionEventKind::gate:out<<"G "<<std::quoted(e.reason)<<'\n';break;
    case LocomotionEventKind::suspend:out<<"S "<<e.flag<<'\n';break;
    case LocomotionEventKind::inspect:out<<"I "<<e.bits<<'\n';break;
    case LocomotionEventKind::native_state:out<<"N "<<e.blocks<<' '<<e.physical[0]<<' '<<e.physical[1]<<'\n';break;
    }
}
void HandRecordingWriter::open(const std::filesystem::path& path,const GestureSettings& settings,LocomotionMode mode,std::uint64_t limit){
    require(!stream_.is_open()&&path.is_absolute()&&!std::filesystem::exists(path),"Recording path exists or is not absolute");require(limit>=1024&&limit<=1024ULL*1024*1024,"Invalid recording limit");
    std::ostringstream header;header.imbue(std::locale::classic());write_hand_recording_header(header);write_hand_configuration(header,settings,mode);
    stream_.open(path,std::ios::binary|std::ios::out);require(static_cast<bool>(stream_),"Cannot open hand recording");
    samples_=bytes_=0;cpu_ms_=0;max_bytes_=limit;failed_=limited_=false;put(header.str());
}
void HandRecordingWriter::open_journal(const std::filesystem::path& path,std::uint64_t limit){
    require(!stream_.is_open()&&path.is_absolute()&&!std::filesystem::exists(path),"Recording path exists or is not absolute");require(limit>=1024&&limit<=1024ULL*1024*1024,"Invalid recording limit");
    stream_.open(path,std::ios::binary|std::ios::out);require(static_cast<bool>(stream_),"Cannot open hand recording");samples_=bytes_=0;cpu_ms_=0;max_bytes_=limit;failed_=limited_=false;
    put("SWVR_HANDS 3\n");
}
bool HandRecordingWriter::append(const LocomotionEvent& event){if(!active())return false;CpuTimer timer{cpu_ms_};std::ostringstream out;out.imbue(std::locale::classic());write_locomotion_event(out,event);if(!put(out.str()))return false;if(event.kind==LocomotionEventKind::sample)++samples_;return true;}
void HandRecordingWriter::mark_incomplete(){failed_=true;}
bool HandRecordingWriter::put(std::string_view text){
    if(!active())return false;
    if(bytes_+text.size()+4>max_bytes_){limited_=true;return false;}
    stream_.write(text.data(),static_cast<std::streamsize>(text.size()));bytes_+=text.size();
    if(!stream_){failed_=true;return false;}return true;
}
bool HandRecordingWriter::configure(const GestureSettings& settings,LocomotionMode mode){std::ostringstream out;out.imbue(std::locale::classic());write_hand_configuration(out,settings,mode);return put(out.str());}
bool HandRecordingWriter::append(const HandRecordingSample& sample){if(!active())return false;CpuTimer timer{cpu_ms_};std::ostringstream out;out.imbue(std::locale::classic());write_hand_sample(out,sample);if(!put(out.str()))return false;if(++samples_%120==0){stream_.flush();if(!stream_){failed_=true;return false;}}return true;}
void HandRecordingWriter::close(){if(stream_.is_open()){if(!failed_)stream_<<"END\n";stream_.flush();if(!stream_)failed_=true;stream_.close();if(stream_.fail())failed_=true;}}
}
