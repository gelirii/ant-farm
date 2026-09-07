#include "antfarm/core.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace antfarm;
namespace {
void require(bool condition,const std::string& why) {
    if(!condition) throw std::runtime_error(why);
}
void valid(const World& w) {
    std::string error;
    require(w.validate(&error),"World invariant failure: "+error);
}
struct TemporaryFile {
    std::string path;
    explicit TemporaryFile(const std::string& suffix) {
        path="/tmp/antfarm-test-"+std::to_string(reinterpret_cast<uintptr_t>(this))+suffix;
    }
    ~TemporaryFile() { std::remove(path.c_str()); }
};
std::vector<char> read_file(const std::string& path) {
    std::ifstream in(path,std::ios::binary);
    require(bool(in),"Cannot read test file");
    return std::vector<char>(std::istreambuf_iterator<char>(in),{});
}
void write_file(const std::string& path,const std::vector<char>& bytes) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.write(bytes.data(),std::streamsize(bytes.size()));
    require(bool(out),"Cannot write test file");
}
bool same_plants(const std::vector<Plant>& a,const std::vector<Plant>& b) {
    if(a.size()!=b.size()) return false;
    for(size_t i=0;i<a.size();++i) {
        const auto& p=a[i]; const auto& q=b[i];
        if(p.x!=q.x || p.ground_y!=q.ground_y || p.born!=q.born ||
           p.lifespan_days!=q.lifespan_days || p.biomass!=q.biomass ||
           p.water!=q.water || p.sugar!=q.sugar || p.kind!=q.kind ||
           p.alive!=q.alive || p.died!=q.died) return false;
    }
    return true;
}
void test_founders_and_conservation() {
    World world;
    require(world.worker_count()==29,"Thirty founders must mean queen plus 29 workers");
    require(world.queen() && world.queen()->alive,"Founding queen missing");
    require(world.ants.size()==30,"Wrong founder count");
    valid(world);
    const auto mineral=world.mineral_total();
    for(unsigned hour=0;hour<24*7;++hour) {
        world.advance(3600);
        valid(world);
        require(world.mineral_total()==mineral,"Excavation, transport or collapse destroyed mineral soil");
    }
    require(world.stats.moves>0,"Headless execution did not simulate movement");
    require(world.stats.excavated>0,"Ants did not excavate their nest");
    require(world.stats.pickups>0,"Ants never picked up physical food");
    require(world.stats.worker_actions>world.stats.moves,"Expected worker behaviours beyond movement");
}
void test_determinism_and_chunking() {
    Config cfg; cfg.seed=92731;
    World continuous(cfg),chunked(cfg),repeated(cfg);
    constexpr uint64_t duration=3*Day+731;
    continuous.advance(duration);
    repeated.advance(duration);
    const std::vector<uint64_t> chunks={1,59,1,3598,1,47,17111,3600,601,17};
    uint64_t elapsed=0; size_t i=0;
    while(elapsed<duration) {
        const auto step=std::min(chunks[i++%chunks.size()],duration-elapsed);
        chunked.advance(step); elapsed+=step;
    }
    require(continuous.state_hash()==repeated.state_hash(),"Identical seed and duration gave different outcomes");
    require(continuous.state_hash()==chunked.state_hash(),"Advance depends on caller chunk boundaries");
    valid(continuous); valid(chunked);
    cfg.seed++;
    World alternate(cfg);
    alternate.advance(duration);
    require(alternate.state_hash()!=continuous.state_hash(),"Seed does not affect simulation");
    const auto before=continuous.state_hash();
    continuous.advance(0);
    require(continuous.state_hash()==before,"Zero-duration advance changed state");
}
void test_round_trip_and_resume() {
    TemporaryFile file(".sav");
    World source;
    source.advance(2*Day+127);
    valid(source);
    std::string error;
    require(source.save(file.path,&error),"Save failed: "+error);
    Config unrelated; unrelated.seed=999; unrelated.founders=40;
    World restored(unrelated);
    require(restored.load(file.path,&error),"Load failed: "+error);
    require(restored.state_hash()==source.state_hash(),"Round-trip omitted or changed persistent state");
    source.advance(Day+3611);
    restored.advance(Day+3611);
    require(restored.state_hash()==source.state_hash(),"Restored RNG or scheduler changed future behaviour");
    require(restored.save(file.path,&error),"Replacing an existing save failed: "+error);
    World replacement;
    require(replacement.load(file.path,&error),"Replacement save unreadable: "+error);
    require(replacement.state_hash()==restored.state_hash(),"Replacement save contains old state");
    valid(restored);
}
void test_corruption_is_transactional() {
    TemporaryFile good(".sav"),bad(".bad");
    World world;
    world.advance(3701);
    std::string error;
    require(world.save(good.path,&error),"Cannot prepare corruption fixture: "+error);
    const auto original=read_file(good.path);
    const auto hash=world.state_hash();
    auto reject=[&](const std::vector<char>& bytes,const std::string& name) {
        write_file(bad.path,bytes);
        error.clear();
        require(!world.load(bad.path,&error),"Accepted "+name);
        require(!error.empty(),"Missing diagnostic for "+name);
        require(world.state_hash()==hash,"Failed load changed current world: "+name);
    };
    auto changed=original; changed[0]^=0x01; reject(changed,"wrong magic");
    changed=original; changed[8]=char(127); reject(changed,"unknown version");
    changed=original; changed[12]^=0x01; reject(changed,"wrong dimensions");
    changed=original; changed[24]^=0x01; reject(changed,"different weather fingerprint");
    changed=original; changed[changed.size()/2]^=0x40; reject(changed,"corrupted payload");
    changed=original; changed.resize(changed.size()-1); reject(changed,"truncated payload");
    changed=original; changed.push_back('x'); reject(changed,"trailing bytes");
    reject({},"empty file");
    require(!world.load(bad.path+".missing",&error),"Missing file load succeeded");
    require(world.state_hash()==hash,"Missing file load changed current world");
}
void test_checkpoint_boundaries() {
    TemporaryFile file(".matrix");
    std::string error;
    for(const uint64_t seed:{1ULL,2ULL,17ULL,91ULL}) {
        Config cfg; cfg.seed=seed;
        World continuous(cfg);
        for(const uint64_t interval:{137ULL,1237ULL,3661ULL,7829ULL}) {
            continuous.advance(interval);
            require(continuous.save(file.path,&error),"Checkpoint matrix save failed: "+error);
            World resumed;
            require(resumed.load(file.path,&error),"Checkpoint matrix restore failed: "+error);
            continuous.advance(5713); resumed.advance(5713);
            require(continuous.state_hash()==resumed.state_hash(),
                    "Checkpoint continuation diverged at seed "+std::to_string(seed)+
                    ", simulation second "+std::to_string(continuous.seconds));
        }
    }
}
void test_physical_cargo_and_life_stages_persist() {
    TemporaryFile file(".life-stages");
    World source;
    source.advance(3601);
    // Persistence fixtures exercise states that need not naturally occur in a
    // short test. They are not used as ecological longevity evidence.
    auto& carrier=source.ants[1];
    carrier.task=Task::RemoveCorpse; carrier.payload=0;
    Corpse corpse;
    corpse.id=source.next_id++; corpse.x=carrier.x; corpse.y=carrier.y;
    corpse.died=uint32_t(source.seconds); corpse.organic=39; corpse.carried=true;
    source.corpses={corpse};
    auto& digger=source.ants[2];
    require(digger.soil_cargo==0,"Cargo fixture ant already carrying soil");
    source.cells[source.index(0,Height-1)].soil-=2;
    digger.soil_cargo=2; digger.task=Task::ReturnSoil;
    Brood larva;
    larva.id=source.next_id++; larva.x=carrier.x; larva.y=carrier.y;
    larva.born=uint32_t(source.seconds); larva.stage=Stage::Larva;
    larva.fed=17; larva.stress=5; larva.development=2.75f;
    source.brood={larva};
    source.plants[0].alive=false; source.plants[0].died=uint32_t(source.seconds);
    source.plants[0].biomass=.23f; source.plants[0].water=.33f;
    source.rebuild_transient();
    valid(source);
    std::string error;
    require(source.save(file.path,&error),"Life-stage fixture save failed: "+error);
    World restored;
    require(restored.load(file.path,&error),"Life-stage fixture load failed: "+error);
    require(restored.corpses.size()==1 && restored.corpses[0].organic==39 && restored.corpses[0].carried,
            "Save lost persistent corpse decomposition/carrying state");
    require(restored.ants[1].task==Task::RemoveCorpse && restored.ants[1].payload==0 &&
            restored.corpses[0].x==restored.ants[1].x && restored.corpses[0].y==restored.ants[1].y,
            "Save detached a carried corpse from its ant");
    require(restored.ants[2].soil_cargo==2 && restored.mineral_total()==source.mineral_total(),
            "Save lost soil in transit");
    require(restored.brood.size()==1 && restored.brood[0].stage==Stage::Larva &&
            restored.brood[0].fed==17 && restored.brood[0].stress==5 && restored.brood[0].development==2.75f,
            "Save lost larval feeding/development state");
    require(same_plants(restored.plants,source.plants),"Save changed a decomposing plant");
    require(restored.state_hash()==source.state_hash(),"Life-stage checkpoint state differs");
    valid(restored);
}
void test_pause_and_outage_catchup() {
    TemporaryFile file(".paused");
    World paused;
    paused.advance(7431);
    paused.power_off();
    require(paused.paused,"OFF did not pause the world");
    const auto hash=paused.state_hash();
    paused.advance(Day);
    paused.catch_up(7*Day);
    require(paused.state_hash()==hash,"Deliberately paused world progressed");
    std::string error;
    require(paused.save(file.path,&error),"Paused save failed: "+error);
    World restored;
    require(restored.load(file.path,&error),"Paused restore failed: "+error);
    require(restored.paused && restored.state_hash()==hash,"Save did not preserve exact paused state");
    restored.catch_up(Day);
    require(restored.state_hash()==hash,"Restoring a deliberate pause counted outage time");
    paused.power_on(); restored.power_on();
    paused.advance(2*Day+7); restored.catch_up(2*Day+7);
    require(paused.state_hash()==restored.state_hash(),"Outage catch-up differs from full-detail simulation");
    require(restored.seconds==7431+2*Day+7,"ON failed to resume the existing colony");
}
void test_extinction_requires_power_cycle() {
    World world;
    world.advance(Day+913);
    // A controlled extinct fixture avoids making this lifecycle test depend on
    // a particular ecological failure rate or waiting decades for queen death.
    for(auto& ant:world.ants) ant.alive=false;
    world.brood.clear(); world.extinct=true;
    world.rebuild_transient();
    const auto previous_colony=world.stats.colony_number;
    world.advance(6*3600);
    require(world.extinct && world.worker_count()==0,"Extinct world silently recolonized");
    require(world.stats.colony_number==previous_colony,"Colony incremented without a power cycle");
    world.power_on();
    require(world.extinct,"ON without prior OFF recolonized the world");
    const auto saved_seconds=world.seconds;
    const auto saved_plants=world.plants;
    const auto saved_seeds=world.seed_bank;
    world.power_off(); world.power_on();
    require(!world.extinct && !world.paused,"OFF/ON failed to create replacement colony");
    require(world.seconds==saved_seconds,"Recolonization reset elapsed day/time");
    require(same_plants(world.plants,saved_plants),"Recolonization replaced plants");
    require(world.seed_bank==saved_seeds,"Recolonization replaced seed bank");
    require(world.stats.colony_number==previous_colony+1,"Replacement colony counter incorrect");
    require(world.ants.size()==world.config.founders && world.worker_count()==unsigned(world.config.founders-1),"Replacement founder count incorrect");
    for(const auto& ant:world.ants) {
        require(ant.alive,"Replacement founder is dead");
        require(ant.x<16 || ant.x>=Width-16,"Replacement founder did not enter from an edge");
        require(ant.task==Task::Arrive || ant.queen,"Replacement worker did not arrive visibly");
    }
    require(world.brood.empty() && world.corpses.empty(),"Old colony remains in fresh nest");
    valid(world);
}
void test_hash_ignores_transient_rebuild() {
    World world;
    world.advance(17*3600+137);
    const auto hash=world.state_hash();
    world.rebuild_transient();
    require(world.state_hash()==hash,"Rebuilding caches changed persistent content hash");
}
void test_spoil_support_and_climbing() {
    World world;
    // A solid vertical face supports an ant, while detached sky does not.
    world.cells[world.index(10,Surface-6)].soil=SoilFull;
    world.cells[world.index(0,Height-1)].soil=0;
    require(world.walkable(9,Surface-6),"Ant cannot climb a vertical spoil face");
    require(!world.walkable(7,Surface-6),"Ant can walk through unsupported sky");
    world.rebuild_transient();
    World natural;
    natural.advance(7*Day);
    for(int x=2;x<Width-2;++x)for(int y=1;y<natural.ground[x]-1;++y){
        if(!natural.cells[natural.index(x,y)].soil)continue;
        require(natural.cells[natural.index(x-1,y+1)].soil&&natural.cells[natural.index(x+1,y+1)].soil,
                "Loose spoil formed an unsupported vertical tower");
    }
    valid(natural);
}
} // namespace

int main() {
    const std::vector<std::pair<const char*,std::function<void()>>> tests={
        {"founders, food pickup, excavation and seven-day mineral conservation",test_founders_and_conservation},
        {"seed determinism and arbitrary advance chunk equivalence",test_determinism_and_chunking},
        {"complete save round trip and identical continuation",test_round_trip_and_resume},
        {"checkpoint continuation across seeds and event boundaries",test_checkpoint_boundaries},
        {"physical cargo, larval feeding and decomposition survive saves",test_physical_cargo_and_life_stages_persist},
        {"corruption rejection leaves current world unchanged",test_corruption_is_transactional},
        {"deliberate pause, paused restore and full-detail outage catch-up",test_pause_and_outage_catchup},
        {"extinction stays extinct until OFF/ON, preserving day and plants",test_extinction_requires_power_cycle},
        {"content hash excludes transient caches",test_hash_ignores_transient_rebuild},
        {"ants climb exposed spoil faces and loose piles retain support",test_spoil_support_and_climbing},
    };
    unsigned failures=0;
    for(const auto& test:tests) {
        try { test.second(); std::cout<<"PASS "<<test.first<<'\n'; }
        catch(const std::exception& ex) { ++failures; std::cerr<<"FAIL "<<test.first<<": "<<ex.what()<<'\n'; }
    }
    std::cout<<tests.size()-failures<<'/'<<tests.size()<<" core tests passed\n";
    return failures?1:0;
}
