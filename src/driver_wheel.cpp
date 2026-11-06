#include "swvr/driver_wheel.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <stdexcept>

namespace swvr {
namespace {
template<class T>T read(std::span<const unsigned char> bytes,std::size_t offset){
    if(offset>bytes.size()||sizeof(T)>bytes.size()-offset)throw std::runtime_error("Truncated Driver seat mesh");
    T value{};std::memcpy(&value,bytes.data()+offset,sizeof(value));return value;
}
std::array<int,3> key(Vec3 p){return {static_cast<int>(std::lround(p.x*100000)),static_cast<int>(std::lround(p.y*100000)),static_cast<int>(std::lround(p.z*100000))};}
}
DriverWheelMesh read_driver_seat_mesh(std::span<const unsigned char> bytes){
    std::uint64_t hash=14695981039346656037ULL;for(auto b:bytes)hash=(hash^b)*1099511628211ULL;
    if(bytes.size()!=80198||hash!=driver_seat_mesh_fingerprint)throw std::runtime_error("Unsupported Driver seat mesh fingerprint");
    if(read<std::uint32_t>(bytes,0)!=0x6873656d||read<std::uint16_t>(bytes,4)!=7||read<std::uint16_t>(bytes,6)!=1||read<std::uint32_t>(bytes,10)!=19)
        throw std::runtime_error("Unsupported Driver seat vertex layout");
    const auto count=read<std::uint16_t>(bytes,8);DriverWheelMesh out;
    std::vector<std::size_t> parent(count);std::iota(parent.begin(),parent.end(),0);
    const auto root=[&](std::size_t i){while(i!=parent[i]){parent[i]=parent[parent[i]];i=parent[i];}return i;};
    const auto join=[&](std::size_t a,std::size_t b){parent[root(b)]=root(a);};
    std::map<std::array<int,3>,std::size_t> welded;
    std::vector<bool> black(count);
    for(std::size_t i=0;i<count;++i){
        const auto at=14+i*28;const auto p=read<Vec3>(bytes,at),n=read<Vec3>(bytes,at+16);
        out.positions.push_back(p);out.normals.push_back(n);
        const auto [previous,inserted]=welded.emplace(key(p),i);if(!inserted)join(i,previous->second);
        black[i]=bytes[at+12]==30&&bytes[at+13]==30&&bytes[at+14]==30;
    }
    const auto at=14+static_cast<std::size_t>(count)*28;const auto indices=read<std::uint32_t>(bytes,at);
    if(indices!=4248)throw std::runtime_error("Unsupported Driver seat triangles");
    for(std::size_t i=0;i<indices;++i){const auto index=read<std::uint16_t>(bytes,at+4+i*2);if(index>=count)throw std::runtime_error("Invalid Driver seat index");out.indices.push_back(index);}
    for(std::size_t i=0;i<indices;i+=3){join(out.indices[i],out.indices[i+1]);join(out.indices[i],out.indices[i+2]);}
    std::vector<bool> groups(count);for(std::size_t i=0;i<count;++i)if(black[i])groups[root(i)]=true;
    out.wheel.resize(count);std::map<std::array<int,3>,Vec3> unique;
    for(std::size_t i=0;i<count;++i)if((out.wheel[i]=groups[root(i)]))unique.emplace(key(out.positions[i]),out.positions[i]);
    for(const auto& [k,p]:unique){static_cast<void>(k);out.points.push_back(p);}
    // Entire connected pieces, including the small grey hub caps, move together.
    if(std::count(out.wheel.begin(),out.wheel.end(),true)!=606||out.points.empty()||out.points.size()>driver_wheel_point_limit)
        throw std::runtime_error("Driver wheel topology differs from verified mesh");
    return out;
}
DriverWheelMesh load_driver_seat_mesh(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file||file.tellg()!=80198)throw std::runtime_error("Driver seat mesh unavailable or changed");
    std::vector<unsigned char> bytes(80198);file.seekg(0);if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("Cannot read Driver seat mesh");
    return read_driver_seat_mesh(bytes);
}
Vec3 rotate_driver_wheel(Vec3 p,float angle,bool direction){
    if(!std::isfinite(angle))throw std::invalid_argument("Nonfinite wheel angle");
    if(!direction){p.x-=driver_wheel_center.x;p.y-=driver_wheel_center.y;p.z-=driver_wheel_center.z;}
    const auto a=driver_wheel_axis;const float c=std::cos(angle),s=std::sin(angle),d=a.x*p.x+a.y*p.y+a.z*p.z;
    Vec3 q{p.x*c+(a.y*p.z-a.z*p.y)*s+a.x*d*(1-c),p.y*c+(a.z*p.x-a.x*p.z)*s+a.y*d*(1-c),p.z*c+(a.x*p.y-a.y*p.x)*s+a.z*d*(1-c)};
    if(!direction){q.x+=driver_wheel_center.x;q.y+=driver_wheel_center.y;q.z+=driver_wheel_center.z;}return q;
}
bool driver_wheel_contains(std::span<const Vec3> points,Vec3 p) noexcept {
    for(auto v:points){const float x=v.x-p.x,y=v.y-p.y,z=v.z-p.z;if(x*x+y*y+z*z<=driver_wheel_match_m*driver_wheel_match_m)return true;}return false;
}
bool driver_wheel_rigid(const Matrix4d& m) noexcept {
    for(auto x:m)if(!std::isfinite(x))return false;
    if(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)>1e-5)return false;
    for(int a=0;a<3;++a)for(int b=0;b<3;++b){double dot{};for(int row=0;row<3;++row)dot+=m[a*4+row]*m[b*4+row];if(std::abs(dot-(a==b?1.:0.))>1e-4)return false;}return true;
}
Matrix4d driver_wheel_product(const Matrix4d& a,const Matrix4d& b){Matrix4d out{};for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)out[c*4+r]+=a[k*4+r]*b[c*4+k];return out;}
Matrix4d driver_wheel_inverse(const Matrix4d& m){
    if(!driver_wheel_rigid(m))throw std::invalid_argument("Non-rigid wheel transform");
    Matrix4d out{};out[15]=1;for(int c=0;c<3;++c)for(int r=0;r<3;++r)out[c*4+r]=m[r*4+c];
    for(int r=0;r<3;++r)for(int k=0;k<3;++k)out[12+r]-=out[k*4+r]*m[12+k];return out;
}
Vec3 driver_wheel_transform(const Matrix4d& m,Vec3 p,bool direction){
    return {static_cast<float>(m[0]*p.x+m[4]*p.y+m[8]*p.z+(direction?0:m[12])),static_cast<float>(m[1]*p.x+m[5]*p.y+m[9]*p.z+(direction?0:m[13])),static_cast<float>(m[2]*p.x+m[6]*p.y+m[10]*p.z+(direction?0:m[14]))};
}
Matrix4d driver_seat_transform(const Matrix4d& section,const std::array<float,3>& offset,const std::array<std::int32_t,3>& grid,const std::array<std::int32_t,9>& rotation){
    if(!driver_wheel_rigid(section))throw std::invalid_argument("Invalid seat section transform");
    Matrix4d local{};local[15]=1;
    for(int r=0;r<3;++r){for(int c=0;c<3;++c){const auto v=rotation[r*3+c];if(v< -1||v>1)throw std::invalid_argument("Invalid component rotation");local[c*4+r]=v;}
        if(!std::isfinite(offset[r])||std::abs(static_cast<double>(grid[r]))>1000000)throw std::invalid_argument("Invalid component grid");local[12+r]=grid[r]*.25-offset[r];}
    if(!driver_wheel_rigid(local))throw std::invalid_argument("Invalid component basis");return driver_wheel_product(section,local);
}
}
