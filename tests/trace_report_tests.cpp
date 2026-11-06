#include "swvr/trace_report.hpp"
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok,const std::string& s){if(!ok)throw std::runtime_error(s);}
// Every report is "key=value" separated by single spaces, with unique keys.
std::map<std::string,std::string> parse(const std::string& line){
    std::map<std::string,std::string> out;std::istringstream in(line);std::string item;
    while(in>>item){
        const auto eq=item.find('=');require(eq!=std::string::npos&&eq>0,"malformed item: "+item);
        require(out.emplace(item.substr(0,eq),item.substr(eq+1)).second,"duplicate key: "+item);
    }
    require(!out.empty()&&line.find("  ")==std::string::npos&&line.front()!=' '&&line.back()!=' ',"bad spacing: "+line);
    return out;
}
}

int main(){try{
    swvr::AvatarGlStats a;
    a.candidate_draws=11;a.posed_draws=7;a.left_hands=5;a.right_hands=6;a.reach_limited=2;a.rejected=4;a.cache_hits=3;
    a.eyes=20;a.identified_eyes=18;a.bound_palettes=9;a.depth_draws=8;a.palette_uploads=30;a.single_palette_uploads=12;
    a.invalid_palette_uploads=1;a.single_palette_draws=13;a.single_provenance_misses=14;a.skinned_unrebased=15;
    a.missing_inputs=16;a.paired_provenance_misses=17;a.clip_rejects=19;a.bound_depth_palettes=21;
    a.body_calibrated=true;a.last_body_scale=1.25;a.last_status=swvr::AvatarPoseStatus::applied;a.last_head_distance=.5;
    const auto pose=swvr::format_avatar_pose_totals(a);const auto p=parse(pose);
    for(auto [key,value]:std::map<std::string,std::string>{{"candidates","11"},{"draws","7"},{"left","5"},{"right","6"},
        {"reach_limited","2"},{"rejected","4"},{"cache_hits","3"},{"eyes","20"},{"identified_eyes","18"},{"bound_palettes","9"},
        {"palette_uploads","30"},{"single_uploads","12"},{"invalid_uploads","1"},{"single_draws","13"},
        {"single_provenance_misses","14"},{"paired_provenance_misses","17"},{"skinned_unrebased","15"},{"missing_inputs","16"},
        {"clip_rejects","19"},{"bound_depth_palettes","21"},{"body_calibrated","1"},{"depth_draws","8"},{"last_status","applied"}})
        require(p.contains(key)&&p.at(key)==value,"avatar_pose_totals "+key+" in: "+pose);
    require(std::stod(p.at("body_scale"))==1.25&&std::stod(p.at("head_distance"))==.5,"avatar floats");
    // Wrist displacement of posed colour draws, in millimetres (mean/max per hand).
    {auto x=a;x.proximity_posed=5;x.proximity_rejects=2;const auto q=parse(swvr::format_avatar_pose_totals(x));
     require(q.at("proximity_posed")=="5"&&q.at("proximity_rejects")=="2","proximity totals");}
    {auto w=a;w.paired_seen=40;w.wrist_shift_samples={4,0};w.wrist_shift_sum={.8,0};w.wrist_shift_max={.35,0};
     const auto q=parse(swvr::format_avatar_pose_totals(w));
     require(q.at("paired_seen")=="40"&&std::stod(q.at("left_wrist_shift_mean_mm"))==200&&std::stod(q.at("left_wrist_shift_max_mm"))==350&&
             std::stod(q.at("right_wrist_shift_mean_mm"))==0&&q.at("right_wrist_shift_samples")=="0","wrist shift totals");}
    // The bounded-test supervisor (run-game-preview.ps1) matches these patterns.
    for(const char* pattern:{R"(\bdraws=[1-9][0-9]*\b)",R"(\bleft=[1-9][0-9]*\b)",R"(\bright=[1-9][0-9]*\b)",R"(\bdepth_draws=[1-9][0-9]*\b)"})
        require(std::regex_search(pose,std::regex(pattern)),std::string("supervisor pattern ")+pattern);
    a.posed_draws=0;require(!std::regex_search(swvr::format_avatar_pose_totals(a),std::regex(R"(\bdraws=[1-9][0-9]*\b)")),"draws=0 matched by depth/single draws");

    swvr::AvatarIdentityStats id;id.observations=10;id.valid_observations=6;id.invalid_snapshots=3;id.stale_snapshots=2;
    id.failed[static_cast<int>(swvr::AvatarIdentityStage::local)]=4;
    const auto i=parse(swvr::format_avatar_identity_totals(id));
    require(i.at("observations")=="10"&&i.at("valid")=="6"&&i.at("invalid_snapshots")=="3"&&i.at("stale_snapshots")=="2","identity totals");
    require(i.at("failed_local")=="4"&&i.at("failed_controller")=="0"&&i.at("failed_customization")=="0","identity stages");
    {auto m=id;m.local_meshes=7;m.scans=50;m.local_mesh_clip3=40;m.local_mesh_other_clip=9;m.foreign_19_bone=3;m.scans_local_without_clip3=2;
     const auto q=parse(swvr::format_avatar_identity_totals(m));
     require(q.at("local_meshes")=="7"&&q.at("scans")=="50"&&q.at("local_mesh_clip3")=="40"&&q.at("local_mesh_other_clip")=="9"&&
             q.at("foreign_19_bone")=="3"&&q.at("scans_local_without_clip3")=="2"&&q.size()==10+m.failed.size(),"identity record classification");}

    const auto h=parse(swvr::format_head_limit_totals({100,7,1.5f,.125f}));
    require(h.at("frames")=="100"&&h.at("limited_frames")=="7"&&std::stod(h.at("max_extent"))==1.5&&std::stod(h.at("max_correction_m"))==.125,"head limit totals");
    std::cout<<"PASS: trace reports keep unique key=value fields and supervisor patterns\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
