#include "swvr/control_profiles.hpp"
#include "swvr/control_diagnostics.hpp"
#include "swvr/flight_recorder.hpp"
#include "steering_fixture.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <sstream>
#include <stdexcept>
using namespace swvr;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F fn){bool caught{};try{fn();}catch(const std::exception&){caught=true;}check(caught,"invalid operation accepted");}
std::string text(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
template<class F>void wait_for(F fn){const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!fn()){check(std::chrono::steady_clock::now()<until,"worker timeout");std::this_thread::yield();}}
}
int main(int argc,char** argv){try{
    check(argc==2,"temporary root required");const auto root=std::filesystem::absolute(argv[1])/("offline-tools-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);
    GestureSettings a,b;b.input.movement_fists=false;b.joystick.forward_only=true;b.steering={150,3,80};
    check(parse_gesture_settings(format_gesture_settings_ini(b))==b,"new preferences roundtrip lost");
    for(const char* config:{"[steering]\nfull_lock_degrees=29","[steering]\nfull_lock_degrees=181","[steering]\ndead_zone_degrees=nan","[steering]\ndead_zone_degrees=16","[steering]\nsmoothing_ms=-1","[steering]\nsmoothing_ms=201"})rejects([&]{parse_gesture_settings(config);});
    ControlProfiles profiles(root/"profiles");rejects([&]{profiles.load(ControlProfile::walking);});
    profiles.save(ControlProfile::walking,a);check(profiles.load(ControlProfile::walking)==a&&!profiles.exists(ControlProfile::walking,true),"first profile save");
    profiles.save(ControlProfile::walking,b);check(profiles.load(ControlProfile::walking)==b&&profiles.load(ControlProfile::walking,true)==a,"profile backup/roundtrip");
    profiles.save(ControlProfile::driving,a);check(profiles.load(ControlProfile::walking)==b,"profile slots overlap");
    rejects([&]{profiles.save(static_cast<ControlProfile>(3),a);});
    {std::ofstream bad(root/"profiles"/"walking.ini");bad<<"[steering]\nsmoothing_ms=oops\n";}
    rejects([&]{profiles.save(ControlProfile::walking,a);});check(profiles.load(ControlProfile::walking,true)==a,"invalid profile destroyed good backup");
    auto f=steering_fixture::hands(0);f.id=1;ControlDiagnostics d;d.camera_current=true;d.movement.forward=.5f;d.turn_degrees_per_second=45;d.keypad=true;
    auto lines=control_diagnostic_lines(f,b,d);check(lines[2].find("0.50")!=std::string::npos&&lines[5].find("KLAWIATURA VR")!=std::string::npos,"diagnostic axes/keypad reason");
    f.system_input_suppressed=true;check(control_diagnostic_lines(f,b,d)[5].find("GEST SYSTEMOWY")!=std::string::npos,"system guard misreported as tracking");f.system_input_suppressed=false;
    d.keypad=false;d.pointing.valid_ray=true;d.denied=7;d.last_denial="target_mismatch";lines=control_diagnostic_lines(f,b,d);check(lines[7].find("TARGET/MISMATCH")!=std::string::npos&&lines[5].find("NIEPOTWIERDZONY")!=std::string::npos,"ray incorrectly certified target");
    FlightRecorder recorder;const auto directory=root/"flight";recorder.open(directory,{3,2,16*1024,100});
    // Mark the first window, then rotate past it. Frozen files must not change.
    for(unsigned i=0;i<3;++i){f.id=i+1;recorder.sample(f,b,i*100,lines);}
    wait_for([&]{return recorder.stats().samples==3||recorder.stats().failed;});check(!recorder.stats().failed&&recorder.mark(250,3),"marker unavailable");
    wait_for([&]{return recorder.stats().saved==1||recorder.stats().failed;});check(!recorder.stats().failed,"marker failed");
    const auto first=text(directory/"incident-1"/"segment-1.tsv");check(first.ends_with("END\n"),"active incident not finalized");
    for(unsigned i=3;i<10;++i){f.id=i+1;recorder.sample(f,i%2?a:b,i*100,lines);}
    wait_for([&]{return recorder.stats().samples==10||recorder.stats().failed;});check(recorder.mark(950,10),"second marker unavailable");
    wait_for([&]{return recorder.stats().saved==2||recorder.stats().failed;});
    check(first==text(directory/"incident-1"/"segment-1.tsv"),"rotation modified frozen incident");
    const auto second_manifest=text(directory/"incident-2"/"manifest.txt");
    check(recorder.mark(970,10),"reused marker unavailable");recorder.close();
    check(recorder.stats().saved==3&&text(directory/"incident-1"/"manifest.txt").find("marker=3\n")!=std::string::npos,"close did not drain marker or slot was not reused");
    check(!std::filesystem::exists(directory/"incident-1"/"segment-1.tsv")&&text(directory/"incident-2"/"manifest.txt")==second_manifest,"slot reuse retained old files or changed another incident");
    check(recorder.stats().samples==10&&recorder.stats().rotations==7&&!recorder.stats().failed&&!recorder.stats().dropped,"rolling stopped/dropped data");
    unsigned segments{};for(const auto& entry:std::filesystem::directory_iterator(directory))if(entry.path().extension()==".tsv"){
        ++segments;const auto raw=text(entry.path());check(raw.starts_with("SWVR_FLIGHT 1\nC ")&&raw.ends_with("END\n")&&raw.size()<=16*1024,"segment not independent/bounded/complete");
        std::istringstream in(raw);std::string header;std::getline(in,header);auto config=read_hand_record(in);check(config.kind==HandRecordKind::configuration,"missing configuration");auto sample=read_hand_record(in);check(sample.kind==HandRecordKind::sample&&sample.sample.frame.id>=8,"latest frames lost");
    }check(segments==3,"unbounded retained segments");
    rejects([&]{FlightRecorder collision;collision.open(directory);});
    // Size rotation, rejected oversized diagnostics and asynchronous write errors.
    FlightRecorder bytes;bytes.open(root/"bytes",{2,1,8192,60'000});for(unsigned i=0;i<12;++i){f.id=100+i;bytes.sample(f,b,i,lines);}bytes.sample(f,b,13,{std::string(129,'x')});bytes.close();check(!bytes.stats().failed&&bytes.stats().rotations>0&&bytes.stats().dropped==1,"byte cap/diagnostic bounds");
    FlightRecorder failure;failure.open(root/"failure");auto bad=b;bad.steering.smoothing_ms=-1;failure.sample(f,bad,0,lines);failure.close();check(failure.stats().failed&&!failure.mark(1,1),"worker error presented as success");
    // Keep these tiny fixtures for the cross-language reader smoke test.
    std::cout<<"PASS profiles, diagnostic gates, async rolling/markers; fixtures: "<<root.string()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
