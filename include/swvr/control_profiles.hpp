#pragma once
#include "swvr/gesture_settings.hpp"
namespace swvr {
// Fixed slots avoid free-form paths from UI input. No body/world calibration.
enum class ControlProfile : unsigned { walking,driving,second_user };
std::string_view control_profile_name(ControlProfile);
class ControlProfiles {
public:
    explicit ControlProfiles(std::filesystem::path directory);
    bool exists(ControlProfile,bool backup=false)const;
    GestureSettings load(ControlProfile,bool backup=false)const;
    void save(ControlProfile,const GestureSettings&)const;
private:
    std::filesystem::path path(ControlProfile,bool backup=false)const;
    std::filesystem::path directory_;
};
}
