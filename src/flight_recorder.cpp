#include "swvr/flight_recorder.hpp"
#include <condition_variable>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <stdexcept>
namespace swvr {
namespace {void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
struct FlightRecorder::Impl {
    struct Packet {bool marker{};std::uint64_t ms{},frame{};XrRuntimeFrame xr;GestureSettings settings;std::vector<std::string> lines;};
    mutable std::mutex mutex;std::condition_variable wake;std::thread worker;std::deque<Packet> queue;
    FlightRecorderStats totals;bool stopping{},opened{};std::filesystem::path directory;FlightRecorderOptions options;
    std::deque<std::filesystem::path> segments;std::ofstream output;std::uint64_t bytes{},began{},serial{},saved_serial{};
    std::optional<GestureSettings> settings;
    void finish(){if(output.is_open()){output<<"END\n";output.flush();require(bool(output),"Flight recording write failed");output.close();require(!output.fail(),"Flight recording close failed");}}
    void rotate(std::uint64_t ms){
        finish();if(segments.size()==options.segments){std::filesystem::remove(segments.front());segments.pop_front();std::lock_guard lock(mutex);++totals.rotations;}
        auto path=directory/("segment-"+std::to_string(++serial)+".tsv");require(!std::filesystem::exists(path),"Flight segment collision");
        output.open(path,std::ios::binary);require(bool(output),"Cannot open flight segment");output<<"SWVR_FLIGHT 1\n";bytes=14;began=ms;settings.reset();segments.push_back(path);
    }
    void write(const Packet& p){
        std::ostringstream record;record.imbue(std::locale::classic());
        const auto config=[&]{write_hand_configuration(record,p.settings,p.settings.input.movement_fists?LocomotionMode::arm_swing:LocomotionMode::joystick);};
        if(!output.is_open()||p.ms<began||p.ms-began>=options.segment_ms)rotate(p.ms);
        if(!settings||*settings!=p.settings)config();
        write_hand_sample(record,{static_cast<std::int64_t>(p.ms*1'000'000),false,false,p.xr});
        record<<"D "<<p.lines.size();for(const auto& line:p.lines)record<<' '<<std::quoted(line);record<<'\n';
        auto text=record.str();
        if(bytes+text.size()+4>options.segment_bytes){
            rotate(p.ms);record.str({});record.clear();config();write_hand_sample(record,{static_cast<std::int64_t>(p.ms*1'000'000),false,false,p.xr});
            record<<"D "<<p.lines.size();for(const auto& line:p.lines)record<<' '<<std::quoted(line);record<<'\n';text=record.str();
        }
        if(bytes+text.size()+4>options.segment_bytes){std::lock_guard lock(mutex);++totals.dropped;return;}
        output<<text;output.flush();require(bool(output),"Flight recording write failed");bytes+=text.size();settings=p.settings;
        std::lock_guard lock(mutex);++totals.samples;
    }
    void snapshot(const Packet& p){
        require(!segments.empty(),"No flight samples available");output.flush();require(bool(output),"Flight flush failed");
        const auto slot=directory/("incident-"+std::to_string(saved_serial%options.incident_slots+1));
        if(std::filesystem::exists(slot)){
            require(!std::filesystem::is_symlink(slot)&&std::filesystem::is_directory(slot),"Invalid incident directory");
            for(const auto& entry:std::filesystem::directory_iterator(slot)){
                require(entry.is_regular_file()&&!entry.is_symlink(),"Unexpected incident entry");
                const auto name=entry.path().filename().string();require(name=="manifest.txt"||(name.starts_with("segment-")&&entry.path().extension()==".tsv"),"Unknown incident file");
                std::filesystem::remove(entry.path());
            }
        }else std::filesystem::create_directory(slot);
        for(const auto& source:segments){const auto dest=slot/source.filename();std::filesystem::copy_file(source,dest);
            if(source==segments.back()){std::ofstream tail(dest,std::ios::binary|std::ios::app);tail<<"END\n";tail.flush();require(bool(tail),"Cannot finish incident segment");}}
        std::ofstream manifest(slot/"manifest.txt",std::ios::binary);
        FlightRecorderStats counters;{std::lock_guard lock(mutex);counters=totals;}
        manifest<<"SWVR_INCIDENT 1\nmarker="<<saved_serial+1<<"\nelapsed_ms="<<p.ms<<"\nframe="<<p.frame<<"\ndropped="<<counters.dropped<<"\n";
        for(const auto& path:segments)manifest<<"segment="<<path.filename().string()<<'\n';
        manifest<<"scope=observations_not_deterministic_replay\n";manifest.flush();require(bool(manifest),"Cannot write incident manifest");
        ++saved_serial;std::lock_guard lock(mutex);totals.saved=saved_serial;
    }
    void run()noexcept{try{for(;;){Packet p;{std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping||!queue.empty();});if(queue.empty())break;p=std::move(queue.front());queue.pop_front();}
            if(p.marker)snapshot(p);else write(p);
        }finish();}catch(...){std::lock_guard lock(mutex);totals.failed=true;totals.dropped+=queue.size();queue.clear();}}
};
FlightRecorder::FlightRecorder():impl_(std::make_unique<Impl>()){}
FlightRecorder::~FlightRecorder(){close();}
void FlightRecorder::open(const std::filesystem::path& directory,FlightRecorderOptions options){
    auto& s=*impl_;require(!s.opened&&!s.stopping&&directory.is_absolute()&&options.segments>=2&&options.segments<=12&&options.incident_slots>=1&&options.incident_slots<=3&&options.segment_bytes>=4096&&options.segment_bytes<=16*1024*1024&&options.segment_ms>=100&&options.segment_ms<=60'000,"Invalid flight options");
    require(std::filesystem::create_directory(directory),"Flight directory already exists");s.directory=directory;s.options=options;s.opened=true;
    try{s.worker=std::thread([&s]{s.run();});}catch(...){s.opened=false;throw;}
}
void FlightRecorder::sample(const XrRuntimeFrame& frame,const GestureSettings& settings,std::uint64_t ms,std::vector<std::string> lines)noexcept{
    auto& s=*impl_;try{
        if(!frame.id||lines.size()>12||ms>9'000'000'000ULL)throw std::invalid_argument("Invalid flight sample");
        for(const auto& line:lines)if(line.size()>128||line.find_first_of("\r\n")!=std::string::npos)throw std::invalid_argument("Invalid flight diagnostic");
        std::lock_guard lock(s.mutex);if(!s.opened||s.stopping||s.totals.failed)return;
        if(s.queue.size()>=256){++s.totals.dropped;return;}
        s.queue.push_back({false,ms,frame.id,frame,settings,std::move(lines)});s.wake.notify_one();
    }catch(...){std::lock_guard lock(s.mutex);++s.totals.dropped;}
}
bool FlightRecorder::mark(std::uint64_t ms,std::uint64_t frame)noexcept{
    auto& s=*impl_;try{std::lock_guard lock(s.mutex);if(!s.opened||s.stopping||s.totals.failed||s.queue.size()>=256||(!s.totals.samples&&s.queue.empty())||s.totals.markers>s.totals.saved)return false;
        Impl::Packet p;p.marker=true;p.ms=ms;p.frame=frame;s.queue.push_back(std::move(p));++s.totals.markers;s.wake.notify_one();return true;
    }catch(...){return false;}
}
FlightRecorderStats FlightRecorder::stats()const noexcept{std::lock_guard lock(impl_->mutex);return impl_->totals;}
void FlightRecorder::close()noexcept{auto& s=*impl_;{std::lock_guard lock(s.mutex);s.stopping=true;s.wake.notify_one();}if(s.worker.joinable())s.worker.join();}
}
