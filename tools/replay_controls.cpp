#include "swvr/control_recording.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3)throw std::runtime_error("Usage: swvr_replay_controls INPUT.controls.tsv OUTPUT.tsv");
    const auto source=std::filesystem::absolute(argv[1]),destination=std::filesystem::absolute(argv[2]);
    if(std::filesystem::exists(destination)||std::filesystem::weakly_canonical(source)==std::filesystem::weakly_canonical(destination))throw std::runtime_error("Output exists or aliases input");
    std::ifstream input(source,std::ios::binary);if(!input)throw std::runtime_error("Cannot read control recording");
    std::ofstream output(destination,std::ios::binary);if(!output)throw std::runtime_error("Cannot write output");
    const auto stats=swvr::replay_controls(input,output);output.flush();if(!output)throw std::runtime_error("Output write failed");
    std::cout<<"turn_checks="<<stats.turn_checks<<" panel_checks="<<stats.panel_checks<<" mismatches="<<stats.mismatches<<" limited="<<stats.limited<<'\n';return stats.mismatches?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
