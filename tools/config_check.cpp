#include "swvr/gesture_settings.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=2) {std::cerr<<"Usage: swvr_config_check FILE.ini\n";return 2;}
        std::cout<<swvr::describe_gesture_settings(swvr::load_gesture_settings(argv[1]))<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
