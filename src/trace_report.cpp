#include "swvr/trace_report.hpp"

namespace swvr {
namespace {
std::string wrist(const char* side,const AvatarGlStats& a,int h) {
    const auto n=a.wrist_shift_samples[h];
    const double mean=n?a.wrist_shift_sum[h]/static_cast<double>(n)*1000:0;
    return std::string(" ")+side+"_wrist_shift_samples="+std::to_string(n)+" "+side+"_wrist_shift_mean_mm="+std::to_string(mean)+
        " "+side+"_wrist_shift_max_mm="+std::to_string(a.wrist_shift_max[h]*1000);
}
}
std::string format_avatar_pose_totals(const AvatarGlStats& a) {
    return "candidates="+std::to_string(a.candidate_draws)+" draws="+std::to_string(a.posed_draws)+
        " left="+std::to_string(a.left_hands)+" right="+std::to_string(a.right_hands)+
        " reach_limited="+std::to_string(a.reach_limited)+" rejected="+std::to_string(a.rejected)+
        " cache_hits="+std::to_string(a.cache_hits)+" eyes="+std::to_string(a.eyes)+
        " identified_eyes="+std::to_string(a.identified_eyes)+" bound_palettes="+std::to_string(a.bound_palettes)+
        " palette_uploads="+std::to_string(a.palette_uploads)+" single_uploads="+std::to_string(a.single_palette_uploads)+
        " invalid_uploads="+std::to_string(a.invalid_palette_uploads)+" single_draws="+std::to_string(a.single_palette_draws)+
        " single_provenance_misses="+std::to_string(a.single_provenance_misses)+
        " paired_provenance_misses="+std::to_string(a.paired_provenance_misses)+" skinned_unrebased="+std::to_string(a.skinned_unrebased)+
        " missing_inputs="+std::to_string(a.missing_inputs)+" clip_rejects="+std::to_string(a.clip_rejects)+
        " bound_depth_palettes="+std::to_string(a.bound_depth_palettes)+
        " body_calibrated="+std::to_string(a.body_calibrated)+" body_scale="+std::to_string(a.last_body_scale)+
        " depth_draws="+std::to_string(a.depth_draws)+" last_status="+avatar_pose_status_name(a.last_status)+
        " head_distance="+std::to_string(a.last_head_distance)+" paired_seen="+std::to_string(a.paired_seen)+
        " proximity_posed="+std::to_string(a.proximity_posed)+" proximity_rejects="+std::to_string(a.proximity_rejects)+
        wrist("left",a,0)+wrist("right",a,1);
}
std::string format_avatar_identity_totals(const AvatarIdentityStats& id) {
    auto out="observations="+std::to_string(id.observations)+" valid="+std::to_string(id.valid_observations)+
        " invalid_snapshots="+std::to_string(id.invalid_snapshots)+" stale_snapshots="+std::to_string(id.stale_snapshots)+
        " local_meshes="+std::to_string(id.local_meshes)+" scans="+std::to_string(id.scans)+
        " local_mesh_clip3="+std::to_string(id.local_mesh_clip3)+" local_mesh_other_clip="+std::to_string(id.local_mesh_other_clip)+
        " foreign_19_bone="+std::to_string(id.foreign_19_bone)+" scans_local_without_clip3="+std::to_string(id.scans_local_without_clip3);
    for(std::size_t i=0;i<id.failed.size();++i)
        out+=std::string(" failed_")+avatar_identity_stage_name(static_cast<AvatarIdentityStage>(i))+"="+std::to_string(id.failed[i]);
    return out;
}
std::string format_head_limit_totals(const HeadLimitTotals& h) {
    return "frames="+std::to_string(h.frames)+" limited_frames="+std::to_string(h.limited_frames)+
        " max_extent="+std::to_string(h.max_extent)+" max_correction_m="+std::to_string(h.max_correction);
}
}
