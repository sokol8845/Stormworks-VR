#pragma once
#include "swvr/avatar_pose.hpp"
#include "swvr/gl_program_tracker.hpp"
#include "swvr/native_avatar_identity.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace swvr {
using AvatarGlResolver=void*(*)(const char*);
struct AvatarGlStats {
 std::uint64_t pose_calls{};double pose_cpu_ms{};
 std::uint64_t candidate_draws{},posed_draws{},left_hands{},right_hands{},reach_limited{},rejected{},cache_hits{};
 std::uint64_t eyes{},identified_eyes{},bound_palettes{},depth_draws{};
 std::uint64_t palette_uploads{},single_palette_uploads{},invalid_palette_uploads{},single_palette_draws{},single_provenance_misses{};
 // Rejections of skinned draws inside an active eye, by first failing check.
 std::uint64_t skinned_unrebased{},missing_inputs{},paired_provenance_misses{},clip_rejects{},bound_depth_palettes{};
 // Paired (colour) skinned draws after rebase, and how far posed colour draws
 // moved each wrist from its native position (world units).
 std::uint64_t paired_seen{},proximity_posed{},proximity_rejects{};
 std::array<std::uint64_t,2> wrist_shift_samples{};std::array<double,2> wrist_shift_sum{},wrist_shift_max{};
 bool body_calibrated{};double last_body_scale{1};
 AvatarPoseStatus last_status{AvatarPoseStatus::inactive};double last_head_distance{};
};
enum class AvatarDrawDecision {posed,unrebased,missing_inputs,provenance,clip,rejected};
enum class AvatarBindingKind {none,colour,depth,proximity};
// One skinned draw inside a journaled XR eye: inputs seen and the decision taken.
struct AvatarDrawJournalEntry {
 std::uint64_t eye{};unsigned program{};bool paired{};int upload_count{};
 bool has_current{},has_world{},has_previous{};
 AvatarBindingKind binding{};int clip{-1};float weight{};
 AvatarDrawDecision decision{};std::array<double,2> wrist_shift{};std::uintptr_t token{};
};
const char* avatar_draw_decision_name(AvatarDrawDecision) noexcept;
std::string avatar_journal_header();
std::string format_avatar_journal_row(const AvatarDrawJournalEntry&);
class GlAvatarPose {
public:
 GlAvatarPose();~GlAvatarPose();
 // Journal every Nth XR eye (0 disables), keeping at most capacity entries. Resets the journal.
 void set_journal(std::uint64_t every_n_eyes,std::size_t capacity);
 std::vector<AvatarDrawJournalEntry> journal() const;
 void configure(AvatarGlResolver,std::array<AvatarRig,2> female_male);
 // Observe ORIGINAL uploads after they have reached GL; copies contain values.
 void uniform_location(unsigned program,int location,const char* name);
 void matrix_uploaded(int location,int count,bool transpose,const float* values);
 void program_linked(unsigned program);
 void context_destroyed(void* context);
 void begin_eye(AvatarTargets,AvatarIdentity,Matrix4d expected_camera,std::span<const AvatarPaletteBinding>);
 // Called after the measured renderer rebases camera0 into its render space.
 void camera_rebased(const Matrix4d& actual_camera);
 void end_eye() noexcept;
 class Draw {
 public:
  Draw()=default;Draw(const Draw&)=delete;Draw& operator=(const Draw&)=delete;
  Draw(Draw&&) noexcept;~Draw();
 private:
  friend class GlAvatarPose;explicit Draw(GlAvatarPose* owner):owner_(owner){}
  GlAvatarPose* owner_{};
 };
 Draw before_draw(); // Restores exact native palettes when Draw leaves scope.
 // Side-effect free. `none`: the program has no bone palette, so before_draw()
 // and matrix_uploaded() cannot affect any draw; a host may skip both.
 GlProgramInterest interest(void* context,unsigned program) const;
 AvatarGlStats stats() const;
private:
 void restore() noexcept;
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
