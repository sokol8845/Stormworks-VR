#pragma once
#include "swvr/pointing_panel.hpp"
#include "swvr/gesture_settings.hpp"
#include <filesystem>
#include <fstream>
#include <optional>
namespace swvr {
// Render-thread companion to the v1-v3 locomotion journal. Every operation
// contains its source frame and expected result; no native pointers are stored.
class ControlRecording {
public:
    void open(const std::filesystem::path&,std::uint64_t limit=64*1024*1024);
    void close() noexcept;
    void panel_open(unsigned,const XrRuntimeFrame&,const PanelSnapshot&,PanelOptions,unsigned,std::span<const PanelButton>) noexcept;
    void panel_update(unsigned,const XrRuntimeFrame&,std::optional<int>,const PanelSnapshot&) noexcept;
    void panel_cancel(unsigned) noexcept;
    void panel_priority(unsigned,unsigned) noexcept;
    void panel_options(unsigned,PanelOptions) noexcept;
    void panel_inspection(unsigned,bool,int) noexcept;
    void turn(const XrRuntimeFrame&,const GestureSettings&,bool,double,bool) noexcept;
    void turn_reset() noexcept;
    bool active()const noexcept{return stream_.is_open()&&!failed_&&!limited_;}
    bool failed()const noexcept{return failed_;}
    bool limited()const noexcept{return limited_;}
    double cpu_ms()const noexcept{return cpu_ms_;}
private:
    void put(const std::string&);
    template<class F>void record(F&&);
    std::ofstream stream_;
    std::optional<GestureSettings> settings_;
    std::uint64_t bytes_{},limit_{};
    bool failed_{},limited_{};
    double cpu_ms_{};
};
struct ControlReplayStats {std::uint64_t turn_checks{},panel_checks{},mismatches{};bool limited{};};
ControlReplayStats replay_controls(std::istream&,std::ostream&);
}
