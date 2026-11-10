#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace swvr {
enum class GestureHand : unsigned { left=0,right=1 };
enum class JoystickActivation {whole_hand,middle_pinch};
enum class RunActivation {two_fists,alternating};
struct InputPreferences {GestureHand joystick_hand{GestureHand::left},pointing_priority{GestureHand::right};float menu_distance_m{1.f};bool movement_fists{true};unsigned dwell_ms{800};float menu_scale{1.f};bool click_sound{},joystick_visual{};bool operator==(const InputPreferences&)const=default;};
struct TurnSettings {float speed_degrees{60},dead_zone_m{.03f},full_deflection_m{.12f};bool snap{};unsigned snap_degrees{30};bool operator==(const TurnSettings&)const=default;};
struct ComfortSettings {bool head_fade{},seated{};float vignette_strength{},vignette_radius_degrees{40};bool operator==(const ComfortSettings&)const=default;};
struct SteeringSettings {float full_lock_degrees{90},dead_zone_degrees{1.4323945f},smoothing_ms{40};bool show_grip{};bool operator==(const SteeringSettings&)const=default;};
struct JoystickSettings {
    bool operator==(const JoystickSettings&)const=default;
    JoystickActivation activation{JoystickActivation::whole_hand};
    float dead_zone_m{.03f},full_deflection_m{.12f},sprint_on_m{.12f},sprint_off_m{.09f};
    float pinch_press_m{.025f},pinch_release_m{.040f};
    bool forward_only{};
};
struct ArmSwingSettings {
    bool operator==(const ArmSwingSettings&)const=default;
    RunActivation activation{RunActivation::two_fists};
    float stroke_m{.06f},minimum_speed_mps{.15f},maximum_speed_mps{4.f};
    unsigned start_half_strokes{3},minimum_half_stroke_ms{80},maximum_half_stroke_ms{900},stop_delay_ms{250};
    float fist_close_wrist_ratio{1.2f},fist_open_wrist_ratio{1.45f};
    float fist_close_knuckle_ratio{.8f},fist_open_knuckle_ratio{1.f};
};
struct GestureSettings {JoystickSettings joystick;ArmSwingSettings arm_swing;InputPreferences input;TurnSettings turn;ComfortSettings comfort;SteeringSettings steering;bool operator==(const GestureSettings&)const=default;};
// Invalid/unknown/duplicate values reject the entire file. No partial settings.
GestureSettings parse_gesture_settings(std::string_view,GestureSettings defaults={});
GestureSettings load_gesture_settings(const std::filesystem::path&);
void validate_gesture_settings(const GestureSettings&);
std::string describe_gesture_settings(const GestureSettings&);
std::string format_gesture_settings_ini(const GestureSettings&);
// Bounded read and atomic replacement in the destination directory.
void save_gesture_settings(const std::filesystem::path&,const GestureSettings&);
}
