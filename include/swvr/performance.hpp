#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <locale>
#include <stdexcept>
namespace swvr {
class TimingDistribution {
public:
    struct SlowFrame {std::uint64_t frame{};double ms{};};
    void observe(double ms,std::uint64_t frame)noexcept{
        if(!std::isfinite(ms)||ms<0)return;++count_;sum_+=ms;maximum_=std::max(maximum_,ms);
        const auto bucket=ms>=127.75?511u:static_cast<unsigned>(std::ceil(ms*4));++histogram_[bucket];
        for(unsigned i=0;i<slowest_.size();++i)if(ms>slowest_[i].ms){for(auto j=slowest_.size()-1;j>i;--j)slowest_[j]=slowest_[j-1];slowest_[i]={frame,ms};break;}
    }
    double percentile(double fraction)const noexcept{
        if(!count_)return 0;const auto rank=std::max(std::uint64_t{1},static_cast<std::uint64_t>(std::ceil(std::clamp(fraction,0.,1.)*static_cast<double>(count_))));std::uint64_t seen{};
        for(unsigned i=0;i<histogram_.size();++i){seen+=histogram_[i];if(seen>=rank)return i==511?maximum_:i*.25;}return maximum_;
    }
    std::uint64_t count()const noexcept{return count_;}
    double mean()const noexcept{return count_?sum_/static_cast<double>(count_):0;}
    void json(std::ostream& out)const{
        out<<"{\"count\":"<<count_<<",\"mean_ms\":"<<mean()<<",\"p95_ms\":"<<percentile(.95)<<",\"p99_ms\":"<<percentile(.99)<<",\"max_ms\":"<<maximum_<<",\"slowest\":[";
        bool first=true;for(const auto& s:slowest_)if(s.ms>0){if(!first)out<<',';first=false;out<<"{\"xr_frame\":"<<s.frame<<",\"ms\":"<<s.ms<<'}';}out<<"]}";
    }
private:
    std::array<std::uint64_t,512> histogram_{};
    std::array<SlowFrame,10> slowest_{};
    std::uint64_t count_{};double sum_{},maximum_{};
};
struct PerformanceReport {
    TimingDistribution interval,avatar,overlay,recording,render_eye;
    TimingDistribution gpu,frame_work,runtime_period;
    std::uint64_t cpu_budget_samples{},cpu_over_budget{},gpu_budget_samples{},gpu_over_budget{},gpu_skipped{};
    unsigned gpu_pending{};bool gpu_available{};
    void cpu_sample(double ms,double budget,std::uint64_t frame){frame_work.observe(ms,frame);if(std::isfinite(budget)&&budget>0){runtime_period.observe(budget,frame);++cpu_budget_samples;cpu_over_budget+=ms>budget;}}
    void gpu_sample(double ms,double budget,std::uint64_t frame){gpu.observe(ms,frame);if(std::isfinite(budget)&&budget>0){++gpu_budget_samples;gpu_over_budget+=ms>budget;}}
    void save(const std::filesystem::path& path,std::uint64_t hits,std::uint64_t misses,std::uint64_t repeated,std::uint64_t empty)const{
        std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("Cannot create performance report");out.imbue(std::locale::classic());out.precision(9);
        out<<"{\"schema\":2,\"measurement\":\"CPU wall time and GPU timestamp interval; excludes compositor and streaming; runtime period may differ from physical refresh\",\"percentile_resolution_ms\":0.25,\"overflow_bucket_ms\":127.75,\"cache_hits\":"<<hits<<",\"cache_misses\":"<<misses<<",\"cache_hit_ratio\":"<<(hits+misses?static_cast<double>(hits)/static_cast<double>(hits+misses):0.)<<",\"repeated_frames\":"<<repeated<<",\"empty_frames\":"<<empty;
        out<<",\"gpu_available\":"<<(gpu_available?"true":"false")<<",\"gpu_skipped\":"<<gpu_skipped<<",\"gpu_pending\":"<<gpu_pending<<",\"cpu_budget_samples\":"<<cpu_budget_samples<<",\"cpu_over_budget\":"<<cpu_over_budget<<",\"cpu_over_budget_percent\":"<<(cpu_budget_samples?100.*static_cast<double>(cpu_over_budget)/static_cast<double>(cpu_budget_samples):0)<<",\"gpu_budget_samples\":"<<gpu_budget_samples<<",\"gpu_over_budget\":"<<gpu_over_budget<<",\"gpu_over_budget_percent\":"<<(gpu_budget_samples?100.*static_cast<double>(gpu_over_budget)/static_cast<double>(gpu_budget_samples):0);
        out<<",\"frame_interval\":";interval.json(out);out<<",\"avatar_pose_cpu\":";avatar.json(out);out<<",\"overlay_cpu\":";overlay.json(out);out<<",\"recording_cpu\":";recording.json(out);out<<",\"render_eye_cpu\":";render_eye.json(out);out<<",\"frame_work_cpu\":";frame_work.json(out);out<<",\"frame_gpu\":";gpu.json(out);out<<",\"runtime_period\":";runtime_period.json(out);out<<"}\n";out.flush();if(!out)throw std::runtime_error("Performance report write failed");
    }
};
}
