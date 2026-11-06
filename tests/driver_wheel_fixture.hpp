#pragma once
#include "swvr/driver_wheel.hpp"
#include <fstream>
#include <string>
namespace wheel_fixture {
inline swvr::Matrix4d identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
inline swvr::DriverWheelMesh mesh(){
    swvr::DriverWheelMesh m;m.positions={{.2f,.4817497f,.2528192f},{0,.29f,.21f},{0,.48f,.35f},{0,.4f,.5f},{.45f,.4817497f,.2528192f},{0,0,0}};
    m.normals.assign(m.positions.size(),{.3f,.4f,.8660254f});m.wheel={true,true,true,false,false,false};m.points.assign(m.positions.begin(),m.positions.begin()+3);return m;
}
inline std::string opaque(){return R"(#version 330 core
in vec3 vertex_position_in;
in vec3 vertex_normal_in;
uniform mat4 mat_world;
uniform mat4 mat_world_prev,mat_world_next,mat_view_proj_prev,mat_view_proj_next;
out vec3 vertex_world_position_out,vertex_normal_out;
out vec4 vertex_position_prev_out,vertex_position_next_out;
void main(){
vertex_normal_out=(mat_world*vec4(vertex_normal_in,0.0)).xyz;
vertex_world_position_out=(mat_world*vec4(vertex_position_in,1.0)).xyz;
vertex_position_prev_out=mat_view_proj_prev*(mat_world_prev * vec4(vertex_position_in, 1.0));
vertex_position_next_out=mat_view_proj_next*(mat_world_next*vec4(vertex_position_in,1.0));
gl_Position=vec4(vertex_world_position_out,1);
})";}
inline std::string depth(){return R"(#version 330 core
in vec3 vertexPosition_in;
uniform mat4 mat_world;
uniform mat4 mat_view_proj;
void main(){
vec4 vertexWorldPos = (mat_world * vec4(vertexPosition_in, 1));
gl_Position=mat_view_proj*vertexWorldPos;
})";}
inline std::string shader(const std::filesystem::path& path){
    std::ifstream file(path);if(!file)throw std::runtime_error("Shader not found");std::string text((std::istreambuf_iterator<char>(file)),{});
    const std::string include="#include \"depth_utils.glslh\"";
    const auto at=text.find(include);if(at!=text.npos)text.replace(at,include.size(),shader(path.parent_path()/"depth_utils.glslh"));return text;
}
}
