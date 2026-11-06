#include "swvr/driver_wheel_shader.hpp"
#include <regex>

namespace swvr {
HeadMaskShaderPatch patch_driver_wheel_vertex(std::string_view text){
    if(text.find('\0')!=text.npos||text.find("swvr_wheel_")!=text.npos||text.find("bone_")!=text.npos||text.find("velocity")!=text.npos)return {};
    std::string source(text);
    const bool opaque=text.find("in vec3 vertex_position_in;")!=text.npos&&text.find("in vec3 vertex_normal_in;")!=text.npos&&
        text.find("vertex_position_prev_out")!=text.npos&&text.find("vertex_position_next_out")!=text.npos;
    const bool depth=text.find("in vec3 vertexPosition_in;")!=text.npos&&text.find("vec4 vertexWorldPos = (mat_world * vec4(vertexPosition_in, 1));")!=text.npos;
    if((!opaque&&!depth)||text.find("uniform mat4 mat_world;")==text.npos)return {};
    const std::regex main(R"(void\s+main\s*\(\s*\)\s*\{)");std::smatch match;
    if(!std::regex_search(source,match,main))return {};
    const auto at=static_cast<std::size_t>(match.position()),start=at+static_cast<std::size_t>(match.length());
    std::string body=source.substr(start);
    if(std::regex_search(body,main))return {};
    // Only identifiers in main are replaced; declarations and preprocessor stay intact.
    const char* position=opaque?"vertex_position_in":"vertexPosition_in";
    if(opaque){
        const std::regex previous(R"(mat_world_prev\s*\*\s*vec4\(\s*vertex_position_in\s*,\s*1\.0\s*\))");
        if(!std::regex_search(body,previous))return {};
        body=std::regex_replace(body,previous,"mat_world_prev * vec4(swvr_wheel_previous, 1.0)");
        body=std::regex_replace(body,std::regex(R"(\bvertex_normal_in\b)"),"swvr_wheel_normal");
    }
    body=std::regex_replace(body,std::regex(std::string("\\b")+position+"\\b"),"swvr_wheel_position");
    const char* helper=R"(
uniform int swvr_wheel_active;
uniform int swvr_wheel_count;
uniform vec3 swvr_wheel_points[192];
uniform mat4 swvr_wheel_to_seat;
uniform mat4 swvr_wheel_to_model;
uniform float swvr_wheel_angle;
uniform float swvr_wheel_old_angle;
const vec3 swvr_wheel_center = vec3(0.0000105,0.4817497,0.2528192);
const vec3 swvr_wheel_axis = vec3(0.0,0.2588190451,-0.9659258263);
bool swvr_wheel_match(vec3 p) {
    if(swvr_wheel_active==0) return false;
    if(any(lessThan(p,vec3(-0.226,0.260,0.177))) || any(greaterThan(p,vec3(0.226,0.703,0.391)))) return false;
    for(int i=0;i<swvr_wheel_count;++i) {
        vec3 d=p-swvr_wheel_points[i];
        if(dot(d,d)<=0.0000005625) return true;
    }
    return false;
}
vec3 swvr_wheel_rotate(vec3 p,float angle) {
    float c=cos(angle),s=sin(angle);
    return p*c+cross(swvr_wheel_axis,p)*s+swvr_wheel_axis*dot(swvr_wheel_axis,p)*(1.0-c);
}
vec3 swvr_wheel_moved(vec3 p,float angle) {
    return (swvr_wheel_to_model*vec4(swvr_wheel_center+swvr_wheel_rotate(p-swvr_wheel_center,angle),1.0)).xyz;
}
)";
    std::string setup="\nvec3 swvr_wheel_local=(swvr_wheel_to_seat*vec4("+std::string(position)+",1.0)).xyz;\n"
        "bool swvr_wheel_selected=swvr_wheel_match(swvr_wheel_local);\n"
        "vec3 swvr_wheel_position=swvr_wheel_selected?swvr_wheel_moved(swvr_wheel_local,swvr_wheel_angle):"+position+";\n";
    if(opaque)setup+="vec3 swvr_wheel_previous=swvr_wheel_selected?swvr_wheel_moved(swvr_wheel_local,swvr_wheel_old_angle):vertex_position_in;\n"
        "vec3 swvr_wheel_normal=swvr_wheel_selected?(swvr_wheel_to_model*vec4(swvr_wheel_rotate((swvr_wheel_to_seat*vec4(vertex_normal_in,0.0)).xyz,swvr_wheel_angle),0.0)).xyz:vertex_normal_in;\n";
    return {true,source.substr(0,at)+helper+source.substr(at,start-at)+setup+body};
}
}
