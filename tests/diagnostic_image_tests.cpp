#include "swvr/diagnostic_image.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {void require(bool ok,const char* s){if(!ok)throw std::runtime_error(s);}}
int main(){try{
    // 5x3 image, factor 2 -> 2x1; block (0,0) averages four pixels per channel.
    std::vector<unsigned char> rgb(5*3*3);
    for(int y=0;y<3;++y)for(int x=0;x<5;++x)for(int c=0;c<3;++c)rgb[(y*5+x)*3+c]=static_cast<unsigned char>(x*40+y*10+c);
    const auto out=swvr::downscale_rgb(rgb,5,3,2);
    require(out.width==2&&out.height==1&&out.pixels.size()==6,"downscaled size");
    // Block x0..1,y0..1 channel 0: (0+40+10+50)/4=25; block x2..3: (80+120+90+130)/4=105.
    require(out.pixels[0]==25&&out.pixels[1]==26&&out.pixels[3]==105,"box average");
    require(swvr::downscale_rgb(rgb,5,3,1).pixels==rgb,"factor 1 is identity");
    bool caught{};try{(void)swvr::downscale_rgb(rgb,5,4,2);}catch(const std::invalid_argument&){caught=true;}require(caught,"size mismatch accepted");
    caught=false;try{(void)swvr::downscale_rgb(rgb,5,3,0);}catch(const std::invalid_argument&){caught=true;}require(caught,"zero factor accepted");
    caught=false;try{(void)swvr::downscale_rgb(rgb,5,3,4);}catch(const std::invalid_argument&){caught=true;}require(caught,"empty result accepted");
    std::cout<<"PASS: diagnostic RGB downscale\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
