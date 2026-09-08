#include "antfarm/checkpoint.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
using namespace antfarm;
namespace {
void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
struct Fixture {
    std::string directory;
    CheckpointStore store;
    Fixture():directory((std::filesystem::temp_directory_path()/
      ("antfarm-checkpoint-test-"+std::to_string(reinterpret_cast<uintptr_t>(this)))).string()),store(directory+"/farm") {
        check(std::filesystem::create_directory(directory),"Cannot create private test directory");
    }
    ~Fixture(){std::error_code error;std::filesystem::remove_all(directory,error);}
};
std::vector<char> read(const std::string& path){
    std::ifstream in(path,std::ios::binary);
    check(bool(in),"Cannot read fixture");
    return std::vector<char>(std::istreambuf_iterator<char>(in),{});
}
void write(const std::string& path,const std::vector<char>& bytes){
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.write(bytes.data(),std::streamsize(bytes.size()));check(bool(out),"Cannot write fixture");
}
void test_running_recovery(){
    Fixture f;World source;std::string error;
    source.advance(113);
    check(f.store.write(source,1000,&error),error);
    source.advance(727);
    check(f.store.write(source,1727,&error),error);
    World recovered;CheckpointRecovery info;
    check(f.store.restore(recovered,5727,false,&info,&error),error);
    source.advance(4000);
    check(source.state_hash()==recovered.state_hash(),"RTC catch-up differs from live simulation");
    check(info.sequence==2&&info.slot==1&&info.saved_wall_seconds==1727&&info.simulated_outage_seconds==4000&&!info.was_paused,
          "Newest snapshot and RTC metadata were not kept together");
    check(f.store.write(recovered,5727,&error),error);
    check(f.store.restore(source,5727,false,&info,&error),error);
    check(info.sequence==3&&info.slot==0,"Checkpoint slots did not alternate");
}
void test_interrupted_and_corrupt_recovery(){
    Fixture f;World source;std::string error;
    source.advance(101);World first=source;
    check(f.store.write(source,1000,&error),error);
    source.advance(401);
    check(f.store.write(source,1401,&error),error);
    const auto previous=read(f.store.slot_path(0));
    const auto newest=read(f.store.slot_path(1));
    // Orphaned staging files are never treated as committed checkpoints.
    write(f.store.slot_path(0)+".scratch.abandoned",newest);
    auto damaged=newest;damaged.resize(damaged.size()/2);write(f.store.slot_path(1),damaged);
    World recovered;CheckpointRecovery info;
    check(f.store.restore(recovered,1500,false,&info,&error),error);
    first.advance(500);
    check(recovered.state_hash()==first.state_hash()&&info.sequence==1,"Interrupted newest slot did not recover previous slot");
    check(read(f.store.slot_path(0))==previous,"Recovery changed the fallback snapshot");
    damaged=newest;damaged[20]^=1;write(f.store.slot_path(1),damaged);
    check(f.store.restore(recovered,1500,false,&info,&error),error);
    check(info.sequence==1,"RTC metadata corruption escaped envelope checksum");
    damaged=newest;damaged[12]^=64;write(f.store.slot_path(1),damaged);
    check(f.store.restore(recovered,1500,false,&info,&error),error);
    check(info.sequence==1,"Sequence corruption escaped envelope checksum");
    check(f.store.write(recovered,1500,&error),error);
    check(read(f.store.slot_path(0))==previous,"Writing replacement destroyed previous valid slot");
    check(f.store.restore(recovered,1500,false,&info,&error),error);
    check(info.sequence==2&&info.slot==1,"Did not replace corrupt slot");
}
void test_clock_rejection_and_write_failure(){
    Fixture f;World source;std::string error;
    source.advance(99);check(f.store.write(source,1000,&error),error);
    const auto saved=read(f.store.slot_path(0));const auto hash=source.state_hash();
    CheckpointRecovery info;info.sequence=999;
    check(!f.store.restore(source,999,false,&info,&error),"Backward RTC restore succeeded");
    check(source.state_hash()==hash&&info.sequence==999,"Backward RTC mutated world or output metadata");
    check(!f.store.write(source,999,&error),"Backward RTC write succeeded");
    check(read(f.store.slot_path(0))==saved,"Backward RTC replaced valid snapshot");
    // A directory at the target slot makes atomic replacement fail reliably,
    // without depending on the host user's filesystem permissions.
    std::filesystem::create_directory(f.store.slot_path(1));
    check(!f.store.write(source,1001,&error),"Simulated failed replacement succeeded");
    check(read(f.store.slot_path(0))==saved,"Failed replacement damaged previous slot");
    check(f.store.restore(source,1000,false,&info,&error),error);
    check(source.state_hash()==hash,"Failed replacement prevented fallback restore");
    for(const auto& item:std::filesystem::directory_iterator(f.directory))
        check(item.path().filename().string().find("scratch")==std::string::npos,"Controller leaked a temporary file");
}
void test_paused_and_extinct_recovery(){
    Fixture f;World source;std::string error;
    source.advance(73);source.power_off();const auto paused_hash=source.state_hash();
    check(f.store.write(source,1000,&error),error);
    World restored;CheckpointRecovery info;
    check(f.store.restore(restored,1000+10*Day,false,&info,&error),error);
    check(restored.state_hash()==paused_hash&&info.was_paused&&info.simulated_outage_seconds==0,
          "Deliberate OFF time changed paused world");
    source.power_on();
    check(f.store.restore(restored,1000+10*Day,true,&info,&error),error);
    check(restored.state_hash()==source.state_hash(),"Explicit resume included deliberately paused time");
    for(auto& ant:source.ants)ant.alive=false;
    source.brood.clear();source.extinct=true;source.rebuild_transient();source.power_off();
    check(f.store.write(source,1000+10*Day,&error),error);
    World expected=source;expected.power_on();
    check(f.store.restore(restored,1000+20*Day,true,&info,&error),error);
    check(restored.state_hash()==expected.state_hash(),"Paused extinction resume did not execute exact OFF/ON reset");
    check(!restored.extinct&&restored.seconds==73&&restored.worker_count()==29,"Replacement colony or preserved day incorrect");
}
}
int main(){
    const std::vector<std::pair<const char*,std::function<void()>>> tests={
      {"newest slot with exact running RTC catch-up",test_running_recovery},
      {"interrupted slot and checksummed RTC/sequence fallback",test_interrupted_and_corrupt_recovery},
      {"backward RTC and failed write are transactional",test_clock_rejection_and_write_failure},
      {"paused restore and explicit extinction restart",test_paused_and_extinct_recovery}};
    unsigned failed=0;
    for(const auto& test:tests)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}
      catch(const std::exception& ex){++failed;std::cerr<<"FAIL "<<test.first<<": "<<ex.what()<<'\n';}
    return failed?1:0;
}
