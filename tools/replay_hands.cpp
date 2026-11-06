#include "swvr/hand_recording.hpp"
#include "swvr/locomotion_replay.hpp"
#include "replay_json.hpp"
#include <fstream>
#include <iostream>
#include <locale>
#include <stdexcept>
int wmain(int argc,wchar_t** argv){try{
    if(argc<3||(argc-3)%2){std::cerr<<"Usage: swvr_replay_hands RECORD.hands.tsv OUTPUT.tsv [--config FILE.ini] [--mode joystick|arm_swing] [--json OUTPUT.json]\n";return 2;}
    std::optional<swvr::GestureSettings> override_settings;std::optional<swvr::LocomotionMode> override_mode;
    std::optional<std::filesystem::path> json_path;
    for(int i=3;i<argc;i+=2){const std::wstring_view option=argv[i],value=argv[i+1];
        if(option==L"--config"&&!override_settings)override_settings=swvr::load_gesture_settings(argv[i+1]);
        else if(option==L"--mode"&&!override_mode&&(value==L"joystick"||value==L"arm_swing"))override_mode=value==L"joystick"?swvr::LocomotionMode::joystick:swvr::LocomotionMode::arm_swing;
        else if(option==L"--json"&&!json_path)json_path=std::filesystem::absolute(argv[i+1]);
        else throw std::invalid_argument("Unknown, duplicate or invalid replay option");
    }
    const std::filesystem::path source=std::filesystem::absolute(argv[1]),destination=std::filesystem::absolute(argv[2]);
    if(std::filesystem::weakly_canonical(source)==std::filesystem::weakly_canonical(destination)||std::filesystem::exists(destination))throw std::runtime_error("Output already exists or aliases input");
    if(json_path&&(std::filesystem::exists(*json_path)||std::filesystem::weakly_canonical(*json_path)==std::filesystem::weakly_canonical(source)||std::filesystem::weakly_canonical(*json_path)==std::filesystem::weakly_canonical(destination)))throw std::runtime_error("JSON output already exists or aliases another file");
    std::ifstream input(source,std::ios::binary);if(!input)throw std::runtime_error("Cannot open recording");swvr::read_hand_recording_header(input);
    std::ofstream output(destination,std::ios::binary);if(!output)throw std::runtime_error("Cannot open replay output");output.imbue(std::locale::classic());
    output<<"elapsed_ms\txr_frame\tmode\tallowed\tsuspended\tnative_bits\tforward\tsideways\tsprint\treserved\tstatus\tevent\tblocks\texpected\tmismatch\n";
    output<<"#overrides\tsettings="<<override_settings.has_value()<<" mode="<<override_mode.has_value()<<'\n';
    swvr::LocomotionReplay detector(override_settings,override_mode);swvr::ReplayJson json;std::uint64_t frames{},sprints{},mismatches{};
    for(;;){const auto r=swvr::read_hand_record(input);if(r.kind==swvr::HandRecordKind::end)break;
        const auto o=detector.apply(r);if(!o.emitted)continue;const auto c=o.command;
        if(o.sample){++frames;if(c.sprint)++sprints;}if(o.mismatch)++mismatches;if(json_path)json.append(r,o,detector.mode());
        output<<static_cast<double>(r.journal?r.event.observed_ns:r.sample.elapsed_ns)/1e6<<'\t'<<r.sample.frame.id<<'\t'<<(detector.mode()==swvr::LocomotionMode::arm_swing?"arm_swing":"joystick")<<'\t'<<r.sample.walking_allowed<<'\t'<<r.sample.suspended<<'\t'<<swvr::locomotion_buttons(c)<<'\t'<<c.forward<<'\t'<<c.sideways<<'\t'<<c.sprint<<'\t'<<o.reserved<<'\t'<<o.status<<'\t'<<o.event<<'\t'<<o.blocks<<'\t'<<o.expected<<'\t'<<o.mismatch<<'\n';
    }
    output<<(mismatches&&!override_settings&&!override_mode?"#diverged":"#complete")<<"\tframes="<<frames<<" sprint_frames="<<sprints<<" configurations="<<detector.configurations()<<" mismatches="<<mismatches<<'\n';output.flush();if(!output)throw std::runtime_error("Replay output failed");
    if(json_path)json.write(*json_path,override_settings.has_value()||override_mode.has_value(),mismatches);
    if(mismatches&&!override_settings&&!override_mode)throw std::runtime_error("Recorded native inspections differ from replay; see mismatch columns");
    std::cout<<"Offline replay complete: frames="<<frames<<" sprint_frames="<<sprints<<". Detector output only; no game/network calls.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
