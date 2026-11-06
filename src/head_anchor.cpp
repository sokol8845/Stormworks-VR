#include "swvr/head_anchor.hpp"
#include <cmath>
#include <stdexcept>

namespace swvr {
namespace {
void validate_rigid(const Matrix4d& matrix) {
    for(const auto value:matrix) if(!std::isfinite(value))
        throw std::invalid_argument("Non-finite head anchor transform");
    if(std::abs(matrix[3])>1e-10 || std::abs(matrix[7])>1e-10 ||
       std::abs(matrix[11])>1e-10 || std::abs(matrix[15]-1)>1e-10)
        throw std::invalid_argument("Head anchor requires affine transforms");
    for(int a=0;a<3;++a) for(int b=0;b<3;++b) {
        double dot{};
        for(int row=0;row<3;++row) dot+=matrix[a*4+row]*matrix[b*4+row];
        if(std::abs(dot-(a==b?1:0))>1e-5)
            throw std::invalid_argument("Head anchor requires rigid transforms");
    }
    const auto determinant=matrix[0]*(matrix[5]*matrix[10]-matrix[9]*matrix[6])-
        matrix[4]*(matrix[1]*matrix[10]-matrix[9]*matrix[2])+
        matrix[8]*(matrix[1]*matrix[6]-matrix[5]*matrix[2]);
    if(determinant<=0) throw std::invalid_argument("Head anchor requires proper native transforms before reflection");
}
}
Matrix4d make_head_anchor(const Matrix4d& final_camera,const Matrix4d& look,
                         const HeadAnchorSettings& settings) {
    validate_rigid(final_camera);validate_rigid(look);
    constexpr std::uint8_t known_modes=head_mode_1a0|head_mode_1a4|head_mode_1a5|head_mode_1a6;
    if((settings.mode_bits&~known_modes)!=0)
        throw std::invalid_argument("Unknown native head anchor mode bits");
    if(!std::isfinite(settings.backoff_distance))
        throw std::invalid_argument("Non-finite native camera backoff");
    for(const auto value:settings.world_shift) if(!std::isfinite(value))
        throw std::invalid_argument("Non-finite native camera world shift");
    Matrix4d result{};result[15]=1;
    // R(final) = R(body) * R(look). Only rotate the basis back; a full inverse
    // would also remove look.position, which may be part of the eye point.
    for(int col=0;col<3;++col) for(int row=0;row<3;++row)
        for(int k=0;k<3;++k)
            result[col*4+row]+=final_camera[k*4+row]*look[k*4+col];
    for(int axis=0;axis<3;++axis) result[12+axis]=final_camera[12+axis];
    // Reproduce 719F20's branch precedence. Backoff was applied along FINAL
    // camera forward, not along the body-only forward reconstructed above.
    if(settings.mode_bits&head_mode_1a6) {
        for(int axis=0;axis<3;++axis) result[12+axis]+=40.0*final_camera[8+axis];
    } else if(settings.mode_bits&(head_mode_1a4|head_mode_1a5)) {
        for(int axis=0;axis<3;++axis) result[12+axis]-=settings.world_shift[axis];
    } else if((settings.mode_bits&head_mode_1a0)||settings.backoff_distance>0) {
        // Negative d is significant when flag 1A0 is set; native code does not
        // clamp it. When the flag is clear, d<=0 applies no translation.
        for(int axis=0;axis<3;++axis)
            result[12+axis]+=static_cast<double>(settings.backoff_distance)*final_camera[8+axis];
    }
    validate_rigid(result); // Includes overflow checks after undoing translation.
    return result;
}
}
