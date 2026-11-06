#include "swvr/native_avatar_identity.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
namespace swvr {
namespace {
using U=std::uintptr_t;
constexpr U constructor_rva=0x4d5f80,character_vtable_rva=0xb76630;
constexpr U opaque_rva=0x672700,glass_rva=0x672990,additive_rva=0x672c50;
// Shadow cascades: submit 0x6d0c20 passes the opaque producer and this shadow
// producer the same mesh, world matrices and current palette. The shadow record
// (stride 0xe0, list scene+0x378) owns a COPY of the palette at +0xc8.
constexpr U shadow_rva=0x6734e0;
// Actor render mesh sets passed to submit 0x6d0c20 by the character path in 0x6be1b0.
constexpr std::array<U,14> mesh_sets{0x888,0x930,0x9d8,0xd20,0xdc8,0xf18,0xfc0,0x1068,0x1110,0x11b8,0x1260,0x1308,0x1458,0x1500};
constexpr std::array<std::pair<U,U>,3> submit_calls{{{0x6d0e5b,opaque_rva},{0x6d0ea3,shadow_rva},{0x6d0eec,shadow_rva}}};
constexpr std::array<unsigned char,24> opaque_prefix{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
constexpr auto glass_prefix=opaque_prefix,additive_prefix=opaque_prefix;
constexpr std::size_t palette_bytes=19*sizeof(Matrix4),world_bytes=0x80;
constexpr std::array<unsigned char,24> constructor_prefix{0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd9,0x48,0x81,0xec};
bool readable(U address,std::size_t size) {
 if(!address||size>std::numeric_limits<U>::max()-address)return false;
 const auto end=address+size;
 while(address<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;const auto p=m.Protect&0xffu;if(p!=PAGE_READONLY&&p!=PAGE_READWRITE&&p!=PAGE_WRITECOPY&&p!=PAGE_EXECUTE_READ&&p!=PAGE_EXECUTE_READWRITE&&p!=PAGE_EXECUTE_WRITECOPY)return false;const auto next=reinterpret_cast<U>(m.BaseAddress)+m.RegionSize;if(next<=address)return false;address=next;}return true;
}
template<class T>bool read(U address,std::size_t offset,T& value){if(offset>std::numeric_limits<U>::max()-address||!readable(address+offset,sizeof(T)))return false;std::memcpy(&value,reinterpret_cast<void*>(address+offset),sizeof(T));return true;}
}
struct NativeAvatarIdentity::Impl {
 U base{};mutable std::mutex mutex;AvatarIdentity identity{};
 std::chrono::steady_clock::time_point observed{};
 AvatarIdentityStats stats{};
 std::vector<U> meshes; // Local actor mesh tokens from the last valid observation.
};
const char* avatar_identity_stage_name(AvatarIdentityStage stage) noexcept {
 switch(stage){
 case AvatarIdentityStage::controller:return "controller";case AvatarIdentityStage::world_node:return "world_node";
 case AvatarIdentityStage::node_kind:return "node_kind";case AvatarIdentityStage::actor:return "actor";
 case AvatarIdentityStage::vtable:return "vtable";case AvatarIdentityStage::local:return "local";
 case AvatarIdentityStage::head:return "head";case AvatarIdentityStage::customization:return "customization";
 default:return "unknown";}
}
NativeAvatarIdentity::NativeAvatarIdentity(U base,bool sha):impl_(std::make_unique<Impl>()) {
 if(!sha)throw std::invalid_argument("Avatar identity requires full executable SHA match");
 if(!base||base>std::numeric_limits<U>::max()-character_vtable_rva-8||!readable(base+constructor_rva,constructor_prefix.size())||std::memcmp(reinterpret_cast<void*>(base+constructor_rva),constructor_prefix.data(),constructor_prefix.size()))throw std::invalid_argument("Avatar rig loader prefix mismatch");
 impl_->base=base;
 for(auto rva:{opaque_rva,glass_rva,additive_rva,shadow_rva})if(!readable(base+rva,opaque_prefix.size())||std::memcmp(reinterpret_cast<void*>(base+rva),opaque_prefix.data(),opaque_prefix.size()))throw std::invalid_argument("Avatar scene record producer prefix mismatch");
 for(auto [site,target]:submit_calls){
  unsigned char opcode{};std::int32_t rel{};
  if(!read(base,site,opcode)||!read(base,site+1,rel)||opcode!=0xe8||static_cast<std::int64_t>(site)+5+rel!=static_cast<std::int64_t>(target))throw std::invalid_argument("Avatar shadow submit call site mismatch");
 }
}
NativeAvatarIdentity::~NativeAvatarIdentity()=default;
void NativeAvatarIdentity::observe(void* controller) noexcept {
 try {
  using S=AvatarIdentityStage;
  AvatarIdentity out;U world{},node{},holder{},actor{},vtable{},head{},custom{};int kind{},head_index{-1};unsigned char local{},male{255};
  const auto failed=[&]()->S{
   if(!read(reinterpret_cast<U>(controller),0,world))return S::controller;
   if(!read(world,0xbb9f8,node))return S::world_node;
   if(!read(node,0,kind)||kind!=4)return S::node_kind;
   if(!read(node,0x90,holder)||!read(holder,0,actor))return S::actor;
   if(!read(actor,0,vtable)||vtable!=impl_->base+character_vtable_rva)return S::vtable;
   if(!read(actor,0x2448,local)||local!=1)return S::local;
   if(!read(actor,0x15b8,head)||!read(head,0x10,head_index)||head_index!=3)return S::head;
   if(!read(actor,0x24d8,custom)||!read(custom,0,male)||male>1)return S::customization;
   return S::count;
  }();
  if(failed==S::count)out={actor,male?AvatarVariant::male:AvatarVariant::female,true};
  // Mesh tokens of the actor's render mesh sets (diagnostic classification only).
  std::vector<U> meshes;
  if(out.valid)for(U set:mesh_sets){
   U list{};std::uint32_t count{};
   if(!read(actor,set+0x68,list)||!read(actor,set+0x70,count)||count>64)continue;
   for(std::uint32_t i=0;i<count&&meshes.size()<256;++i){U mesh{};if(read(list,static_cast<std::size_t>(i)*0x48,mesh)&&mesh)meshes.push_back(mesh);}
  }
  std::lock_guard lock(impl_->mutex);impl_->identity=out;impl_->observed=std::chrono::steady_clock::now();impl_->meshes=std::move(meshes);impl_->stats.local_meshes=impl_->meshes.size();
  ++impl_->stats.observations;if(out.valid)++impl_->stats.valid_observations;else ++impl_->stats.failed[static_cast<std::size_t>(failed)];
 }catch(...){}
}
AvatarIdentity NativeAvatarIdentity::snapshot() const noexcept {
 try{std::lock_guard lock(impl_->mutex);
  if(!impl_->identity.valid){++impl_->stats.invalid_snapshots;return {};}
  if(std::chrono::steady_clock::now()-impl_->observed>std::chrono::milliseconds(100)){++impl_->stats.stale_snapshots;return {};}
  return impl_->identity;}catch(...){return {};}
}
AvatarIdentityStats NativeAvatarIdentity::stats() const {std::lock_guard lock(impl_->mutex);return impl_->stats;}
namespace {
// Ring list {data,capacity,begin,count} at +0,+8,+12,+16 of the prepared scene.
template<class F>void visit_records(U scene,std::uint32_t list_offset,U stride,F&& visit){
 U data{};std::uint32_t capacity{},begin{},count{};
 if(!read(scene,list_offset,data)||!read(scene,list_offset+8,capacity)||!read(scene,list_offset+12,begin)||!read(scene,list_offset+16,count))throw std::runtime_error("Unreadable avatar scene list");
 if(!count)return;
 if(!data||capacity>65536||count>capacity||begin>=capacity)throw std::runtime_error("Invalid avatar scene list bounds");
 for(std::uint32_t i=0;i<count;++i) {
  const auto index=(static_cast<std::uint64_t>(begin)+i)%capacity;
  if(data>std::numeric_limits<U>::max()-(index+1)*stride)throw std::runtime_error("Avatar scene record overflow");
  visit(data+index*stride);
 }
}
}
std::vector<AvatarPaletteBinding> NativeAvatarIdentity::palettes(void* scene) const {
 struct Local {U mesh{},current{};std::array<unsigned char,world_bytes> world{};};
 std::vector<AvatarPaletteBinding> result;std::vector<Local> locals;
 const auto object=reinterpret_cast<U>(scene);
 const auto add=[&](AvatarPaletteBinding binding){if(result.size()>=256)throw std::runtime_error("Too many local avatar palettes");result.push_back(binding);};
 std::vector<U> local_meshes;
 {std::lock_guard lock(impl_->mutex);if(impl_->identity.valid&&std::chrono::steady_clock::now()-impl_->observed<=std::chrono::milliseconds(100))local_meshes=impl_->meshes;}
 std::uint64_t local_clip3{},local_other{},foreign{};
 for(auto list_offset:{0x1e0u,0x1f8u,0x240u})visit_records(object,list_offset,0x118,[&](U record){
  int clip{-1};float threshold{};std::uint32_t bones{};U current{},previous{};
  if(!read(record,0x10c,clip)||!read(record,0x110,threshold))throw std::runtime_error("Unreadable avatar record selector");
  const bool head_selector=clip==3&&std::isfinite(threshold)&&std::abs(threshold-.25f)<=1e-6f;
  {U mesh{};std::uint32_t shape{};
   if(read(record,0,mesh)&&read(record,0x108,shape)&&shape==19){
    if(std::find(local_meshes.begin(),local_meshes.end(),mesh)==local_meshes.end())++foreign;
    else ++(head_selector?local_clip3:local_other);
   }}
  if(!head_selector){
   // Unselected 19-bone colour records become proximity candidates; the GL
   // adapter poses them only next to the eyes (see AvatarPaletteBinding).
   std::uint32_t shape{};U candidate{},candidate_previous{};
   if(read(record,0x108,shape)&&shape==19&&read(record,0xe8,candidate)&&read(record,0xf8,candidate_previous)&&
      readable(candidate,palette_bytes)&&readable(candidate_previous,palette_bytes))add({candidate,candidate_previous,false,true});
   return;
  }
  if(!read(record,0x108,bones)||bones!=19||!read(record,0xe8,current)||!read(record,0xf8,previous)||!readable(current,palette_bytes)||!readable(previous,palette_bytes))throw std::runtime_error("Avatar record palette layout mismatch");
  Local local{};local.current=current;
  if(!read(record,0,local.mesh)||!read(record,0x10,local.world))throw std::runtime_error("Unreadable avatar record transform");
  locals.push_back(local);add({current,previous});
 });
 {std::lock_guard lock(impl_->mutex);auto& st=impl_->stats;++st.scans;st.local_mesh_clip3+=local_clip3;st.local_mesh_other_clip+=local_other;st.foreign_19_bone+=foreign;if(local_other&&!local_clip3)++st.scans_local_without_clip3;}
 if(locals.empty())return result;
 // Shadow records carry no clip selector. Same mesh, byte-identical world
 // matrices and palette identify the copy made from a verified local record.
 visit_records(object,0x378,0xe0,[&](U record){
  U mesh{},palette{};std::uint32_t bones{};
  if(!read(record,0,mesh)||!read(record,0xd8,bones))throw std::runtime_error("Unreadable avatar shadow record");
  if(bones!=19)return;
  std::array<unsigned char,world_bytes> world{};bool same_world{};const Local* source{};
  for(const auto& local:locals){
   if(local.mesh!=mesh)continue;
   if(!same_world){if(!read(record,0x10,world))throw std::runtime_error("Unreadable avatar shadow transform");same_world=true;}
   if(world==local.world){source=&local;break;}
  }
  if(!source)return;
  if(!read(record,0xc8,palette)||!readable(palette,palette_bytes))throw std::runtime_error("Avatar shadow palette layout mismatch");
  if(std::memcmp(reinterpret_cast<const void*>(palette),reinterpret_cast<const void*>(source->current),palette_bytes))return;
  add({palette,0,true});
 });
 return result;
}
}
