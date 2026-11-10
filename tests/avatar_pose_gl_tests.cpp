#include "swvr/avatar_pose_gl.hpp"
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
using namespace swvr;void require(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
template<class T>T proc(const char* name){auto p=wglGetProcAddress(name);require(p&&p!=reinterpret_cast<PROC>(-1),name);return reinterpret_cast<T>(p);}
void* resolver(const char* name){return reinterpret_cast<void*>(wglGetProcAddress(name));}
Matrix4d id(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix4 fm(Matrix4d m){Matrix4 r{};for(int i=0;i<16;++i)r[i]=static_cast<float>(m[i]);return r;}
AvatarRig rig(){AvatarRig r;for(unsigned i=0;i<19;++i){r.bind[i]=r.inverse_bind[i]=id();r.bind[i][13]=1.5;r.inverse_bind[i][13]=-1.5;r.parent[i]=i?static_cast<int>(i)-1:-1;}for(auto [i,x]:std::array<std::pair<int,double>,6>{{{4,-.2},{5,-.48},{6,-.73},{9,.2},{10,.48},{11,.73}}}){r.bind[i][12]=x;r.inverse_bind[i][12]=-x;}return r;}
}
int main(){try{
 auto window=CreateWindowW(L"STATIC",L"Avatar GPU fixture",WS_OVERLAPPEDWINDOW,0,0,32,32,nullptr,nullptr,nullptr,nullptr);auto dc=GetDC(window);PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.cColorBits=32;require(SetPixelFormat(dc,ChoosePixelFormat(dc,&pfd),&pfd)!=0,"pixel format");auto context=wglCreateContext(dc);require(wglMakeCurrent(dc,context)!=0,"GL context");
 auto create=proc<GLuint(APIENTRY*)(GLenum)>("glCreateShader");auto source=proc<void(APIENTRY*)(GLuint,GLsizei,const char*const*,const GLint*)>("glShaderSource");auto compile=proc<void(APIENTRY*)(GLuint)>("glCompileShader");auto shader_int=proc<void(APIENTRY*)(GLuint,GLenum,GLint*)>("glGetShaderiv");auto program=proc<GLuint(APIENTRY*)()>("glCreateProgram")();auto attach=proc<void(APIENTRY*)(GLuint,GLuint)>("glAttachShader");auto link=proc<void(APIENTRY*)(GLuint)>("glLinkProgram");auto program_int=proc<void(APIENTRY*)(GLuint,GLenum,GLint*)>("glGetProgramiv");auto use=proc<void(APIENTRY*)(GLuint)>("glUseProgram");auto location=proc<GLint(APIENTRY*)(GLuint,const char*)>("glGetUniformLocation");auto upload=proc<void(APIENTRY*)(GLint,GLsizei,GLboolean,const GLfloat*)>("glUniformMatrix4fv");auto integer=proc<void(APIENTRY*)(GLint,GLint)>("glUniform1i");auto scalar=proc<void(APIENTRY*)(GLint,GLfloat)>("glUniform1f");auto get=proc<void(APIENTRY*)(GLuint,GLint,GLfloat*)>("glGetUniformfv");
 const char* vs="#version 330 core\nuniform mat4 bone_matrices[19],bone_matrices_prev[19],mat_world;uniform int bone_clip_index;uniform float bone_clip_weight;void main(){int i=clamp(bone_clip_index,0,18);gl_Position=mat_world*(bone_matrices_prev[i]*(1.0-bone_clip_weight)+bone_matrices[i]*bone_clip_weight)*vec4(0,0,0,1);}";const char* fs="#version 330 core\nout vec4 color;void main(){color=vec4(1);}";
 for(auto [kind,text]:std::array<std::pair<GLenum,const char*>,2>{{{0x8B31,vs},{0x8B30,fs}}}){auto sh=create(kind);source(sh,1,&text,nullptr);compile(sh);GLint ok{};shader_int(sh,0x8B81,&ok);require(ok!=0,"avatar shader compilation");attach(program,sh);}link(program);GLint linked{};program_int(program,0x8B82,&linked);require(linked!=0,"avatar shader link");use(program);
 GlAvatarPose avatar;avatar.configure(resolver,{rig(),rig()});avatar.set_journal(1,1000);
 const auto current=location(program,"bone_matrices[0]"),old=location(program,"bone_matrices_prev[0]"),world=location(program,"mat_world"),clip=location(program,"bone_clip_index"),weight=location(program,"bone_clip_weight");avatar.uniform_location(program,current,"bone_matrices[0]");avatar.uniform_location(program,old,"bone_matrices_prev[0]");avatar.uniform_location(program,world,"mat_world");
 require(avatar.interest(context,program)==GlProgramInterest::active&&avatar.interest(context,program+1000)==GlProgramInterest::unknown,"skinned program interest");
 std::array<Matrix4,19> palettes;for(auto& m:palettes)m=fm(id());const auto model=fm(id());auto originals=[&]{upload(current,19,GL_FALSE,palettes[0].data());avatar.matrix_uploaded(current,19,false,palettes[0].data());upload(old,19,GL_FALSE,palettes[0].data());avatar.matrix_uploaded(old,19,false,palettes[0].data());upload(world,1,GL_FALSE,model.data());avatar.matrix_uploaded(world,1,false,model.data());};originals();integer(clip,3);scalar(weight,.25f);
 const std::array<AvatarPaletteBinding,1> bindings{{{reinterpret_cast<std::uintptr_t>(palettes.data()),reinterpret_cast<std::uintptr_t>(palettes.data())}}};
 AvatarTargets targets;targets.frame=1;targets.valid=true;targets.reference=id();targets.reference[13]=1.6;targets.head=targets.reference;targets.head[12]=.2;avatar.begin_eye(targets,{42,AvatarVariant::male,true},targets.reference,bindings);avatar.camera_rebased(targets.reference);
 const auto read_head=[&](GLint base){Matrix4 m{};get(program,base+3,m.data());return m;};
 {auto draw=avatar.before_draw();require(std::abs(read_head(current)[12]-.2f)<1e-5,"GPU head palette not overridden");require(read_head(old)==read_head(current),"native old palette mixed with tracked pose");}
 require(read_head(current)==palettes[3]&&read_head(old)==palettes[3],"native palettes not restored after draw");require(avatar.stats().posed_draws==1,"draw counter missing");
 originals();{auto draw=avatar.before_draw();}require(avatar.stats().cache_hits==1,"identical native uploads invalidated pose cache");
 integer(clip,-1);{auto draw=avatar.before_draw();require(read_head(current)==palettes[3],"NPC/third-person palette changed");}integer(clip,3);
 avatar.end_eye();{auto draw=avatar.before_draw();require(read_head(current)==palettes[3],"desktop palette changed outside XR scope");}
 targets.frame=2;avatar.begin_eye(targets,{42,AvatarVariant::male,true},targets.reference,bindings);{auto draw=avatar.before_draw();require(read_head(current)==palettes[3],"unrebased render space accepted");}avatar.camera_rebased(targets.reference);avatar.matrix_uploaded(current,1,false,palettes[0].data());{auto draw=avatar.before_draw();require(read_head(current)==palettes[3],"partial palette reused stale pose");}
 originals();avatar.program_linked(program);{auto draw=avatar.before_draw();require(read_head(current)==palettes[3],"relinked program reused stale uniform cache");}avatar.end_eye();
 // A depth-only shader must use the same local provenance without clip uniforms.
 auto depth=proc<GLuint(APIENTRY*)()>("glCreateProgram")();
 const char* dvs="#version 330 core\nuniform mat4 bone_matrices[19],mat_world;uniform int fixture_bone;void main(){gl_Position=mat_world*bone_matrices[clamp(fixture_bone,0,18)]*vec4(0,0,0,1);}";
 for(auto [kind,text]:std::array<std::pair<GLenum,const char*>,2>{{{0x8B31,dvs},{0x8B30,fs}}}){auto sh=create(kind);source(sh,1,&text,nullptr);compile(sh);GLint ok{};shader_int(sh,0x8B81,&ok);require(ok!=0,"depth shader compilation");attach(depth,sh);}link(depth);program_int(depth,0x8B82,&linked);require(linked!=0,"depth shader link");use(depth);
 const auto depth_bones=location(depth,"bone_matrices[0]"),depth_world=location(depth,"mat_world");avatar.configure(resolver,{rig(),rig()}); // Locations were resolved before this adapter/context saw the shader.
 upload(depth_bones,19,GL_FALSE,palettes[0].data());avatar.matrix_uploaded(depth_bones,19,false,palettes[0].data());upload(depth_world,1,GL_FALSE,model.data());avatar.matrix_uploaded(depth_world,1,false,model.data());
 targets.frame=3;avatar.begin_eye(targets,{42,AvatarVariant::male,true},targets.reference,bindings);avatar.camera_rebased(targets.reference);
 {auto draw=avatar.before_draw();Matrix4 m{};get(depth,depth_bones+3,m.data());require(std::abs(m[12]-.2f)<1e-5,"depth pass did not follow the local avatar pose");}
 Matrix4 restored{};get(depth,depth_bones+3,restored.data());require(restored==palettes[3]&&avatar.stats().depth_draws==1,"depth palette not restored/count missing");avatar.end_eye();
 const std::array<AvatarPaletteBinding,1> foreign{{{1,2}}};avatar.begin_eye(targets,{42,AvatarVariant::male,true},targets.reference,foreign);avatar.camera_rebased(targets.reference);{auto draw=avatar.before_draw();get(depth,depth_bones+3,restored.data());require(restored==palettes[3],"unregistered/NPC depth palette changed");}avatar.end_eye();
 // Shadow cascades upload a producer-owned copy of the palette, bound as depth-only.
 const auto shadow=palettes;upload(depth_bones,19,GL_FALSE,shadow[0].data());avatar.matrix_uploaded(depth_bones,19,false,shadow[0].data());
 const std::array<AvatarPaletteBinding,1> shadow_only{{{reinterpret_cast<std::uintptr_t>(shadow.data()),0,true}}};
 targets.frame=4;avatar.begin_eye(targets,{42,AvatarVariant::male,true},targets.reference,shadow_only);avatar.camera_rebased(targets.reference);
 {auto draw=avatar.before_draw();Matrix4 m{};get(depth,depth_bones+3,m.data());require(std::abs(m[12]-.2f)<1e-5,"shadow palette copy did not follow the local avatar pose");}
 get(depth,depth_bones+3,restored.data());require(restored==shadow[3]&&avatar.stats().depth_draws==2,"shadow palette not restored/count missing");avatar.end_eye();
 // A depth-only binding must never authorize a paired colour draw, even with equal tokens.
 use(program);originals();const std::array<AvatarPaletteBinding,1> colour_as_depth{{{reinterpret_cast<std::uintptr_t>(palettes.data()),reinterpret_cast<std::uintptr_t>(palettes.data()),true}}};
 targets.frame=5;avatar.begin_eye(targets,{42,AvatarVariant::male,true},targets.reference,colour_as_depth);avatar.camera_rebased(targets.reference);
 {auto draw=avatar.before_draw();require(read_head(current)==palettes[3],"depth-only binding posed a colour draw");}avatar.end_eye();
 // Proximity binding: an unselected (clip -1) copy of the local body is posed only
 // when its native head is within 0.35 m of the eyes, and its head selector is set
 // to the local one (3/.25) during the draw, then restored exactly.
 {use(program);originals();integer(clip,-1);scalar(weight,0.f);
  const std::array<AvatarPaletteBinding,1> near_copy{{{reinterpret_cast<std::uintptr_t>(palettes.data()),reinterpret_cast<std::uintptr_t>(palettes.data()),false,true}}};
  targets.frame=6;avatar.begin_eye(targets,{42,AvatarVariant::male,true},targets.reference,near_copy);avatar.camera_rebased(targets.reference);
  GLint seen_clip{};GLfloat seen_weight{};auto get_int=proc<void(APIENTRY*)(GLuint,GLint,GLint*)>("glGetUniformiv");
  {auto draw=avatar.before_draw();require(std::abs(read_head(current)[12]-.2f)<1e-5,"near copy not posed");get_int(program,clip,&seen_clip);get(program,weight,&seen_weight);
   require(seen_clip==3&&std::abs(seen_weight-.25f)<1e-6f,"near copy head selector not applied");}
  get_int(program,clip,&seen_clip);get(program,weight,&seen_weight);
  require(seen_clip==-1&&seen_weight==0.f&&read_head(current)==palettes[3],"near copy uniforms not restored");avatar.end_eye();
  auto distant=targets;distant.frame=7;distant.reference[12]=.4;distant.head=distant.reference;distant.head[12]=.6;
  avatar.begin_eye(distant,{42,AvatarVariant::male,true},distant.reference,near_copy);avatar.camera_rebased(distant.reference);
  {auto draw=avatar.before_draw();get_int(program,clip,&seen_clip);require(read_head(current)==palettes[3]&&seen_clip==-1,"distant unselected character posed");}
  avatar.end_eye();
  // A proximity binding never authorizes a draw that already carries the local selector, nor a depth draw.
  require(avatar.stats().proximity_posed==1&&avatar.stats().proximity_rejects==1,"proximity counters");
  integer(clip,3);scalar(weight,.25f);}
 // Rejection reasons are counted for skinned draws so a headset log explains a missing pass.
 const auto s=avatar.stats();
 require(s.skinned_unrebased==1,"skinned draw before camera rebase not counted");
 require(s.missing_inputs==1,"partial palette/world rejection not counted");
 require(s.clip_rejects==1,"non-local clip selector rejection not counted");
 require(s.paired_provenance_misses==1&&s.single_provenance_misses==1,"provenance misses not counted per pass kind");
 require(s.bound_depth_palettes==2,"bound shadow palettes not counted"); // shadow_only + colour_as_depth
 require(s.paired_seen>=s.posed_draws-s.depth_draws&&s.paired_seen>0,"paired skinned draws not counted");
 // Sampled journal: one entry per skinned draw decision in journaled eyes.
 {const auto j=avatar.journal();std::map<std::string,unsigned> by;for(const auto& e:j)++by[avatar_draw_decision_name(e.decision)];
  require(by["posed"]>=3&&by["unrebased"]==1&&by["missing_inputs"]==1&&by["clip"]==1&&by["provenance"]==2,"journal decisions incomplete");
  require(std::any_of(j.begin(),j.end(),[](const auto& e){return e.decision==AvatarDrawDecision::posed&&e.binding==AvatarBindingKind::depth&&!e.paired;}),"journal lacks posed shadow copy");
  require(std::all_of(j.begin(),j.end(),[](const auto& e){return e.upload_count==19||e.decision!=AvatarDrawDecision::posed;}),"journal upload count wrong");
  require(swvr::format_avatar_journal_row(j.front()).find('\t')!=std::string::npos,"journal row format");
  require(std::any_of(j.begin(),j.end(),[](const auto& e){return e.decision==AvatarDrawDecision::provenance&&e.paired&&e.clip==3&&std::abs(e.weight-.25f)<1e-6f;}),"unbound colour draw lacks head selector");
  avatar.set_journal(1,2);avatar.begin_eye(targets,{42,AvatarVariant::male,true},targets.reference,shadow_only);avatar.camera_rebased(targets.reference);
  for(int k=0;k<5;++k){auto draw=avatar.before_draw();}avatar.end_eye();require(avatar.journal().size()==2,"journal capacity not enforced");}
 // Native colour/depth producers may upload identical matrices from distinct
 // registered buffers. Tokens authorize a draw; they are not pose inputs.
 use(program);avatar.configure(resolver,{rig(),rig()});avatar.set_journal(0,0);originals();integer(clip,3);scalar(weight,.25f);
 auto copy=palettes;const std::array<AvatarPaletteBinding,2> copies{{bindings[0],{reinterpret_cast<std::uintptr_t>(copy.data()),reinterpret_cast<std::uintptr_t>(copy.data())}}};
 const auto uploads=[&](const auto& data){upload(current,19,GL_FALSE,data[0].data());avatar.matrix_uploaded(current,19,false,data[0].data());upload(old,19,GL_FALSE,data[0].data());avatar.matrix_uploaded(old,19,false,data[0].data());};
 const auto before=avatar.stats();double milliseconds{};
 for(unsigned batch=0;batch<10;++batch){targets.frame=20+batch;avatar.begin_eye(targets,{42,AvatarVariant::male,true},targets.reference,copies);avatar.camera_rebased(targets.reference);
  const auto start=std::chrono::steady_clock::now();
  for(unsigned draw=0;draw<40;++draw){uploads(draw%2?copy:palettes);{auto scope=avatar.before_draw();}}
  milliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();avatar.end_eye();}
 const auto measured=avatar.stats();std::cout<<"CACHE_BENCH draws="<<measured.posed_draws-before.posed_draws<<" hits="<<measured.cache_hits-before.cache_hits<<" ms="<<milliseconds<<'\n';
 require(measured.posed_draws-before.posed_draws==400&&measured.cache_hits-before.cache_hits==390,"equivalent registered palette copies invalidated pose cache");
 avatar.begin_eye(targets,{42,AvatarVariant::male,true},targets.reference,copies);avatar.camera_rebased(targets.reference);uploads(palettes);{auto scope=avatar.before_draw();}
 const auto foreign_copy=palettes;uploads(foreign_copy);{auto scope=avatar.before_draw();require(read_head(current)==palettes[3],"cache bypassed provenance for identical foreign data");}
 const auto hits=avatar.stats().cache_hits;copy[3][12]+=.01f;uploads(copy);{auto scope=avatar.before_draw();}
 require(avatar.stats().cache_hits==hits,"changed palette reused cached pose");avatar.end_eye();
 {const char* plain_vs="#version 330 core\nuniform mat4 mat_world;void main(){gl_Position=mat_world*vec4(0,0,0,1);}";const auto plain=proc<GLuint(APIENTRY*)()>("glCreateProgram")();
  for(auto [kind,text]:std::array<std::pair<GLenum,const char*>,2>{{{0x8B31,plain_vs},{0x8B30,fs}}}){auto sh=create(kind);source(sh,1,&text,nullptr);compile(sh);attach(plain,sh);}link(plain);GLint plain_linked{};program_int(plain,0x8B82,&plain_linked);require(plain_linked!=0,"plain shader link");use(plain);
  require(avatar.interest(context,plain)==GlProgramInterest::unknown,"unseen program interest");
  const auto plain_world=location(plain,"mat_world");upload(plain_world,1,GL_FALSE,model.data());avatar.matrix_uploaded(plain_world,1,false,model.data());
  require(avatar.interest(context,plain)==GlProgramInterest::none,"program without a palette not reported as skippable");
  avatar.program_linked(plain);require(avatar.interest(context,plain)==GlProgramInterest::unknown,"link did not reset program interest");use(program);}
 avatar.context_destroyed(context);wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);std::cout<<"Avatar GPU pose, exact restore, local gating, scope, cache and relink guards passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
