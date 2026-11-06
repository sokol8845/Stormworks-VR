#include "swvr/head_mask_shader.hpp"
#include <array>
#include <cctype>
#include <sstream>

namespace swvr {
namespace {
constexpr std::string_view predicate="if(vertex_color_out.a <= 0 && decode_depth_linear(gl_FragDepth) < 0.25)";
constexpr std::string_view replacement="if(vertex_color_out.a <= 0 && (swvr_vr_head_mask != 0 || decode_depth_linear(gl_FragDepth) < 0.25))";
constexpr std::string_view main_signature="void main()";
constexpr std::string_view declaration="uniform int swvr_vr_head_mask = 0;\n\n";
bool space(char c) {return std::isspace(static_cast<unsigned char>(c))!=0;}
std::size_t skip_space(std::string_view value,std::size_t at) {
    while(at<value.size() && space(value[at]))++at;
    return at;
}
bool once(std::string_view value,std::string_view needle) {
    const auto at=value.find(needle);
    return at!=std::string_view::npos && value.find(needle,at+needle.size())==std::string_view::npos;
}
// Mask comments and quoted include names while keeping byte offsets/newlines.
// An unterminated comment/string is unsupported, not something to repair.
bool code_only(std::string_view source,std::string& code) {
    code=source;
    const auto mask=[&](std::size_t i) {if(code[i]!='\n' && code[i]!='\r')code[i]=' ';};
    for(std::size_t i=0;i<source.size();) {
        if(i+1<source.size() && source[i]=='/' && source[i+1]=='/') {
            while(i<source.size() && source[i]!='\n')mask(i++);
        } else if(i+1<source.size() && source[i]=='/' && source[i+1]=='*') {
            mask(i++);mask(i++);bool closed{};
            while(i<source.size()) {
                if(i+1<source.size() && source[i]=='*' && source[i+1]=='/') {mask(i++);mask(i++);closed=true;break;}
                mask(i++);
            }
            if(!closed)return false;
        } else if(source[i]=='"') {
            mask(i++);bool closed{};
            while(i<source.size() && source[i]!='\n') {
                if(source[i]=='\\' && i+1<source.size()){mask(i++);mask(i++);continue;}
                if(source[i]=='"'){mask(i++);closed=true;break;}
                mask(i++);
            }
            if(!closed)return false;
        } else ++i;
    }
    return true;
}
bool version_supported(std::string_view code) {
    constexpr std::array<std::string_view,11> versions{"130","140","150","330","400","410","420","430","440","450","460"};
    const auto first=skip_space(code,0);bool found{};
    for(std::size_t start=0;start<code.size();) {
        const auto end=code.find('\n',start);const auto limit=end==std::string_view::npos?code.size():end;
        const auto at=skip_space(code,start);
        if(at<limit && code[at]=='#') {
            const auto word_at=skip_space(code,at+1);auto word_end=word_at;
            while(word_end<limit && std::isalpha(static_cast<unsigned char>(code[word_end])))++word_end;
            if(code.substr(word_at,word_end-word_at)=="version") {
                if(found || at!=first || word_at!=at+1)return false;
                found=true;
                std::istringstream tokens{std::string(code.substr(word_end,limit-word_end))};
                std::string version,profile,extra;tokens>>version;
                bool allowed{};for(const auto known:versions)allowed|=version==known;
                if(!allowed)return false;
                if(tokens>>profile) {
                    if((profile!="core" && profile!="compatibility") || version=="130" || version=="140")return false;
                    if(tokens>>extra)return false;
                }
            }
        }
        if(end==std::string_view::npos)break;
        start=end+1;
    }
    // Headerless input is only a game include template, not arbitrary old GLSL.
    return found || code.substr(first,8)=="#include";
}
bool known_fragment(std::string_view code) {
    if(!once(code,"in float log_z;") || !once(code,"in vec4 vertex_color_out;") ||
       !once(code,"gl_FragDepth = log_z_to_frag_depth(log_z);"))return false;
    const bool opaque=code.find("out vec4 gnormal_light_factor_out;")!=std::string_view::npos &&
                      code.find("out vec4 gcolor_out;")!=std::string_view::npos;
    const bool glass=code.find("out vec4 color_out;")!=std::string_view::npos &&
                     code.find("uniform sampler2D texture_water_depth;")!=std::string_view::npos &&
                     code.find("uniform vec3 camera_position;")!=std::string_view::npos;
    const bool additive=code.find("out vec4 color_out;")!=std::string_view::npos &&
                        code.find("uniform float additive_factor;")!=std::string_view::npos;
    return static_cast<unsigned>(opaque)+static_cast<unsigned>(glass)+static_cast<unsigned>(additive)==1;
}
bool structure_supported(std::string_view code,std::size_t main,std::size_t target) {
    const auto open=skip_space(code,main+main_signature.size());
    if(open>=code.size() || code[open]!='{')return false;
    int depth{};std::size_t main_end=std::string_view::npos;
    for(std::size_t i=0;i<code.size();++i) {
        if(code[i]=='{')++depth;
        if(code[i]=='}') {if(--depth<0)return false;if(i>open && depth==0 && main_end==std::string_view::npos)main_end=i;}
        if(i==open && depth!=1)return false; // main must be a top-level function.
    }
    if(depth!=0 || target<=open || target>=main_end || main_end==std::string_view::npos)return false;
    auto at=skip_space(code,target+predicate.size());
    // Accept only the known discard-only block; no replacement of arbitrary code.
    for(const auto token:{std::string_view("{"),std::string_view("discard"),std::string_view(";"),std::string_view("}")}) {
        if(code.substr(at,token.size())!=token)return false;
        at=skip_space(code,at+token.size());
    }
    return true;
}
}
HeadMaskShaderPatch patch_head_mask_fragment(std::string_view source) {
    HeadMaskShaderPatch result{false,std::string(source)};
    if(source.find('\0')!=std::string_view::npos || source.find(head_mask_uniform_name)!=std::string_view::npos)return result;
    std::string code;
    if(!code_only(source,code) || !version_supported(code) || !known_fragment(code) ||
       !once(code,predicate) || !once(code,main_signature))return result;
    const auto main=code.find(main_signature),target=code.find(predicate);
    if(!structure_supported(code,main,target))return result;
    result.source.replace(target,predicate.size(),replacement);
    result.source.insert(main,declaration);
    result.patched=true;
    return result;
}
}
