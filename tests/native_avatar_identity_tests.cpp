#include "swvr/native_avatar_identity.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
namespace {using U=std::uintptr_t;
void require(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
template<class T>void put(U address,std::size_t offset,T value){std::memcpy(reinterpret_cast<void*>(address+offset),&value,sizeof(T));}
template<class F>void rejects(F fn,const char* s){bool caught{};try{fn();}catch(const std::exception&){caught=true;}require(caught,s);}
}
int main(){try{
 auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0xc00000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));require(image!=nullptr,"fixture image allocation");const auto base=reinterpret_cast<U>(image);
 const std::array<unsigned char,24> prefix{0x48,0x89,0x5c,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xd9,0x48,0x81,0xec};
 rejects([&]{swvr::NativeAvatarIdentity fail(1,false);},"false SHA read supplied base");rejects([&]{swvr::NativeAvatarIdentity fail(base,true);},"bad prefix accepted");std::memcpy(image+0x4d5f80,prefix.data(),prefix.size());
 std::vector<unsigned char> world(0xbba10),actor(0x2500),node(0xa0),head(0x20);std::array<unsigned char,2> custom{1,0};U holder=reinterpret_cast<U>(actor.data()),controller=reinterpret_cast<U>(world.data());
 put(controller,0xbb9f8,reinterpret_cast<U>(node.data()));put(reinterpret_cast<U>(node.data()),0,int{4});put(reinterpret_cast<U>(node.data()),0x90,reinterpret_cast<U>(&holder));put(holder,0,base+0xb76630);put(holder,0x2448,std::uint8_t{1});put(holder,0x15b8,reinterpret_cast<U>(head.data()));put(reinterpret_cast<U>(head.data()),0x10,int{3});put(holder,0x24d8,reinterpret_cast<U>(custom.data()));
 const std::array<unsigned char,24> producer{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};for(auto rva:{0x672700,0x672990,0x672c50})std::memcpy(image+rva,producer.data(),producer.size());
 // Shadow producer and the submit call sites that pass it the opaque record's mesh/world/current palette.
 rejects([&]{swvr::NativeAvatarIdentity fail(base,true);},"missing shadow producer accepted");std::memcpy(image+0x6734e0,producer.data(),producer.size());
 const auto call=[&](U site,U target){image[site]=0xe8;put(base,site+1,static_cast<std::int32_t>(static_cast<std::int64_t>(target)-static_cast<std::int64_t>(site+5)));};
 call(0x6d0e5b,0x672700);call(0x6d0ea3,0x6734e0);rejects([&]{swvr::NativeAvatarIdentity fail(base,true);},"unverified shadow submit site accepted");call(0x6d0eec,0x6734e0);
 swvr::NativeAvatarIdentity identity(base,true);require(!identity.snapshot().valid,"empty identity accepted");identity.observe(&controller);auto s=identity.snapshot();require(s.valid&&s.actor==holder&&s.variant==swvr::AvatarVariant::male,"male native customization mapping wrong");custom[0]=0;identity.observe(&controller);require(identity.snapshot().valid&&identity.snapshot().variant==swvr::AvatarVariant::female,"female mapping wrong");
 put(holder,0x2448,std::uint8_t{0});identity.observe(&controller);require(!identity.snapshot().valid,"remote character accepted");put(holder,0x2448,std::uint8_t{1});
 put(reinterpret_cast<U>(head.data()),0x10,int{4});identity.observe(&controller);require(!identity.snapshot().valid,"unknown skeleton index accepted");put(reinterpret_cast<U>(head.data()),0x10,int{3});
 custom[0]=2;identity.observe(&controller);require(!identity.snapshot().valid,"unknown customization accepted");custom[0]=1;identity.observe(&controller);Sleep(115);require(!identity.snapshot().valid,"stale actor observation retained");identity.observe(reinterpret_cast<void*>(1));require(!identity.snapshot().valid,"unreadable controller accepted");
 std::array<unsigned char,0x400> scene{};std::array<unsigned char,0x118*4> records{};std::array<swvr::Matrix4,19> current{},previous{};auto scene_at=reinterpret_cast<U>(scene.data());put(scene_at,0x1e0,reinterpret_cast<U>(records.data()));put(scene_at,0x1e8,std::uint32_t{4});put(scene_at,0x1ec,std::uint32_t{3});put(scene_at,0x1f0,std::uint32_t{2});auto row=reinterpret_cast<U>(records.data()+0x118*3);put(row,0x10c,int{3});put(row,0x110,.25f);put(row,0x108,std::uint32_t{19});put(row,0xe8,reinterpret_cast<U>(current.data()));put(row,0xf8,reinterpret_cast<U>(previous.data()));put(reinterpret_cast<U>(records.data()),0x10c,int{-1});auto bindings=identity.palettes(scene.data());require(bindings.size()==1&&bindings[0].current==reinterpret_cast<U>(current.data()),"wrapped scene palette provenance failed");put(row,0x108,std::uint32_t{20});rejects([&]{(void)identity.palettes(scene.data());},"wrong local palette size accepted");put(row,0x108,std::uint32_t{19});
 // Shadow cascades (submit 0x6d0c20 -> 0x6734e0) draw a producer-owned copy of the
 // current palette from a separate 0xe0-stride list. Bind it only by exact mesh,
 // world and palette equality with a verified local record.
 for(int i=0;i<19;++i)current[i][0]=1.f+static_cast<float>(i);
 const U mesh=0x1234;put(row,0,mesh);for(int k=0;k<32;++k)put(row,0x10+4*k,static_cast<float>(k)*.5f);
 std::array<unsigned char,0xe0*3> shadows{};auto shadow_copy=current,foreign_copy=current;foreign_copy[4][12]=9.f;
 const auto shadow_row=[&](int i){return reinterpret_cast<U>(shadows.data()+0xe0*i);};
 put(scene_at,0x378,reinterpret_cast<U>(shadows.data()));put(scene_at,0x380,std::uint32_t{3});put(scene_at,0x384,std::uint32_t{1});put(scene_at,0x388,std::uint32_t{3});
 for(int i=0;i<3;++i){const auto r=shadow_row(i);put(r,0,mesh);std::memcpy(reinterpret_cast<void*>(r+0x10),reinterpret_cast<void*>(row+0x10),0x80);put(r,0xd8,std::uint32_t{19});}
 put(shadow_row(0),0xc8,reinterpret_cast<U>(shadow_copy.data()));
 put(shadow_row(1),0xc8,reinterpret_cast<U>(foreign_copy.data()));
 put(shadow_row(2),0xc8,reinterpret_cast<U>(shadow_copy.data()));put(shadow_row(2),0x10,99.f);
 bindings=identity.palettes(scene.data());
 require(bindings.size()==2&&!bindings[0].depth_only&&!bindings[0].proximity&&bindings[1].depth_only&&bindings[1].current==reinterpret_cast<U>(shadow_copy.data())&&bindings[1].previous==0,"local shadow palette copy not bound exactly once");
 put(shadow_row(0),0,U{0x9999});require(identity.palettes(scene.data()).size()==1,"shadow of another mesh bound");put(shadow_row(0),0,mesh);
 put(shadow_row(0),0xd8,std::uint32_t{20});require(identity.palettes(scene.data()).size()==1,"shadow with another skeleton bound");put(shadow_row(0),0xd8,std::uint32_t{19});
 shadow_copy[18][15]=2.f;require(identity.palettes(scene.data()).size()==1,"differing shadow pose bound");shadow_copy=current;
 put(row,0x10c,int{-1});{const auto only=identity.palettes(scene.data());require(only.size()==1&&only[0].proximity,"shadow bound without a verified local record");}put(row,0x10c,int{3});
 put(scene_at,0x388,std::uint32_t{4});rejects([&]{(void)identity.palettes(scene.data());},"invalid shadow list bounds accepted");put(scene_at,0x388,std::uint32_t{3});
 put(shadow_row(0),0xc8,U{1});rejects([&]{(void)identity.palettes(scene.data());},"unreadable shadow palette accepted");put(shadow_row(0),0xc8,reinterpret_cast<U>(shadow_copy.data()));
 // Mesh tokens of the local actor (mesh sets {+0x68 list,+0x70 count}, 0x48 stride)
 // classify 19-bone colour records by clip, to show whether the local body is
 // ever drawn without the head selector (e.g. while seated).
 {std::array<unsigned char,0x48*2> entries{};put(reinterpret_cast<U>(entries.data()),0,mesh);put(reinterpret_cast<U>(entries.data()),0x48,U{0x7777});
  put(holder,0x9d8+0x68,reinterpret_cast<U>(entries.data()));put(holder,0x9d8+0x70,std::uint32_t{2});
  identity.observe(&controller);require(identity.snapshot().valid,"identity lost with mesh sets");
  auto before=identity.stats();(void)identity.palettes(scene.data());auto after=identity.stats();
  require(after.local_mesh_clip3-before.local_mesh_clip3==1&&after.local_mesh_other_clip==before.local_mesh_other_clip,"local clip-3 record not classified");
  put(row,0x10c,int{-1});before=after;const auto unbound=identity.palettes(scene.data());after=identity.stats();
  require(unbound.size()==1&&unbound[0].proximity&&!unbound[0].depth_only&&unbound[0].current==reinterpret_cast<U>(current.data())&&after.local_mesh_other_clip-before.local_mesh_other_clip==1&&after.scans_local_without_clip3-before.scans_local_without_clip3==1,"local record without head selector not reported");
  put(row,0,U{0x5555});before=after;(void)identity.palettes(scene.data());after=identity.stats();
  require(after.foreign_19_bone-before.foreign_19_bone==1&&after.local_mesh_other_clip==before.local_mesh_other_clip,"foreign 19-bone record misclassified");
  put(row,0,mesh);put(row,0x10c,int{3});require(after.local_meshes==2,"local mesh tokens not collected");
  put(holder,0x9d8+0x70,std::uint32_t{100000});identity.observe(&controller);require(identity.snapshot().valid&&identity.stats().local_meshes==0,"unbounded mesh set accepted");}
 // Diagnostics: which read stage failed, and how many snapshots were stale versus invalid.
 {const auto st=identity.stats();
  require(st.observations==9&&st.valid_observations==5,"observation totals wrong");
  require(st.failed[static_cast<int>(swvr::AvatarIdentityStage::local)]==1,"remote character stage not counted");
  require(st.failed[static_cast<int>(swvr::AvatarIdentityStage::head)]==1,"skeleton stage not counted");
  require(st.failed[static_cast<int>(swvr::AvatarIdentityStage::customization)]==1,"customization stage not counted");
  require(st.failed[static_cast<int>(swvr::AvatarIdentityStage::controller)]==1,"unreadable controller stage not counted");
  require(st.stale_snapshots==1,"stale snapshot not counted");require(st.invalid_snapshots>=5,"invalid snapshots not counted");
  require(swvr::avatar_identity_stage_name(swvr::AvatarIdentityStage::world_node)==std::string_view("world_node"),"stage name");}
 VirtualFree(image,0,MEM_RELEASE);std::cout<<"Native avatar profile, local identity, model choice, unreadable/stale guards passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
