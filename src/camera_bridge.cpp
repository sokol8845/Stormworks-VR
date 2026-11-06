#include "swvr/camera_bridge.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace swvr {
namespace {
Matrix4 multiply(const Matrix4& a,const Matrix4& b) {
    Matrix4 result{};
    for(int col=0;col<4;++col) for(int row=0;row<4;++row) {
        double sum{};
        for(int k=0;k<4;++k) sum+=static_cast<double>(a[k*4+row])*b[col*4+k];
        result[col*4+row]=static_cast<float>(sum);
    }
    return result;
}
Matrix4 invert(const Matrix4& input) {
    double rows[4][8]{};
    for(int row=0;row<4;++row) {
        for(int col=0;col<4;++col) rows[row][col]=input[col*4+row];
        rows[row][row+4]=1;
    }
    for(int col=0;col<4;++col) {
        int pivot=col;
        for(int row=col+1;row<4;++row)
            if(std::abs(rows[row][col])>std::abs(rows[pivot][col])) pivot=row;
        if(std::abs(rows[pivot][col])<1e-12) throw std::invalid_argument("Singular camera matrix");
        for(int k=0;k<8;++k) std::swap(rows[col][k],rows[pivot][k]);
        const auto scale=rows[col][col];
        for(auto& value:rows[col]) value/=scale;
        for(int row=0;row<4;++row) if(row!=col) {
            const auto factor=rows[row][col];
            for(int k=0;k<8;++k) rows[row][k]-=factor*rows[col][k];
        }
    }
    Matrix4 result{};
    for(int col=0;col<4;++col) for(int row=0;row<4;++row)
        result[col*4+row]=static_cast<float>(rows[row][col+4]);
    return result;
}
bool near(float a,float b) {return std::abs(a-b)<1e-4f;}
void validate(const Matrix4& view,const Matrix4& projection) {
    for(const auto& m:{view,projection}) for(float value:m)
        if(!std::isfinite(value)) throw std::invalid_argument("Non-finite game camera");
    if(!near(view[3],0)||!near(view[7],0)||!near(view[11],0)||!near(view[15],1))
        throw std::invalid_argument("Expected affine game view");
    // An orthonormal basis may have determinant -1: Stormworks' observed
    // camera does. Requiring a proper rotation would erase its handedness.
    for(int a=0;a<3;++a) for(int b=0;b<3;++b) {
        float dot{};
        for(int row=0;row<3;++row) dot+=view[a*4+row]*view[b*4+row];
        if(!near(dot,a==b?1.f:0.f)) throw std::invalid_argument("Non-rigid game view");
    }
    for(int slot:{1,2,3,4,6,7,12,13,15})
        if(!near(projection[slot],0)) throw std::invalid_argument("Unsupported game projection");
    if(!near(projection[11],-1)||projection[0]<=0||projection[5]<=0||
       projection[10]>-1||projection[14]>=0)
        throw std::invalid_argument("Expected conventional OpenGL perspective depth");
}
}
EyeCamera make_eye_camera(const Matrix4& game_view,const Matrix4& game_projection,
                          const Pose& eye_from_camera,Fov eye_fov) {
    validate(game_view,game_projection);
    EyeCamera result{};
    result.view=multiply(view_matrix(eye_from_camera),game_view);
    result.projection=game_projection;
    const auto lens=projection(eye_fov,1,2); // Only angular terms are copied.
    for(int slot:{0,5,8,9}) result.projection[slot]=lens[slot];
    result.view_projection=multiply(result.projection,result.view);
    // Compose inverses instead of inverting a translated VP with poor conditioning.
    const auto inverse_view=invert(result.view);
    result.inverse_view_projection=multiply(inverse_view,invert(result.projection));
    result.render_position={inverse_view[12],inverse_view[13],inverse_view[14]};
    for(const auto& m:{result.view,result.projection,result.view_projection,result.inverse_view_projection})
        for(float value:m) if(!std::isfinite(value)) throw std::invalid_argument("Camera result overflow");
    return result;
}
Matrix4d offset_game_camera(const Matrix4d& transform,const Pose& eye_from_camera) {
    for(double value:transform) if(!std::isfinite(value))
        throw std::invalid_argument("Non-finite camera transform");
    if(std::abs(transform[3])>1e-8 || std::abs(transform[7])>1e-8 ||
       std::abs(transform[11])>1e-8 || std::abs(transform[15]-1)>1e-8)
        throw std::invalid_argument("Expected affine camera transform");
    for(int a=0;a<3;++a) for(int b=0;b<3;++b) {
        double dot{};
        for(int row=0;row<3;++row) dot+=transform[a*4+row]*transform[b*4+row];
        if(std::abs(dot-(a==b?1:0))>1e-4) throw std::invalid_argument("Non-rigid camera transform");
    }
    const auto eye=view_matrix(inverse(eye_from_camera));
    // Measured V*T=S, where S=diag(1,1,-1,1). Thus V'=D^-1*V
    // requires T'=T*S*D*S. Keep T in double: casting world positions
    // to float before adding IPD loses centimetres far from the origin.
    constexpr double sign[]{1,1,-1,1};
    Matrix4d result{};
    for(int col=0;col<4;++col) for(int row=0;row<4;++row)
        for(int k=0;k<4;++k)
            result[col*4+row]+=transform[k*4+row]*sign[k]*eye[col*4+k]*sign[col];
    for(double value:result) if(!std::isfinite(value))
        throw std::invalid_argument("Camera transform overflow");
    return result;
}
}
