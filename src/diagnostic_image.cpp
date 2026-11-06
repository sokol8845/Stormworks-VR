#include "swvr/diagnostic_image.hpp"
#include <stdexcept>

namespace swvr {
RgbImage downscale_rgb(std::span<const unsigned char> rgb,int width,int height,int factor) {
    if(width<=0||height<=0||factor<=0||rgb.size()!=static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*3)
        throw std::invalid_argument("Invalid RGB image or factor");
    RgbImage out{width/factor,height/factor,{}};
    if(out.width<=0||out.height<=0) throw std::invalid_argument("Downscale factor exceeds image size");
    out.pixels.resize(static_cast<std::size_t>(out.width)*static_cast<std::size_t>(out.height)*3);
    const unsigned area=static_cast<unsigned>(factor*factor);
    for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x)for(int c=0;c<3;++c) {
        unsigned sum{};
        for(int dy=0;dy<factor;++dy)for(int dx=0;dx<factor;++dx)
            sum+=rgb[(static_cast<std::size_t>(y*factor+dy)*width+static_cast<std::size_t>(x*factor+dx))*3+c];
        out.pixels[(static_cast<std::size_t>(y)*out.width+x)*3+c]=static_cast<unsigned char>(sum/area);
    }
    return out;
}
}
