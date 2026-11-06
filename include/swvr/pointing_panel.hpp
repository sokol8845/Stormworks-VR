#pragma once
#include "swvr/xr_runtime.hpp"
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace swvr {
class ControlRecording;
struct PanelButton {int id{};std::string label;float x{},y{},width{},height{};bool enabled{true};};
struct PanelSnapshot {
    bool visible{};Pose panel{};std::uint64_t space_revision{};
    bool compact{};
    std::string title,value;
    int hover{-1},pressed{-1};
    float dwell_progress{};
    std::array<bool,2> rays{};
    std::array<int,2> ray_targets{-1,-1};
    std::array<Vec3,2> ray_start{},ray_end{};
    std::array<bool,2> touches{};
    std::array<Vec3,2> touch_local{};
    float scale{1};
    bool direct_touch{};
    bool help_mode{},release_required{};
    std::string description,notice;
    std::vector<PanelButton> buttons;
};
struct PanelOptions {
    Vec3 offset{0,-.05f,-1.f};
    bool follow_head{},compact{},allow_dwell{true};
    unsigned dwell_ms{800};float scale{1};
    unsigned dwell_grace_ms{120};
};
// Local panel input: fingertip contact fires once until the finger retracts.
// Distant pointing commits after the configured dwell, once until the pointer
// leaves. Brief geometric misses pause progress; tracking loss resets it.
// No pinches or operating-system input are used.
class PointingPanel {
public:
    void record_to(ControlRecording* recorder,unsigned id)noexcept{recorder_=recorder;recording_id_=id;}
    bool open(const XrRuntimeFrame&,std::span<const PanelButton>,std::string title,PanelOptions options={});
    bool reconfigure(PanelOptions);
    void inspect_only(bool enabled,int toggle_id);
    void set_description(std::string text){state_.description=text.substr(0,120);}
    void set_notice(std::string text){state_.notice=text.substr(0,40);}
    void cancel();
    std::optional<int> update(const XrRuntimeFrame&);
    void set_preferred_hand(unsigned);
    void set_value(std::string);
    const PanelSnapshot& snapshot() const noexcept {return state_;}
private:
    std::optional<int> update_impl(const XrRuntimeFrame&);
    ControlRecording* recorder_{};unsigned recording_id_{};
    void reset_input();
    PanelSnapshot state_;
    PanelOptions options_;
    std::vector<PanelButton> unscaled_buttons_;
    std::array<bool,2> need_clear_{};
    int inspection_toggle_{-1};
    struct Touch {bool armed{},latched{};float approach{};std::optional<Vec3> previous;};
    std::array<Touch,2> touch_;
    struct Dwell {int button{-1};std::optional<std::int64_t> began;bool fired{};std::optional<std::int64_t> last_hit,missed;};
    std::array<Dwell,2> dwell_;
    std::optional<std::int64_t> last_time_;
    std::optional<std::int64_t> pressed_until_;
    std::uint64_t last_frame_{};
    unsigned preferred_{1};
};
}
