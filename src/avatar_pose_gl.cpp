#include "swvr/avatar_pose_gl.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
namespace swvr {
struct GlAvatarPose::Impl {
 enum Slot {current,previous,world,other};
 struct Program {
  std::map<int,Slot> locations;int current_location{-1},previous_location{-1},clip{-1},weight{-1};
  std::array<Matrix4,19> original{},old{};Matrix4 model{};
  bool has_current{},has_previous{},has_world{};
  std::uintptr_t current_token{},previous_token{};int current_count{};
  std::uint64_t serial{},cached_serial{},cached_frame{},cached_actor{};
  AvatarVariant cached_variant{};AvatarPoseResult cached;AvatarRetargeter retarget;
 };
 struct Context {
  int(APIENTRY* location)(unsigned,const char*){};
  void(APIENTRY* integer)(unsigned,int,int*){};void(APIENTRY* scalar)(unsigned,int,float*){};
  void(APIENTRY* matrix)(int,int,unsigned char,const float*){};
  void(APIENTRY* set_int)(int,int){};void(APIENTRY* set_float)(int,float){};
  std::map<unsigned,Program> programs;
 };
 std::recursive_mutex mutex;AvatarGlResolver resolver{};std::array<AvatarRig,2> rigs{};
 std::map<HGLRC,Context> contexts;AvatarGlStats counters;
 AvatarTargets targets;AvatarIdentity identity;Matrix4d expected{};
 std::vector<AvatarPaletteBinding> bindings;
 bool active{},rebased{};HGLRC eye_context{};std::thread::id eye_thread{};
 std::chrono::steady_clock::time_point eye_time;
 std::uint64_t journal_every{};std::size_t journal_capacity{};bool journal_eye{};std::vector<AvatarDrawJournalEntry> journal;
 Context* restoring_context{};Program* restoring_program{};unsigned restoring_name{};
 bool restoring_selector{};int restore_clip{-1};float restore_weight{};
 template<class T>T resolve(const char* name){auto p=resolver(name);if(!p||p==reinterpret_cast<void*>(1)||p==reinterpret_cast<void*>(2)||p==reinterpret_cast<void*>(3)||p==reinterpret_cast<void*>(-1))throw std::runtime_error("Avatar GL resolver failed");return reinterpret_cast<T>(p);}
 Context& context(HGLRC handle){
  auto found=contexts.find(handle);if(found!=contexts.end())return found->second;
  if(!resolver)throw std::logic_error("Avatar GL not configured");
  Context c;c.location=resolve<decltype(c.location)>("glGetUniformLocation");c.integer=resolve<decltype(c.integer)>("glGetUniformiv");c.scalar=resolve<decltype(c.scalar)>("glGetUniformfv");c.matrix=resolve<decltype(c.matrix)>("glUniformMatrix4fv");c.set_int=resolve<decltype(c.set_int)>("glUniform1i");c.set_float=resolve<decltype(c.set_float)>("glUniform1f");return contexts.emplace(handle,std::move(c)).first->second;
 }
 Program* program(Context& context,unsigned name){
  auto found=context.programs.find(name);if(found!=context.programs.end())return &found->second;
  if(context.programs.size()>=512)return nullptr;
  Program p;p.clip=context.location(name,"bone_clip_index");p.weight=context.location(name,"bone_clip_weight");
  p.current_location=context.location(name,"bone_matrices[0]");p.previous_location=context.location(name,"bone_matrices_prev[0]");
  const auto world_location=context.location(name,"mat_world");
  if(p.current_location>=0)p.locations[p.current_location]=current;
  if(p.previous_location>=0)p.locations[p.previous_location]=previous;
  if(world_location>=0)p.locations[world_location]=world;
  return &context.programs.emplace(name,std::move(p)).first->second;
 }
};
GlAvatarPose::GlAvatarPose():impl_(std::make_unique<Impl>()){}
GlAvatarPose::~GlAvatarPose()=default;
void GlAvatarPose::configure(AvatarGlResolver resolver,std::array<AvatarRig,2> rigs){std::lock_guard lock(impl_->mutex);if(!resolver)throw std::invalid_argument("Avatar GL resolver is null");restore();impl_->resolver=resolver;impl_->rigs=std::move(rigs);impl_->contexts.clear();impl_->active=false;}
void GlAvatarPose::uniform_location(unsigned name,int location,const char* text){
 if(location<0||!text)return;std::lock_guard lock(impl_->mutex);auto handle=wglGetCurrentContext();if(!handle)return;
 const std::string_view value(text);auto slot=Impl::other;
 if(value=="bone_matrices"||value=="bone_matrices[0]")slot=Impl::current;
 else if(value=="bone_matrices_prev"||value=="bone_matrices_prev[0]")slot=Impl::previous;
 else if(value=="mat_world")slot=Impl::world;
 else return;
 auto& c=impl_->context(handle);auto* p=impl_->program(c,name);if(!p)return;p->locations[location]=slot;
 if(slot==Impl::current)p->current_location=location;else if(slot==Impl::previous)p->previous_location=location;
}
void GlAvatarPose::matrix_uploaded(int location,int count,bool transpose,const float* values){
 std::lock_guard lock(impl_->mutex);auto handle=wglGetCurrentContext();if(!handle)return;
 GLint name{};glGetIntegerv(0x8B8D,&name);if(name<=0)return;
 auto& context=impl_->context(handle);auto* entry=impl_->program(context,static_cast<unsigned>(name));if(!entry)return;
 auto& program=*entry;auto slot=program.locations.find(location);
 if(slot==program.locations.end()){
  if(program.current_location>=0&&location>program.current_location&&location<program.current_location+19){program.has_current=false;++program.serial;}
  if(program.previous_location>=0&&location>program.previous_location&&location<program.previous_location+19){program.has_previous=false;++program.serial;}
  return;
 }
 if(slot->second==Impl::current){program.current_count=count;++impl_->counters.palette_uploads;if(program.previous_location<0)++impl_->counters.single_palette_uploads;if(count!=19||transpose||!values)++impl_->counters.invalid_palette_uploads;}
 if(slot->second==Impl::world){const bool valid=count==1&&!transpose&&values;if(valid!=program.has_world||(valid&&std::memcmp(values,program.model.data(),sizeof(Matrix4))))++program.serial;program.has_world=valid;if(valid)std::copy_n(values,16,program.model.data());}
 else {const bool valid=count==19&&!transpose&&values;auto& present=slot->second==Impl::current?program.has_current:program.has_previous;auto& data=slot->second==Impl::current?program.original:program.old;auto& token=slot->second==Impl::current?program.current_token:program.previous_token;
  // Provenance is checked against the current tokens before EVERY cache lookup.
  // A different authorized producer with identical matrices needs no new IK.
  if(valid!=present||(valid&&std::memcmp(values,data.data(),19*sizeof(Matrix4))))++program.serial;
  token=reinterpret_cast<std::uintptr_t>(values);present=valid;if(valid)std::memcpy(data.data(),values,19*sizeof(Matrix4));}
}
void GlAvatarPose::program_linked(unsigned name){std::lock_guard lock(impl_->mutex);if(impl_->restoring_name==name){impl_->restoring_program=nullptr;impl_->restoring_context=nullptr;}for(auto& [_,c]:impl_->contexts)c.programs.erase(name);}
void GlAvatarPose::context_destroyed(void* context){std::lock_guard lock(impl_->mutex);if(impl_->eye_context==context){impl_->active=false;impl_->restoring_program=nullptr;impl_->restoring_context=nullptr;}impl_->contexts.erase(static_cast<HGLRC>(context));}
void GlAvatarPose::begin_eye(AvatarTargets targets,AvatarIdentity identity,Matrix4d expected,std::span<const AvatarPaletteBinding> bindings){
 std::lock_guard lock(impl_->mutex);impl_->targets=std::move(targets);impl_->identity=identity;impl_->targets.actor=identity.actor;impl_->expected=expected;impl_->rebased=false;impl_->active=identity.valid&&impl_->targets.valid;impl_->eye_context=wglGetCurrentContext();impl_->eye_thread=std::this_thread::get_id();impl_->eye_time=std::chrono::steady_clock::now();
 impl_->bindings.assign(bindings.begin(),bindings.end());
 ++impl_->counters.eyes;impl_->journal_eye=impl_->journal_every&&impl_->counters.eyes%impl_->journal_every==0;impl_->counters.identified_eyes+=identity.valid;impl_->counters.bound_palettes+=bindings.size();
 impl_->counters.bound_depth_palettes+=static_cast<std::uint64_t>(std::count_if(bindings.begin(),bindings.end(),[](const auto& b){return b.depth_only;}));
 for(auto& [_,c]:impl_->contexts)for(auto& [__,p]:c.programs){p.cached_frame=0;if(!impl_->active)p.retarget.reset();}
}
void GlAvatarPose::camera_rebased(const Matrix4d& actual){std::lock_guard lock(impl_->mutex);if(!impl_->active||impl_->rebased||impl_->eye_context!=wglGetCurrentContext()||impl_->eye_thread!=std::this_thread::get_id())return;std::array<double,3> shift{};for(int i=0;i<3;++i)shift[i]=actual[12+i]-impl_->expected[12+i];impl_->targets=rebase_avatar_targets(impl_->targets,shift);impl_->rebased=true;}
void GlAvatarPose::end_eye() noexcept {try{restore();std::lock_guard lock(impl_->mutex);impl_->active=false;}catch(...){}}
GlAvatarPose::Draw GlAvatarPose::before_draw(){
 std::lock_guard lock(impl_->mutex);auto& s=*impl_;
 if(!s.active||s.eye_context!=wglGetCurrentContext()||s.eye_thread!=std::this_thread::get_id()||s.restoring_program||std::chrono::steady_clock::now()-s.eye_time>std::chrono::milliseconds(100))return {};
 GLint name{};glGetIntegerv(0x8B8D,&name);auto& c=s.context(s.eye_context);auto found=c.programs.find(static_cast<unsigned>(name));if(found==c.programs.end())return {};auto& p=found->second;
 if(p.current_location<0)return {};
 const bool paired=p.previous_location>=0;
 AvatarDrawJournalEntry entry;
 entry.eye=s.counters.eyes;entry.program=static_cast<unsigned>(name);entry.paired=paired;entry.upload_count=p.current_count;
 entry.has_current=p.has_current;entry.has_world=p.has_world;entry.has_previous=p.has_previous;entry.token=p.current_token;
 const auto note=[&](AvatarDrawDecision decision){
  if(!s.journal_eye||s.journal.size()>=s.journal_capacity)return;
  entry.decision=decision;if(decision==AvatarDrawDecision::posed)entry.wrist_shift=p.cached.wrist_shift;s.journal.push_back(entry);
 };
 // Before camera0 is rebased the render space of this eye is unknown.
 if(!s.rebased){++s.counters.skinned_unrebased;note(AvatarDrawDecision::unrebased);return {};}
 if(paired)++s.counters.paired_seen;else ++s.counters.single_palette_draws;
 if(!p.has_current||!p.has_world||(paired&&!p.has_previous)){++s.counters.missing_inputs;note(AvatarDrawDecision::missing_inputs);return {};}
 const auto provenance=std::find_if(s.bindings.begin(),s.bindings.end(),[&](const auto& b){return paired?(!b.depth_only&&p.current_token==b.current&&p.previous_token==b.previous):(!b.proximity&&(p.current_token==b.current||(!b.depth_only&&p.current_token==b.previous)));});
 if(provenance==s.bindings.end()){
  ++(paired?s.counters.paired_provenance_misses:s.counters.single_provenance_misses);
  // Journaled eyes only: record the head selector of unbound colour draws.
  if(s.journal_eye&&p.clip>=0&&p.weight>=0){GLint clip{-1};GLfloat weight{};c.integer(static_cast<unsigned>(name),p.clip,&clip);c.scalar(static_cast<unsigned>(name),p.weight,&weight);entry.clip=clip;entry.weight=weight;}
  note(AvatarDrawDecision::provenance);return {};
 }
 const bool proximity=provenance->proximity;
 entry.binding=proximity?AvatarBindingKind::proximity:provenance->depth_only?AvatarBindingKind::depth:AvatarBindingKind::colour;
 GLint native_clip{-1};GLfloat native_weight{};
 // Depth-only shaders have no clip uniforms or previous palette. Their
 // provenance still comes from the same verified local scene record.
 if(p.clip>=0||p.weight>=0){
  if(p.clip<0||p.weight<0){++s.counters.clip_rejects;note(AvatarDrawDecision::clip);return {};}
  GLint clip{-1};GLfloat weight{};c.integer(static_cast<unsigned>(name),p.clip,&clip);c.scalar(static_cast<unsigned>(name),p.weight,&weight);entry.clip=clip;entry.weight=weight;
  native_clip=clip;native_weight=weight;
  const bool selected=clip==3&&std::isfinite(weight)&&std::abs(weight-.25f)<=1e-6f;
  // Normal bindings need the local selector; proximity copies must lack it.
  if(selected==proximity){++s.counters.clip_rejects;note(AvatarDrawDecision::clip);return {};}
 } else if(proximity){++s.counters.clip_rejects;note(AvatarDrawDecision::clip);return {};}
 ++s.counters.candidate_draws;
 if(p.cached_frame==s.targets.frame&&p.cached_serial==p.serial&&p.cached_actor==s.identity.actor&&p.cached_variant==s.identity.variant)++s.counters.cache_hits;
 else {
  const auto pose_start=std::chrono::steady_clock::now();
  p.cached=p.retarget.pose(s.rigs[s.identity.variant==AvatarVariant::male?1:0],s.identity.variant,s.targets,p.model,p.original,paired?p.old:p.original);
  s.counters.pose_cpu_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-pose_start).count();++s.counters.pose_calls;
  p.cached_frame=s.targets.frame;p.cached_serial=p.serial;p.cached_actor=s.identity.actor;p.cached_variant=s.identity.variant;
 }
 s.counters.body_calibrated=p.cached.body_calibrated;s.counters.last_body_scale=p.cached.body_scale;
 s.counters.last_status=p.cached.status;s.counters.last_head_distance=p.cached.head_distance;
 if(p.cached.status!=AvatarPoseStatus::applied){++s.counters.rejected;note(AvatarDrawDecision::rejected);return {};}
 // Only a copy whose native head sits at the eyes is the local body.
 if(proximity&&!(p.cached.head_distance<=avatar_proximity_head_m)){++s.counters.proximity_rejects;note(AvatarDrawDecision::rejected);return {};}
 s.restoring_context=&c;s.restoring_program=&p;s.restoring_name=static_cast<unsigned>(name);
 c.matrix(p.current_location,19,GL_FALSE,p.cached.current[0].data());if(paired)c.matrix(p.previous_location,19,GL_FALSE,p.cached.previous[0].data());
 if(proximity){
  // Hide the copy's head like the selected local body; restored after the draw.
  s.restoring_selector=true;s.restore_clip=native_clip;s.restore_weight=native_weight;
  c.set_int(p.clip,3);c.set_float(p.weight,.25f);++s.counters.proximity_posed;
 }
 note(AvatarDrawDecision::posed);
 if(paired)for(int h=0;h<2;++h)if(p.cached.hands[h]){
  auto& n=s.counters.wrist_shift_samples[h];++n;s.counters.wrist_shift_sum[h]+=p.cached.wrist_shift[h];
  s.counters.wrist_shift_max[h]=std::max(s.counters.wrist_shift_max[h],p.cached.wrist_shift[h]);
 }
 ++s.counters.posed_draws;s.counters.depth_draws+=!paired;s.counters.left_hands+=p.cached.hands[0];s.counters.right_hands+=p.cached.hands[1];s.counters.reach_limited+=p.cached.reach_limited[0]||p.cached.reach_limited[1];return Draw(this);
}
void GlAvatarPose::restore() noexcept {
 try{std::lock_guard lock(impl_->mutex);auto& s=*impl_;auto* p=std::exchange(s.restoring_program,nullptr);auto* c=std::exchange(s.restoring_context,nullptr);if(!p||!c)return;GLint name{};if(wglGetCurrentContext()!=s.eye_context)return;glGetIntegerv(0x8B8D,&name);if(static_cast<unsigned>(name)!=s.restoring_name)return;c->matrix(p->current_location,19,GL_FALSE,p->original[0].data());if(p->previous_location>=0)c->matrix(p->previous_location,19,GL_FALSE,p->old[0].data());if(std::exchange(s.restoring_selector,false)){c->set_int(p->clip,s.restore_clip);c->set_float(p->weight,s.restore_weight);}}catch(...){}
}
GlAvatarPose::Draw::Draw(Draw&& other) noexcept:owner_(std::exchange(other.owner_,nullptr)){}
GlAvatarPose::Draw::~Draw(){if(owner_)owner_->restore();}
AvatarGlStats GlAvatarPose::stats() const {std::lock_guard lock(impl_->mutex);return impl_->counters;}
void GlAvatarPose::set_journal(std::uint64_t every,std::size_t capacity){std::lock_guard lock(impl_->mutex);impl_->journal_every=every;impl_->journal_capacity=capacity;impl_->journal.clear();impl_->journal.reserve(std::min<std::size_t>(capacity,65536));impl_->journal_eye=false;}
std::vector<AvatarDrawJournalEntry> GlAvatarPose::journal() const {std::lock_guard lock(impl_->mutex);return impl_->journal;}
const char* avatar_draw_decision_name(AvatarDrawDecision d) noexcept {
 switch(d){case AvatarDrawDecision::posed:return "posed";case AvatarDrawDecision::unrebased:return "unrebased";
 case AvatarDrawDecision::missing_inputs:return "missing_inputs";case AvatarDrawDecision::provenance:return "provenance";
 case AvatarDrawDecision::clip:return "clip";case AvatarDrawDecision::rejected:return "rejected";}return "unknown";
}
std::string avatar_journal_header(){return "eye\tprogram\tpaired\tupload_count\thas_current\thas_world\thas_previous\tbinding\tclip\tweight\tdecision\tleft_wrist_shift_m\tright_wrist_shift_m\ttoken";}
std::string format_avatar_journal_row(const AvatarDrawJournalEntry& e){
 const char* binding=e.binding==AvatarBindingKind::colour?"colour":e.binding==AvatarBindingKind::depth?"depth":e.binding==AvatarBindingKind::proximity?"proximity":"none";
 char token[24]{};std::snprintf(token,sizeof(token),"%llx",static_cast<unsigned long long>(e.token));
 return std::to_string(e.eye)+'\t'+std::to_string(e.program)+'\t'+std::to_string(e.paired)+'\t'+std::to_string(e.upload_count)+'\t'+
  std::to_string(e.has_current)+'\t'+std::to_string(e.has_world)+'\t'+std::to_string(e.has_previous)+'\t'+binding+'\t'+
  std::to_string(e.clip)+'\t'+std::to_string(e.weight)+'\t'+avatar_draw_decision_name(e.decision)+'\t'+
  std::to_string(e.wrist_shift[0])+'\t'+std::to_string(e.wrist_shift[1])+'\t'+token;
}
}
