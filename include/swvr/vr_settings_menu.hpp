#pragma once
#include "swvr/pointing_panel.hpp"
#include "swvr/control_profiles.hpp"
namespace swvr {
enum class VrMenuAction { joystick,arm_swing,reload,calibrate,clear_profile,keypad,joystick_left,joystick_right,priority_left,priority_right,close,controls,back,turn_slower,turn_faster,turn_dead_less,turn_dead_more,turn_range_less,turn_range_more,menu_nearer,menu_farther,defaults,interface_page,comfort_page,dwell_less,dwell_more,scale_less,scale_more,sound_toggle,turn_toggle,snap_30,snap_45,vignette_toggle,head_toggle,seated_toggle,recenter,help,movement_page,forward_only_toggle,tools_page,steering_page,profiles_page,wheel_lock_less,wheel_lock_more,wheel_dead_less,wheel_dead_more,wheel_smooth_less,wheel_smooth_more,profile_walking,profile_driving,profile_second_user,profile_save,profile_load,profile_restore,diagnostics_toggle,mark_problem,movement_tuning_page,joy_dead_less,joy_dead_more,joy_range_less,joy_range_more,sprint_on_less,sprint_on_more,sprint_off_less,sprint_off_more,joystick_visual_toggle,wheel_grip_reset,wheel_visual_toggle,practice };
std::string_view vr_menu_help(VrMenuAction)noexcept;
// Returns whether an action changes preferences; the host applies it after releasing input.
bool adjust_vr_preferences(VrMenuAction,GestureSettings&);
struct VrMenuCapabilities {bool locomotion{},avatar{},keypad{};};
class VrSettingsMenu {
public:
    void record_to(ControlRecording* recorder){panel_.record_to(recorder,0);}
    bool open(const XrRuntimeFrame&,VrMenuCapabilities,const GestureSettings&,bool arm_swing);
    void cancel(){panel_.cancel();confirm_clear_=help_mode_=false;}
    std::optional<VrMenuAction> update(const XrRuntimeFrame&);
    const PanelSnapshot& snapshot() const noexcept{return panel_.snapshot();}
    void set_preferred_hand(unsigned hand){panel_.set_preferred_hand(hand);}
    void status(const GestureSettings&,bool arm_swing);
    ControlProfile selected_profile()const noexcept{return profile_;}
    void diagnostics_enabled(bool enabled){diagnostics_=enabled;}
    void message(std::string text){panel_.set_notice(std::move(text));}
    bool take_click(){const bool value=clicked_;clicked_=false;return value;}
private:
    PointingPanel panel_;
    PanelOptions options_;
    VrMenuCapabilities capabilities_;
    void refresh_help();
    ControlProfile profile_{ControlProfile::walking};bool diagnostics_{};
    bool confirm_clear_{},clicked_{},arms_{},help_mode_{};unsigned page_{};GestureSettings settings_;
};
// MENU above the eye line, away from cockpit controls: touch or point for 800 ms.
// No palm-up or pinch gesture opens the application menu.
class VrMenuSummon {
public:
    void record_to(ControlRecording* recorder){button_.record_to(recorder,2);}
    bool update(const XrRuntimeFrame&,unsigned dwell_ms=800);
    void reset(){button_.cancel();}
    const PanelSnapshot& snapshot() const noexcept{return button_.snapshot();}
private:
    PointingPanel button_;
};
}
