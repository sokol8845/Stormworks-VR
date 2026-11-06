#pragma once
#include "swvr/locomotion_replay.hpp"
#include <cmath>
#include <fstream>
#include <sstream>
namespace swvr {
inline std::string json_string(std::string_view value){std::string out="\"";for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32)out+=' ';else out+=static_cast<char>(c);}return out+'"';}
inline void json_float(std::ostream& out,float n){if(std::isfinite(n))out<<n;else out<<"null";}
inline void json_position(std::ostream& out,Vec3 p){out<<'[';json_float(out,p.x);out<<',';json_float(out,p.y);out<<',';json_float(out,p.z);out<<']';}
class ReplayJson {
public:
    void append(const HandRecord& r,const ReplayObservation& o,LocomotionMode mode){
        if(!o.emitted)return;
        if(o.sample){const auto& f=r.sample.frame;std::ostringstream row;row.imbue(std::locale::classic());row.precision(9);
            row<<"{\"id\":"<<f.id<<",\"focused\":"<<(f.focused?"true":"false")<<",\"head\":";
            Vec3 head=f.eyes[0].pose.position;try{head=stereo_center({f.eyes[0].pose,f.eyes[1].pose}).position;}catch(...){}json_position(row,head);
            row<<",\"hands\":[";for(unsigned h=0;h<2;++h){if(h)row<<',';row<<"{\"active\":"<<(f.hands[h].active?"true":"false")<<",\"joints\":[";
                for(unsigned j=0;j<xr_hand_joint_count;++j){if(j)row<<',';const auto& joint=f.hands[h].joints[j];row<<"{\"p\":";json_position(row,joint.pose.position);row<<",\"tracked\":"<<(joint.position_valid&&joint.position_tracked?"true":"false")<<'}';}row<<"]}";}row<<"]}";samples_.push_back(row.str());
        }
        std::ostringstream row;row.imbue(std::locale::classic());row.precision(9);
        row<<"{\"t\":"<<static_cast<double>(r.journal?r.event.observed_ns:r.sample.elapsed_ns)/1e6<<",\"sample\":"<<(samples_.empty()?-1:static_cast<long long>(samples_.size()-1))
           <<",\"type\":"<<json_string(o.event)<<",\"status\":"<<json_string(o.status)<<",\"mode\":"<<json_string(mode==LocomotionMode::arm_swing?"arm_swing":"joystick")
           <<",\"mask\":"<<locomotion_buttons(o.command)<<",\"sprint\":"<<(o.command.sprint?"true":"false")<<",\"fist\":"<<(o.reserved&&mode==LocomotionMode::arm_swing?"true":"false")
           <<",\"half_strokes\":"<<o.swing.half_strokes<<",\"starts\":"<<o.swing.starts<<",\"blocks\":"<<o.blocks<<",\"physical\":["<<json_string(std::to_string(o.physical[0]))<<','<<json_string(std::to_string(o.physical[1]))<<"]"
           <<",\"mismatch\":"<<(o.mismatch?"true":"false")<<'}';events_.push_back(row.str());
    }
    void write(const std::filesystem::path& path,bool overridden,std::uint64_t mismatches){
        std::ofstream output(path,std::ios::binary);if(!output)throw std::runtime_error("Cannot write replay JSON");
        output<<"{\"schema\":1,\"complete\":true,\"overridden\":"<<(overridden?"true":"false")<<",\"mismatches\":"<<mismatches<<",\"samples\":[";
        for(std::size_t i=0;i<samples_.size();++i){if(i)output<<',';output<<samples_[i];}output<<"],\"events\":[";for(std::size_t i=0;i<events_.size();++i){if(i)output<<',';output<<events_[i];}output<<"]}\n";output.flush();if(!output)throw std::runtime_error("Replay JSON write failed");
    }
private:std::vector<std::string> samples_,events_;
};
}
