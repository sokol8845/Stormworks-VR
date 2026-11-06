#include "swvr/native_camera.hpp"
#include "swvr/camera_bridge.hpp"
#include "swvr/game_profile.hpp"
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace swvr {
void apply_game_eye_projection(std::span<unsigned char> camera,Fov fov) {
    using namespace game_profile;
    if(camera.size()!=camera_size) throw std::invalid_argument("Unexpected native camera size");
    Matrix4 view{},old_projection{};
    Matrix4d double_view{},double_vp{};
    std::memcpy(view.data(),camera.data()+view_offset,sizeof(view));
    std::memcpy(old_projection.data(),camera.data()+projection_offset,sizeof(old_projection));
    std::memcpy(double_view.data(),camera.data()+double_view_offset,sizeof(double_view));
    const auto lens=make_eye_camera(view,old_projection,{},fov);
    for(double value:double_view) if(!std::isfinite(value))
        throw std::invalid_argument("Non-finite native double view");
    // Do not promote the translated float VP: its world position may already
    // have lost centimetres. Multiply the original double view directly.
    for(int col=0;col<4;++col) for(int row=0;row<4;++row)
        for(int k=0;k<4;++k)
            double_vp[col*4+row]+=static_cast<double>(lens.projection[k*4+row])*double_view[col*4+k];
    for(double value:double_vp) if(!std::isfinite(value))
        throw std::invalid_argument("Native double projection overflow");
    // Validation finishes before any bytes are changed.
    std::memcpy(camera.data()+projection_offset,lens.projection.data(),sizeof(lens.projection));
    std::memcpy(camera.data()+view_projection_offset,lens.view_projection.data(),sizeof(lens.view_projection));
    std::memcpy(camera.data()+double_view_projection_offset,double_vp.data(),sizeof(double_vp));
}
}
