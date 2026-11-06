#include "swvr/camera_bridge.hpp"
#include "swvr/native_camera.hpp"
#include "swvr/game_profile.hpp"
#include <cstring>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace swvr;
void check(bool condition,const char* text) {if(!condition) throw std::runtime_error(text);}
bool near(float a,float b,float tolerance=2e-4f) {return std::abs(a-b)<tolerance;}
float transform(const Matrix4& m,Vec3 point,int row) {
    return m[row]*point.x+m[4+row]*point.y+m[8+row]*point.z+m[12+row];
}
int main() {
    try {
        // Real frame 1085, Steam build 26294447, camera matrices sent without transpose.
        // Its basis has determinant -1; a pose/quaternion conversion cannot represent it.
        const Matrix4 view{1,0,0,0,0,1,0,0,0,0,-1,0,91,-48.2999992f,20,1};
        const Matrix4 projection{1.83048773f,0,0,0,0,1.83048773f,0,0,
                                  0,0,-1.0000025f,-1,0,0,-.0500000641f,0};
        const float angle=std::atan(1/projection[0]);
        const Matrix4d distant{1,0,0,0,0,1,0,0,0,0,1,0,1000000000.125,2,3,1};
        const auto distant_left=offset_game_camera(distant,{{},{-.032f,0,-.2f}});
        check(std::abs((distant_left[12]-distant[12])+.032)<1e-6,"World coordinate rounding erased IPD");
        check(std::abs(distant_left[14]-3.2)<1e-6,"World camera forward basis reversed");
        const Matrix4d rotated{0,0,-1,0,0,1,0,0,1,0,0,0,10,20,30,1};
        const auto local_left=offset_game_camera(rotated,{{},{-.032f,0,0}});
        check(std::abs(local_left[12]-10)<1e-6 && std::abs(local_left[14]-30.032)<1e-6,
              "IPD must follow camera orientation in world space");
        const Fov same_fov{-angle,angle,angle,-angle};
        const auto centered=make_eye_camera(view,projection,{},same_fov);
        const Matrix4 observed_vp{1.83048773f,0,0,0,0,1.83048773f,0,0,0,0,1.0000025f,1,
                                  166.574387f,-88.4125595f,-20.0500488f,-20};
        for(int i=0;i<16;++i) {
            check(near(centered.view[i],view[i]),"Centered camera changed game view");
            check(near(centered.view_projection[i],observed_vp[i]),"Game VP convention mismatch");
        }
        const Fov asymmetric{-.8f,.7f,.9f,-.6f};
        {
            using namespace game_profile;
            std::array<unsigned char,camera_size> bytes{};bytes.fill(0x5a);
            auto precise_view=Matrix4d{};
            for(int i=0;i<16;++i) precise_view[i]=view[i];
            precise_view[12]=1000000000.125;
            auto rounded_view=view;rounded_view[12]=static_cast<float>(precise_view[12]);
            std::memcpy(bytes.data()+view_offset,rounded_view.data(),sizeof(rounded_view));
            std::memcpy(bytes.data()+projection_offset,projection.data(),sizeof(projection));
            std::memcpy(bytes.data()+double_view_offset,precise_view.data(),sizeof(precise_view));
            const auto before=bytes;
            apply_game_eye_projection(bytes,asymmetric);
            Matrix4 lens{},float_vp{};Matrix4d precise_vp{};
            std::memcpy(lens.data(),bytes.data()+projection_offset,sizeof(lens));
            std::memcpy(float_vp.data(),bytes.data()+view_projection_offset,sizeof(float_vp));
            std::memcpy(precise_vp.data(),bytes.data()+double_view_projection_offset,sizeof(precise_vp));
            check(lens[8]!=0 && lens[9]!=0,"Native eye lost asymmetric lens shift");
            check(lens[10]==projection[10] && lens[14]==projection[14],"Native eye changed depth range");
            const auto expected=make_eye_camera(rounded_view,projection,{},asymmetric);
            check(float_vp==expected.view_projection,"Native float VP inconsistent");
            check(std::abs(precise_vp[12]-(static_cast<double>(lens[0])*precise_view[12]+lens[8]*precise_view[14]))<1e-6,
                  "Native double VP discarded large-world precision");
            for(std::size_t i=0;i<bytes.size();++i) {
                const bool modified=(i>=projection_offset && i<view_projection_offset+sizeof(Matrix4)) ||
                                    (i>=double_view_projection_offset && i<double_view_projection_offset+sizeof(Matrix4d));
                if(!modified) check(bytes[i]==before[i],"Lens patch changed unrelated camera bytes");
            }
            const auto valid=bytes;bool rejected{};
            try {apply_game_eye_projection(bytes,{0,0,0,0});} catch(const std::invalid_argument&){rejected=true;}
            check(rejected && bytes==valid,"Invalid lens partially changed native camera");
        }
        const auto left=make_eye_camera(view,projection,{{},{-.032f,0,0}},asymmetric);
        const auto right=make_eye_camera(view,projection,{{},{.032f,0,0}},asymmetric);
        check(near(right.render_position.x-left.render_position.x,.064f),"IPD or handedness reversed");
        check(near(left.render_position.x,-91.032f),"Left eye render coordinates");
        check(near(left.render_position.y,48.3f) && near(left.render_position.z,20),"Game origin lost");
        for(const auto& camera:{left,right}) {
            for(int row=0;row<3;++row)
                check(near(transform(camera.view,camera.render_position,row),0),"View does not center eye");
            check(camera.projection[10]==projection[10] && camera.projection[14]==projection[14],
                  "Game depth coefficients changed");
            for(int row=0;row<4;++row) for(int col=0;col<4;++col) {
                double value{};
                for(int k=0;k<4;++k) value+=static_cast<double>(camera.view_projection[k*4+row])*
                    camera.inverse_view_projection[col*4+k];
                check(std::abs(value-(row==col?1:0))<.002,"Depth reconstruction inverse inconsistent");
            }
        }
        // OpenXR forward is -Z; this game's captured basis maps it to world +Z.
        const float s=std::sqrt(.5f);
        const auto moved=make_eye_camera(view,projection,{{0,s,0,s},{0,.1f,-.2f}},asymmetric);
        check(near(moved.render_position.y,48.4f)&&near(moved.render_position.z,20.2f),"Head motion basis mismatch");
        for(int row=0;row<3;++row)
            check(near(transform(moved.view,moved.render_position,row),0),"Rotated eye not centered");
        const Vec3 yaw_forward{moved.render_position.x-1,moved.render_position.y,moved.render_position.z};
        check(near(transform(moved.view,yaw_forward,0),0) &&
              near(transform(moved.view,yaw_forward,2),-1),"Head yaw does not turn view in reflected basis");
        for(int invalid=0;invalid<3;++invalid) {
            auto bad=view;auto bad_projection=projection;
            if(invalid==0) bad[0]=0;
            if(invalid==1) bad[12]=std::numeric_limits<float>::quiet_NaN();
            if(invalid==2) bad_projection[11]=0;
            bool rejected=false;
            try {make_eye_camera(bad,bad_projection,{},asymmetric);} catch(const std::invalid_argument&) {rejected=true;}
            check(rejected,"Invalid camera accepted");
        }
        std::cout<<"PASS camera bridge: captured matrices, reflected basis, IPD, head pose and depth reconstruction\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
