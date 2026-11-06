#pragma once
#include <cstdint>
struct FakeXrState {
    bool hand_aim_extension{},hand_aim_enabled{},system_menu_hands[2]{};
    bool should_render{true}, valid_pose{true}, active_hands{true}, fail_right_acquire{}, timeout_right_wait{};
    int pending_event{}, initial_events{}, created_swapchains{}, acquired{}, waited{}, released{};
    int projections{}, empty_frames{}, begin_sessions{}, end_sessions{}, destroyed_sessions{}, destroyed_instances{};
    int destroyed_swapchains{}, destroyed_hands{}, wait_frames{}, begin_frames{}, invalid_releases{};
    std::int64_t display_time{1000000000}, eye_time{}, hand_times[2]{};
    std::int64_t display_period{11'111'111};
    float last_view_x[2]{}, last_fov_left[2]{}; // From the last submitted projection layer.
    // Opt-in GPU integration evidence; untouched by protocol-only mode.
    bool gpu_mode{}, gpu_readback{}, gpu_different_eyes{}, gpu_nonblack[2]{};
    int gpu_width{}, gpu_height{}, gpu_errors{}, gpu_valid_projections{};
    std::uint64_t gpu_checksum[2]{};
    std::uint32_t gpu_hand_pixels[2][2]{}; // [eye][cyan/orange diagnostic color]
};
