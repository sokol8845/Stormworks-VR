#pragma once
#include "swvr/hand_locomotion.hpp"
#include <fstream>
#include <string>
namespace swvr {
enum class HandRecordKind { configuration,sample,event,end };
struct HandRecord {
    HandRecordKind kind{HandRecordKind::end};
    GestureSettings settings;
    LocomotionMode mode{LocomotionMode::joystick};
    HandRecordingSample sample;
    LocomotionEvent event;
    bool journal{};
};
void write_hand_recording_header(std::ostream&,unsigned version=1);
void read_hand_recording_header(std::istream&);
void write_hand_configuration(std::ostream&,const GestureSettings&,LocomotionMode);
void write_hand_sample(std::ostream&,const HandRecordingSample&);
HandRecord read_hand_record(std::istream&); // Requires explicit END, bounded rows.
void write_locomotion_event(std::ostream&,const LocomotionEvent&);
class HandRecordingWriter {
public:
    void open(const std::filesystem::path&,const GestureSettings&,LocomotionMode,std::uint64_t max_bytes=64*1024*1024);
    void open_journal(const std::filesystem::path&,std::uint64_t max_bytes=64*1024*1024);
    bool append(const LocomotionEvent&);
    void mark_incomplete();
    bool configure(const GestureSettings&,LocomotionMode);
    bool append(const HandRecordingSample&);
    void close();
    bool active() const noexcept {return stream_.is_open()&&!limited_&&!failed_;}
    bool limited() const noexcept {return limited_;}
    bool failed() const noexcept {return failed_;}
    std::uint64_t samples() const noexcept {return samples_;}
    double cpu_ms()const noexcept{return cpu_ms_;}
private:
    bool put(std::string_view);
    std::ofstream stream_;
    std::uint64_t samples_{},bytes_{},max_bytes_{};
    bool limited_{},failed_{};
    double cpu_ms_{};
};
}
