#pragma once
#include "swvr/xr_runtime.hpp"
#include "swvr/pointing_panel.hpp"
#include "swvr/gesture_settings.hpp"
#include <optional>
#include <string>
#include <string_view>
namespace swvr {
enum class KeypadKey { seven,eight,nine,four,five,six,one,two,three,minus,zero,decimal,erase,clear,accept,cancel,channel };
struct KeypadButton {KeypadKey key;std::string_view label;float x,y,width,height;};
const std::array<KeypadButton,16>& numeric_keypad_buttons() noexcept;
using KeypadSnapshot=PanelSnapshot;
struct KeypadResult {bool accepted{};std::string value;std::optional<std::array<float,2>> native_values;};
// Pure panel; the native adapter owns session validation and submission.
class NumericKeypad {
public:
    void record_to(ControlRecording* recorder){panel_.record_to(recorder,1);}
    bool open(const XrRuntimeFrame&,float distance_m=1.f);
    bool open(const XrRuntimeFrame&,const GestureSettings&);
    bool open_vehicle(const XrRuntimeFrame&,const GestureSettings&,unsigned button_type,std::array<float,2> values);
    void cancel();
    void update(const XrRuntimeFrame&);
    const KeypadSnapshot& snapshot() const noexcept {return panel_.snapshot();}
    void set_preferred_hand(unsigned hand){panel_.set_preferred_hand(hand);}
    std::optional<KeypadResult> take_result();
    bool take_click(){const bool value=clicked_;clicked_=false;return value;}
private:
    void activate(KeypadKey);
    PointingPanel panel_;
    std::optional<KeypadResult> result_;
    bool clicked_{};
    bool vehicle_{};unsigned channels_{1},channel_{};
    std::array<float,2> initial_{};
    std::array<std::string,2> text_;
    std::array<bool,2> edited_{};
};
}
