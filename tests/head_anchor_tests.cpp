#include "swvr/head_anchor.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using swvr::Matrix4d;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F&& action,const char* message) {
    bool caught{};try {action();}catch(const std::invalid_argument&){caught=true;}require(caught,message);
}
Matrix4d identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
Matrix4d multiply(const Matrix4d& a,const Matrix4d& b) {
    Matrix4d out{};
    for(int col=0;col<4;++col)for(int row=0;row<4;++row)
        for(int k=0;k<4;++k)out[col*4+row]+=a[k*4+row]*b[col*4+k];
    return out;
}
Matrix4d rotation(char axis,double angle) {
    auto m=identity();const auto c=std::cos(angle),s=std::sin(angle);
    if(axis=='x'){m[5]=m[10]=c;m[6]=s;m[9]=-s;}
    if(axis=='y'){m[0]=m[10]=c;m[2]=-s;m[8]=s;}
    if(axis=='z'){m[0]=m[5]=c;m[1]=s;m[4]=-s;}
    return m;
}
Matrix4d inverse(const Matrix4d& value) {
    auto result=identity();
    for(int col=0;col<3;++col)for(int row=0;row<3;++row)result[col*4+row]=value[row*4+col];
    for(int row=0;row<3;++row)for(int k=0;k<3;++k)result[12+row]-=result[k*4+row]*value[12+k];
    return result;
}
double position_distance(const Matrix4d& a,const Matrix4d& b) {
    return std::hypot(a[12]-b[12],a[13]-b[13],a[14]-b[14]);
}
void same_rotation(const Matrix4d& a,const Matrix4d& b,double tolerance=1e-12) {
    for(int col=0;col<3;++col)for(int row=0;row<3;++row)
        require(std::abs(a[col*4+row]-b[col*4+row])<tolerance,"Body rotation was not recovered on the correct side");
}
// Independent forward native rule: use homogeneous matrix composition for
// local backoff, or a world translation on the LEFT of the composed camera.
Matrix4d native_final(const Matrix4d& pre,const swvr::HeadAnchorSettings& s) {
    auto displacement=identity();
    if(s.mode_bits&swvr::head_mode_1a6)displacement[14]=-40;
    else if(s.mode_bits&(swvr::head_mode_1a4|swvr::head_mode_1a5)) {
        for(int i=0;i<3;++i)displacement[12+i]=s.world_shift[i];
        return multiply(displacement,pre);
    } else if((s.mode_bits&swvr::head_mode_1a0)||s.backoff_distance>0)
        displacement[14]=-static_cast<double>(s.backoff_distance);
    return multiply(pre,displacement);
}
}
int main() {
    try {
        auto body=multiply(multiply(rotation('y',.7),rotation('x',-.3)),rotation('z',.22));
        body[12]=1e9+.125;body[13]=1e9+.25;body[14]=-1e9+.5;
        auto look=multiply(multiply(rotation('y',-.4),rotation('x',.6)),rotation('z',-.12));
        look[12]=.02;look[13]=.12;look[14]=-.03;
        const auto pre=multiply(body,look);
        for(unsigned flags=0;flags<16;++flags)for(float distance:{-2.f,0.f,.25f}) {
            const swvr::HeadAnchorSettings settings{static_cast<std::uint8_t>(flags),distance,{.125,-.75,1.5}};
            const auto final=native_final(pre,settings);
            const auto before=final;
            const auto anchor=swvr::make_head_anchor(final,look,settings);
            same_rotation(anchor,body);
            require(position_distance(anchor,pre)<3e-7,"Native translation branch or double world precision mismatch");
            require(final==before,"Head anchor modified source camera");
            require(std::abs(anchor[1])>.1,"Real body/vehicle bank was flattened");
        }
        const auto anchor=swvr::make_head_anchor(pre,look,{});
        const auto wrong_full_inverse=multiply(pre,inverse(look));
        require(position_distance(anchor,wrong_full_inverse)>.12 && position_distance(anchor,wrong_full_inverse)<.13,
                "Regression must expose loss of local look translation from a full inverse");
        const auto wrong_side=multiply(inverse(look),pre);
        double wrong_rotation_error{};
        for(int col=0;col<3;++col)for(int row=0;row<3;++row)
            wrong_rotation_error=std::max(wrong_rotation_error,std::abs(wrong_side[col*4+row]-body[col*4+row]));
        require(wrong_rotation_error>.1,"Fixture must distinguish noncommuting multiplication order");
        {
            // Captured final camera: headset-xr-20261028-230241-819, frame35476.
            // The look, body decomposition and .4m backoff below are SYNTHETIC
            // math factors, NOT measured game body/mode data. See JSON fixture.
            const Matrix4d captured{
                .97424725757152653,.01739303212262654,-.22481037507691942,0,
                -.12362065167039119,.87502386645315533,-.46802903385304861,0,
                .18857396877951577,.48376722967898406,.85463973926800141,0,
                11418.428426174956,2.6652464730944008,-15216.250979537968,1};
            const Matrix4d synthetic_look{
                .92089566622919439,.06595644308736591,-.38418865097182586,0,
                -.26806511242718761,.82269594921428901,-.50131075257349056,0,
                .28300577276742539,.56464247339503537,.7752977555872641,0,
                .02,.12,-.03,1};
            const Matrix4d expected{
                .98368598297990928,-.08163728455806811,.16030333529660237,0,
                .0690325465781612,.99418129804355437,.082692613267181775,0,
                -.16612140291995522,-.070277431162113477,.98359785360255347,0,
                11418.503855762468,2.8587533649659944,-15215.909123642261,1};
            const auto actual=swvr::make_head_anchor(captured,synthetic_look,{swvr::head_mode_1a0,.4f,{}});
            same_rotation(actual,expected);
            require(position_distance(actual,expected)<2e-8,"Captured matrix / synthetic decomposition regression");
        }
        // Same current camera with a different historical look must recover a
        // different body rotation. The caller cannot read overwritten Lprev.
        const auto other=swvr::make_head_anchor(pre,rotation('y',.2),{});
        require(std::abs(other[0]-anchor[0])>.01,"Look identity must affect reconstructed body basis");
        for(unsigned invalid=0;invalid<12;++invalid) {
            auto m=identity(),l=identity();swvr::HeadAnchorSettings s{};
            if(invalid==0)m[0]=2;
            if(invalid==1)l[4]=.1;
            if(invalid==2)m[3]=.01;
            if(invalid==3)l[15]=0;
            if(invalid==4)m[12]=std::numeric_limits<double>::quiet_NaN();
            if(invalid==5)l[13]=std::numeric_limits<double>::infinity();
            if(invalid==6)m[10]=-1;
            if(invalid==7)l[0]=-1;
            if(invalid==8)s.mode_bits=16;
            if(invalid==9)s.backoff_distance=std::numeric_limits<float>::quiet_NaN();
            if(invalid==10)s.world_shift[1]=std::numeric_limits<double>::infinity();
            if(invalid==11){s.mode_bits=swvr::head_mode_1a4;m[12]=-std::numeric_limits<double>::max();s.world_shift[0]=std::numeric_limits<double>::max();}
            rejects([&]{swvr::make_head_anchor(m,l,s);},"Invalid head anchor input was accepted");
        }
        std::cout<<"Head anchor tests passed: native branch precedence, body/look separation, preserved eye point and large-world precision.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
