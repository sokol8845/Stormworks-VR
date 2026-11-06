#include "swvr/control_profiles.hpp"
#include <array>
#include <stdexcept>
namespace swvr {
namespace {
unsigned index(ControlProfile slot){const auto i=static_cast<unsigned>(slot);if(i>=3)throw std::invalid_argument("Invalid control profile");return i;}
}
std::string_view control_profile_name(ControlProfile slot){return std::array<std::string_view,3>{"CHODZENIE","JAZDA","UZYTKOWNIK 2"}[index(slot)];}
ControlProfiles::ControlProfiles(std::filesystem::path directory):directory_(std::move(directory)){
    if(!directory_.is_absolute())throw std::invalid_argument("Profile directory must be absolute");
}
std::filesystem::path ControlProfiles::path(ControlProfile slot,bool backup)const{
    const auto name=std::array<const char*,3>{"walking.ini","driving.ini","second-user.ini"}[index(slot)];
    auto result=directory_/name;if(backup)result+=".bak";return result;
}
bool ControlProfiles::exists(ControlProfile slot,bool backup)const{return std::filesystem::is_regular_file(path(slot,backup));}
GestureSettings ControlProfiles::load(ControlProfile slot,bool backup)const{return load_gesture_settings(path(slot,backup));}
void ControlProfiles::save(ControlProfile slot,const GestureSettings& settings)const{
    validate_gesture_settings(settings);
    const auto target=path(slot);std::filesystem::create_directories(directory_);
    // A damaged old profile is never silently replaced or copied over a good backup.
    if(std::filesystem::exists(target))save_gesture_settings(path(slot,true),load_gesture_settings(target));
    save_gesture_settings(target,settings);
}
}
