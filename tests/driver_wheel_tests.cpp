#include "driver_wheel_fixture.hpp"
#include "swvr/driver_wheel_shader.hpp"
#include "swvr/head_mask_sources.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace swvr;
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    auto mesh=argc>1?load_driver_seat_mesh(argv[1]):wheel_fixture::mesh();
    for(std::size_t i=0;i<mesh.positions.size();++i)check(driver_wheel_contains(mesh.points,mesh.positions[i])==mesh.wheel[i],"wheel mask moves another piece or misses wheel");
    check(!driver_wheel_contains(mesh.points,{0,.4f,.5f}),"fixed steering column selected");
    const auto p=rotate_driver_wheel({driver_wheel_center.x+.2f,driver_wheel_center.y,driver_wheel_center.z},1.5707963268f);
    check(std::abs(p.x-driver_wheel_center.x)<1e-6f&&p.y<driver_wheel_center.y-.19f,"clockwise steering visual sign");
    auto basis=wheel_fixture::identity();basis[10]=-1;basis[12]=10000;basis[14]=-20000;
    const auto roundtrip=driver_wheel_product(basis,driver_wheel_inverse(basis));check(roundtrip==wheel_fixture::identity(),"reflected native transform inverse");
    basis[0]=2;check(!driver_wheel_rigid(basis),"scaled matrix accepted");
    for(auto text:{wheel_fixture::opaque(),wheel_fixture::depth()}){
        auto patch=patch_driver_wheel_vertex(text);check(patch.patched,"supported shader not patched");check(!patch_driver_wheel_vertex(patch.source).patched,"double injected shader");
        check(patch.source.find("#version")==0,"version displaced");check(!patch_driver_wheel_vertex(text+"\n// bone_matrices").patched,"skinned shader accepted");
        check(!patch_driver_wheel_vertex(text+std::string(1,'\0')).patched,"NUL shader accepted");
    }
    check(!patch_driver_wheel_vertex("void main(){}").patched,"unrelated shader patched");
    bool rejected{};try{read_driver_seat_mesh({});}catch(const std::runtime_error&){rejected=true;}check(rejected,"empty mesh accepted");
    std::cout<<"PASS Driver wheel topology, axis, transform and shader selection; vertices="<<mesh.positions.size()<<" wheel="<<std::count(mesh.wheel.begin(),mesh.wheel.end(),true)<<" points="<<mesh.points.size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
