#include "swvr/avatar_profile.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace swvr;
namespace {void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}template<class F>void rejects(F f){bool caught{};try{f();}catch(const std::exception&){caught=true;}check(caught,"invalid profile accepted");}}
int main(int argc,char** argv){try{
    check(argc==2,"temporary directory missing");const auto directory=std::filesystem::absolute(argv[1])/("profile-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(!std::filesystem::exists(directory),"temporary path collision");std::filesystem::create_directories(directory);
    struct Clean{std::filesystem::path p;~Clean(){std::error_code e;std::filesystem::remove_all(p,e);}} clean{directory};
    AvatarBodyCalibration body{0xdeadbeef,44,88,AvatarVariant::female,1.653456789,{.01,-.31,.12}};
    const auto p=make_avatar_profile(body);const auto text=format_avatar_profile(p);
    check(text.find("actor")==text.npos&&text.find("revision")==text.npos&&text.find("generation")==text.npos,"native identity persisted");
    const auto round=parse_avatar_profile(text);check(round.wrist_span==p.wrist_span&&round.shoulder_offset==p.shoulder_offset,"profile precision lost");
    const auto bound=bind_avatar_profile(round,AvatarVariant::female,77,5,1);
    check(bound&&bound->actor==77&&bound->space_revision==5&&bound->generation==1&&bound->wrist_span==body.wrist_span,"profile did not bind a fresh identity");
    check(!bind_avatar_profile(round,AvatarVariant::male,77,5,1)&&!bind_avatar_profile(round,AvatarVariant::female,0,5,1),"wrong model or absent actor accepted");
    for(const auto invalid:{text+"actor=123\n",text+"schema=1\n",std::string("schema=99\n"),std::string("wrist_span_m=nan\n")})rejects([&]{parse_avatar_profile(invalid);});
    auto bad=p;bad.wrist_span=std::numeric_limits<double>::infinity();rejects([&]{format_avatar_profile(bad);});
    const auto path=directory/"avatar.ini";check(!load_avatar_profile(path),"missing profile looked loaded");
    save_avatar_profile(path,p,directory/"staging");check(load_avatar_profile(path)->wrist_span==p.wrist_span,"saved profile not readable");
    auto newer=p;newer.wrist_span=1.7;save_avatar_profile(path,newer,directory/"staging");check(load_avatar_profile(path)->wrist_span==1.7,"atomic replacement did not update profile");
    rejects([&]{save_avatar_profile(path,bad,directory/"staging");});check(load_avatar_profile(path)->wrist_span==1.7,"rejected save damaged prior profile");
    check(std::filesystem::is_empty(directory/"staging"),"staging files leaked");
    std::filesystem::remove(path);check(!load_avatar_profile(path),"deleted profile still loaded");
    std::cout<<"PASS avatar profile: validation, precision, no native identities, live rebinding, atomic replace and deletion\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
