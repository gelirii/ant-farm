#pragma once
#include "weather.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace antfarm {
constexpr int Width=180, Height=320, Surface=96, CellCount=Width*Height;
constexpr uint16_t SoilFull=8;
constexpr uint64_t Day=86400;
struct Rng {
    uint64_t state=1;
    uint32_t next() { state ^= state>>12; state ^= state<<25; state ^= state>>27; return uint32_t((state*2685821657736338717ULL)>>32); }
    uint32_t bounded(uint32_t n) { return n?uint32_t((uint64_t(next())*n)>>32):0; }
    float unit() { return (next()>>8)*(1.0f/16777216.0f); }
};
struct Config {
    uint64_t seed=42;
    uint16_t founders=30; // Includes one queen.
    uint16_t max_workers=512;
    float nectar_scale=1.0f, insect_scale=0.25f, rain_scale=1.0f;
    bool enable_collapses=true;
};
enum class Task:uint8_t { Arrive, Explore, Forage, ReturnFood, Excavate, ReturnSoil, Nurse, Rest, RemoveCorpse, Queen };
struct Cell { uint16_t home=0, trail=0, brood=0; uint8_t soil=SoilFull, moisture=95, nutrients=20, disturbance=0; };
struct Ant {
    uint32_t id=0;
    int16_t x=0,y=0,previous_x=0,previous_y=0;
    int16_t nest_x=Width/2,nest_y=Surface-1;
    uint16_t crop=0, protein=0;
    uint16_t payload=0;
    uint32_t born=0, lifespan=0, last_move=0;
    uint64_t next_action=0;
    uint32_t moves=0, food_delivered=0, soil_moved=0;
    uint16_t patience=0;
    Task task=Task::Arrive;
    uint8_t heading=0, soil_cargo=0;
    bool queen=false, alive=true;
};
enum class Stage:uint8_t { Egg, Larva, Pupa };
struct Brood { uint32_t id=0; int16_t x=0,y=0; uint32_t born=0; float development=0; uint16_t fed=0; Stage stage=Stage::Egg; uint8_t stress=0; };
struct Corpse { int16_t x=0,y=0; uint32_t died=0; uint32_t id=0; uint16_t organic=24; bool carried=false; };
struct Plant { int16_t x=0,ground_y=0; uint32_t born=0; uint16_t lifespan_days=63; float biomass=0,water=1; uint16_t sugar=0; uint8_t kind=0; bool alive=true; uint32_t died=0; };
struct Food { int16_t x=0,y=0; uint16_t sugar=0,protein=0; uint32_t created=0; uint8_t kind=0; };
struct Stats {
    uint64_t worker_actions=0, moves=0, pickups=0, exchanges=0, births=0, deaths=0;
    uint64_t eggs_laid=0, brood_deaths=0, excavated=0, deposited=0, collapses=0;
    uint64_t sugar_input=0, protein_input=0, sugar_metabolized=0, protein_used=0;
    uint64_t germinations=0, plant_deaths=0, corpse_decompositions=0;
    uint64_t mineral_baseline=0, colony_number=1;
    uint32_t extinction_second=0;
};
struct Snapshot { uint64_t seconds=0; unsigned workers=0,brood=0,corpses=0,plants=0,food_sites=0; bool queen_alive=false,extinct=false; uint64_t mineral=0; uint64_t births=0,deaths=0,excavated=0; float sugar=0,protein=0; };

class World {
public:
    Config config;
    Rng rng;
    uint64_t seconds=0;
    uint32_t next_id=1;
    bool paused=false;
    bool extinct=false;
    std::vector<Cell> cells;
    std::vector<Ant> ants;
    std::vector<Brood> brood;
    std::vector<Corpse> corpses;
    std::vector<Plant> plants;
    std::vector<Food> foods;
    std::array<int16_t,Width> ground{};
    std::array<uint16_t,Width> seed_bank{};
    WeatherTape weather;
    WeatherSample conditions{};
    Stats stats;
    explicit World(Config cfg={});
    void advance(uint64_t duration_seconds);
    void power_off();
    void power_on();
    void catch_up(uint64_t elapsed_seconds); // Same core, no ecological surrogate.
    bool save(const std::string& path, std::string* error=nullptr) const;
    bool load(const std::string& path, std::string* error=nullptr);
    Snapshot snapshot() const;
    uint64_t mineral_total() const;
    uint64_t state_hash() const;
    size_t memory_bytes() const;
    bool validate(std::string* error=nullptr) const;
    bool in_bounds(int x,int y) const {return x>=0&&x<Width&&y>=0&&y<Height;}
    int index(int x,int y) const {return y*Width+x;}
    bool open(int x,int y) const {return in_bounds(x,y)&&cells[index(x,y)].soil==0;}
    bool walkable(int x,int y) const;
    unsigned worker_count() const;
    const Ant* queen() const;
    Ant* queen();
    void rebuild_transient();
private:
    // Rebuilt after loading. Integer bucket scheduler avoids per-ant polling.
    static constexpr unsigned WheelSize=1024;
    std::array<std::vector<uint16_t>,WheelSize> wheel_;
    std::vector<int32_t> occupancy_;
    std::vector<int32_t> next_occupant_;
    std::vector<uint16_t> scratch_home_,scratch_trail_;
    std::vector<uint32_t> walk_cells_;
    std::vector<uint8_t> walkable_cache_;
    bool topology_dirty_=true;
    uint64_t next_minute_=60,next_hour_=3600;
    void init_soil();
    void set_soil(int cell,uint8_t amount);
    bool can_walk(int x,int y)const{
#ifdef ANTFARM_REFERENCE_ACCESS
        return walkable(x,y);
#else
        return in_bounds(x,y)&&walkable_cache_[index(x,y)];
#endif
    }
    void found_colony();
    void restart_after_extinction();
    void schedule(unsigned i,uint32_t delay);
    void act(unsigned i);
    void move_ant(Ant& a,int x,int y);
    void minute();
    void hour();
    void chemistry();
    void ecology();
    void metabolism();
    void die(Ant& a);
    void rebuild_occupancy();
    void share_food(Ant& a);
    bool take_food(Ant& a);
    bool dig(Ant& a);
    bool deposit(Ant& a);
    bool step(Ant& a,bool homeward,bool exploring=false);
};

const char* task_name(Task t);
} // namespace antfarm
