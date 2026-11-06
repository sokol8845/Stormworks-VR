#include "swvr/head_mask_sources.hpp"
#include "swvr/driver_wheel_shader.hpp"
#include "driver_wheel_fixture.hpp"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}
std::map<unsigned,std::string> submitted;std::map<unsigned,int> type,compiled;unsigned compiles{};
std::vector<std::string> notes;
void source(unsigned shader,int count,const char* const* strings,const int* lengths){
    std::string s;for(int i=0;i<count;++i)s.append(strings[i],lengths&&lengths[i]>=0?static_cast<std::size_t>(lengths[i]):std::strlen(strings[i]));submitted[shader]=s;}
void compile(unsigned){++compiles;}
void get_shader(unsigned shader,unsigned pname,int* value){*value=pname==0x8B4F?type[shader]:compiled[shader];}
void note(const char* key,const char* message) noexcept {notes.push_back(std::string(key)+":"+message);}
std::string fragment(){
    return "#include \"depth_utils.glslh\"\nin float log_z;\nin vec4 vertex_color_out;\nout vec4 gnormal_light_factor_out;\nout vec4 gcolor_out;\n"
           "void main()\n{\n    gl_FragDepth = log_z_to_frag_depth(log_z);\n    if(vertex_color_out.a <= 0 && decode_depth_linear(gl_FragDepth) < 0.25)\n    {\n        discard;\n    }\n}\n";
}
}

int main(){try{
    swvr::HeadMaskShaderSources sources;const swvr::HeadMaskShaderApi api{source,compile,get_shader};
    constexpr std::uintptr_t ctx=0x10,other=0x20;
    const auto text=fragment();const char* parts[2]{text.c_str(),text.c_str()+40};const int lengths[2]{40,static_cast<int>(text.size()-40)};

    // A vertex shader is never touched and leaves no backup.
    type[1]=0x8B31;require(!sources.submit(ctx,1,2,parts,lengths,api,note)&&submitted.empty()&&sources.backups()==0,"vertex shader patched");
    // A known fragment is joined, patched and submitted once by the injector.
    type[2]=0x8B30;require(sources.submit(ctx,2,2,parts,lengths,api,note),"known fragment not patched");
    require(submitted[2].find("swvr_vr_head_mask")!=std::string::npos&&sources.patched()==1&&sources.backups()==1,"patched source/backup missing");
    // Successful compile keeps the patch; a failed compile restores the original once.
    compiled[2]=1;sources.compiled(ctx,2,api,note);require(sources.fallbacks()==0&&compiles==0,"successful compile fell back");
    compiled[2]=0;sources.compiled(ctx,2,api,note);
    require(submitted[2]==text&&compiles==1&&sources.fallbacks()==1,"failed compile did not restore the original");
    // Unknown fragments are left alone and supersede an older backup.
    const std::string unknown="void main(){}";const char* u=unknown.c_str();
    require(!sources.submit(ctx,2,1,&u,nullptr,api,note)&&sources.backups()==0,"unsupported resubmission kept stale backup");
    // Backups are per context; forgetting and context teardown release them.
    type[3]=0x8B30;require(sources.submit(ctx,3,2,parts,lengths,api,note)&&sources.submit(other,3,2,parts,lengths,api,note)&&sources.backups()==2,"per-context backups");
    sources.forget(ctx,3);require(sources.backups()==1,"forget released wrong entry");
    sources.context_destroyed(other);require(sources.backups()==0,"context teardown kept backups");
    compiled[3]=0;const auto before=compiles;sources.compiled(other,3,api,note);require(compiles==before,"fallback without backup");
    // At most 256 backups; beyond that the original source is used unchanged.
    for(unsigned s=100;s<356;++s){type[s]=0x8B30;require(sources.submit(ctx,s,2,parts,lengths,api,note),"backup budget too small");}
    type[999]=0x8B30;require(!sources.submit(ctx,999,2,parts,lengths,api,note)&&sources.backups()==256,"backup budget exceeded");
    // Malformed input is refused without touching GL.
    const char* null_part[1]{nullptr};require(!sources.submit(ctx,4,1,null_part,nullptr,{source,compile,nullptr},note),"null source accepted");
    swvr::HeadMaskShaderSources wheels{0x8B31,swvr::patch_driver_wheel_vertex,"driver_wheel_shader"};
    const auto vertex=wheel_fixture::opaque();const char* v=vertex.c_str();type[1000]=0x8B31;
    require(wheels.submit(ctx,1000,1,&v,nullptr,api,note)&&submitted[1000].find("swvr_wheel_active")!=std::string::npos,"wheel source injection failed");
    compiled[1000]=0;wheels.compiled(ctx,1000,api,note);require(submitted[1000]==vertex&&wheels.fallbacks()==1,"wheel compile fallback lost original");
    type[1001]=0x8B30;require(!wheels.submit(ctx,1001,1,&v,nullptr,api,note),"wheel injector accepted fragment shader");
    wheels.context_destroyed(ctx);require(wheels.backups()==0,"wheel context retained backup");
    std::cout<<"PASS: head-mask/wheel shader injection, compile fallback and bounded backups\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
