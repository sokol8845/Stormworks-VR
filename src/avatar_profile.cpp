#include "swvr/avatar_profile.hpp"
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace swvr {
namespace {
std::string_view trim(std::string_view v){const auto at=v.find_first_not_of(" \t\r\n");return at==v.npos?std::string_view{}:v.substr(at,v.find_last_not_of(" \t\r\n")-at+1);}
void require(bool good,const char* text){if(!good)throw std::invalid_argument(text);}
double number(std::string_view raw){double value{};const auto r=std::from_chars(raw.data(),raw.data()+raw.size(),value);require(r.ec==std::errc{}&&r.ptr==raw.data()+raw.size()&&std::isfinite(value),"Invalid profile number");return value;}
void range(double n,double lo,double hi){require(std::isfinite(n)&&n>=lo&&n<=hi,"Avatar profile proportions out of range");}
}
void validate_avatar_profile(const AvatarProfile& p){
    require(p.variant==AvatarVariant::male||p.variant==AvatarVariant::female,"Invalid avatar variant");
    range(p.wrist_span,.8,2.4);range(p.shoulder_offset[0],-.15,.15);range(p.shoulder_offset[1],-.65,-.12);range(p.shoulder_offset[2],-.35,.35);
}
AvatarProfile make_avatar_profile(const AvatarBodyCalibration& body){AvatarProfile p{body.variant,body.wrist_span,body.shoulder_offset};validate_avatar_profile(p);return p;}
AvatarProfile parse_avatar_profile(std::string_view text){
    require(text.size()<=4096,"Avatar profile too large");if(text.starts_with("\xef\xbb\xbf"))text.remove_prefix(3);
    std::map<std::string,std::string> fields;
    while(!text.empty()) {
        const auto end=text.find('\n');auto line=trim(text.substr(0,end));text=end==text.npos?std::string_view{}:text.substr(end+1);
        if(line.empty()||line.front()=='#')continue;
        const auto eq=line.find('=');require(eq!=line.npos,"Expected profile key=value");
        require(fields.emplace(std::string(trim(line.substr(0,eq))),std::string(trim(line.substr(eq+1)))).second,"Duplicate profile field");
    }
    require(fields.size()==6&&fields.contains("schema")&&fields.contains("variant")&&fields.contains("wrist_span_m")&&fields.contains("shoulder_x_m")&&fields.contains("shoulder_y_m")&&fields.contains("shoulder_z_m"),"Missing or unknown avatar profile fields");
    require(fields.at("schema")=="1","Unsupported avatar profile schema");
    require(fields.at("variant")=="male"||fields.at("variant")=="female","Invalid avatar variant");
    AvatarProfile p{fields.at("variant")=="male"?AvatarVariant::male:AvatarVariant::female,number(fields.at("wrist_span_m")),
        {number(fields.at("shoulder_x_m")),number(fields.at("shoulder_y_m")),number(fields.at("shoulder_z_m"))}};
    validate_avatar_profile(p);return p;
}
std::string format_avatar_profile(const AvatarProfile& p){
    validate_avatar_profile(p);std::ostringstream out;out.imbue(std::locale::classic());out.precision(17);
    out<<"schema=1\nvariant="<<(p.variant==AvatarVariant::male?"male":"female")<<"\nwrist_span_m="<<p.wrist_span
       <<"\nshoulder_x_m="<<p.shoulder_offset[0]<<"\nshoulder_y_m="<<p.shoulder_offset[1]<<"\nshoulder_z_m="<<p.shoulder_offset[2]<<'\n';return out.str();
}
std::optional<AvatarProfile> load_avatar_profile(const std::filesystem::path& path){
    if(!std::filesystem::exists(path))return {};
    require(std::filesystem::file_size(path)<=4096,"Avatar profile too large");std::ifstream input(path,std::ios::binary);require(static_cast<bool>(input),"Cannot open avatar profile");
    const std::string text{std::istreambuf_iterator<char>(input),{}};require(!input.bad(),"Cannot read avatar profile");return parse_avatar_profile(text);
}
void save_avatar_profile(const std::filesystem::path& path,const AvatarProfile& p,const std::filesystem::path& staging){
    const auto text=format_avatar_profile(p);require(path.is_absolute()&&staging.is_absolute(),"Profile paths must be absolute");
    std::filesystem::create_directories(path.parent_path());std::filesystem::create_directories(staging);
    static std::atomic<unsigned long long> serial{};
    const auto temp=staging/("avatar-profile-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial)+".tmp");
    require(!std::filesystem::exists(temp),"Profile staging collision");
    try {
        {std::ofstream output(temp,std::ios::binary);output<<text;output.flush();require(static_cast<bool>(output),"Cannot write avatar profile");output.close();require(!output.fail(),"Cannot close avatar profile");}
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace avatar profile");
#else
        std::filesystem::rename(temp,path);
#endif
    }catch(...){std::error_code ignored;std::filesystem::remove(temp,ignored);throw;}
}
std::optional<AvatarBodyCalibration> bind_avatar_profile(const AvatarProfile& p,AvatarVariant variant,std::uint64_t actor,std::uint64_t revision,std::uint64_t generation){
    validate_avatar_profile(p);if(!actor||!generation||variant!=p.variant)return {};
    return AvatarBodyCalibration{actor,revision,generation,variant,p.wrist_span,p.shoulder_offset};
}
}
