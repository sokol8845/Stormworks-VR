#include "swvr/head_mask_shader.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
constexpr std::string_view original="if(vertex_color_out.a <= 0 && decode_depth_linear(gl_FragDepth) < 0.25)";
constexpr std::string_view patched="if(vertex_color_out.a <= 0 && (swvr_vr_head_mask != 0 || decode_depth_linear(gl_FragDepth) < 0.25))";
constexpr std::string_view uniform="uniform int swvr_vr_head_mask = 0;\n\n";
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::string fixture(unsigned type) {
    std::string source="#include \"depth_utils.glslh\"\nin float log_z;\nin vec4 vertex_color_out;\n";
    if(type==0)source+="out vec4 gnormal_light_factor_out;\nout vec4 gcolor_out;\n";
    if(type==1)source+="out vec4 color_out;\nuniform sampler2D texture_water_depth;\nuniform vec3 camera_position;\n";
    if(type==2)source+="out vec4 color_out;\nuniform float additive_factor;\n";
    source+="void main()\n{\n    gl_FragDepth = log_z_to_frag_depth(log_z);\n    ";source+=original;
    source+="\n    {\n        discard;\n    }\n}\n";return source;
}
void rejected(const std::string& source){const auto result=swvr::patch_head_mask_fragment(source);require(!result.patched && result.source==source,"unsupported input must remain byte-for-byte unchanged");}
void accepted(const std::string& source) {
    const auto result=swvr::patch_head_mask_fragment(source);require(result.patched,"known animated fragment rejected");
    require(result.source.find(uniform)!=std::string::npos && result.source.find(patched)!=std::string::npos,"default-zero uniform or exact predicate absent");
    auto restored=result.source;restored.erase(restored.find(uniform),uniform.size());restored.replace(restored.find(patched),patched.size(),original);
    require(restored==source,"unrelated shader bytes changed");rejected(result.source);
}
}
int main(int argc,char** argv) {
    try {
        for(unsigned type=0;type<3;++type) {
            const auto source=fixture(type);accepted(source);
            accepted("#version 330 core\n"+source);accepted("// header\n#version 150\n"+source);
            accepted("#version 460 compatibility\n#define VELOCITY_ENABLED 1\n"+source);
            auto crlf=source;for(std::size_t i=0;(i=crlf.find('\n',i))!=std::string::npos;i+=2)crlf.insert(i,"\r");accepted(crlf);
        }
        const auto source=fixture(0);
        rejected("");rejected("void main(){}");rejected("/*"+source+"*/");rejected(source+"/* unterminated");
        rejected(source+"\nvoid main(){}\n");rejected(source+"\n}");rejected(source.substr(0,source.size()-2));
        rejected("#version 100\n"+source);rejected("#version 300 es\n"+source);rejected("#version 999\n"+source);
        rejected("#version 330 core junk\n"+source);rejected("#version 140 core\n"+source);
        rejected("#version 330\n#version 330\n"+source);rejected("#define X 1\n#version 330\n"+source);
        rejected("# version 330\n"+source);rejected(source+"\n#version 330\n");
        rejected("// swvr_vr_head_mask already mentioned\n"+source);
        auto changed=source;changed.replace(changed.find("< 0.25"),6,"< 0.50");rejected(changed);
        changed=source;changed.replace(changed.find("discard;"),8,"discard; color=vec4(0);");rejected(changed);
        changed=source;changed.insert(changed.find(original),std::string(original)+" { discard; }\n");rejected(changed);
        changed=source;changed.replace(changed.find("out vec4 gcolor_out;"),20,"out vec4 unknown;");rejected(changed);
        changed=source;changed.push_back('\0');rejected(changed);
        accepted("// A commented fake void main() and "+std::string(original)+"\n"+source);
        // Original alpha gate remains mandatory. With mode=0, truth table is
        // exactly the original nearby-only rule. Mode=1 only removes its depth gate.
        for(bool alpha_zero:{false,true})for(bool nearby:{false,true})for(bool mode:{false,true}) {
            const bool old_rule=alpha_zero && nearby,new_rule=alpha_zero && (mode || nearby);
            if(!mode)require(old_rule==new_rule,"normal mode predicate changed");
            if(!alpha_zero)require(!new_rule,"unmasked vertices must remain visible");
        }
        unsigned installed{};
        if(argc==2)for(const char* name:{"opaque_anim.glslf","glass_anim.glslf","additive_anim.glslf"}) {
            std::ifstream input(std::filesystem::path(argv[1])/name,std::ios::binary);require(input.good(),"cannot read installed shader fixture");
            std::ostringstream buffer;buffer<<input.rdbuf();accepted(buffer.str());++installed;
        }
        require(argc<=2,"usage: head_mask_shader_tests [installed-shader-directory]");
        std::cout<<"PASS head-mask source patch: three variants, defaults, strict refusals, idempotency; installed fixtures="<<installed<<"\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
