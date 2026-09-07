#include "antfarm/core.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace antfarm {
namespace {
constexpr uint32_t FormatVersion=2;
constexpr uint64_t MaxSaveBytes=64ULL*1024*1024;
constexpr uint8_t Magic[8]={'A','N','T','F','A','R','M',0};
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
              "Save format requires IEEE-754 binary32 floats");

template<class Derived> struct Output {
    void u8(uint8_t v) { static_cast<Derived*>(this)->u8(v); }
    void u16(uint16_t v) { for(unsigned i=0;i<2;++i) u8(uint8_t(v>>(8*i))); }
    void i16(int16_t v) { u16(uint16_t(v)); }
    void u32(uint32_t v) { for(unsigned i=0;i<4;++i) u8(uint8_t(v>>(8*i))); }
    void u64(uint64_t v) { for(unsigned i=0;i<8;++i) u8(uint8_t(v>>(8*i))); }
    void boolean(bool v) { u8(v?1:0); }
    void real(float v) { uint32_t bits; std::memcpy(&bits,&v,4); u32(bits); }
};
struct Writer:Output<Writer> {
    std::vector<uint8_t> bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
};
struct Hasher:Output<Hasher> {
    uint64_t hash=14695981039346656037ULL;
    void u8(uint8_t v) { hash^=v; hash*=1099511628211ULL; }
};
struct Reader {
    const std::vector<uint8_t>& bytes;
    size_t at=0;
    uint8_t u8() { if(at==bytes.size()) throw std::runtime_error("Truncated save"); return bytes[at++]; }
    uint16_t u16() { uint16_t v=0; for(unsigned i=0;i<2;++i) v|=uint16_t(u8())<<(8*i); return v; }
    int16_t i16() { const uint16_t v=u16(); return v<=32767?int16_t(v):int16_t(int32_t(v)-65536); }
    uint32_t u32() { uint32_t v=0; for(unsigned i=0;i<4;++i) v|=uint32_t(u8())<<(8*i); return v; }
    uint64_t u64() { uint64_t v=0; for(unsigned i=0;i<8;++i) v|=uint64_t(u8())<<(8*i); return v; }
    bool boolean() { const auto v=u8(); if(v>1) throw std::runtime_error("Invalid boolean in save"); return v!=0; }
    float real() { uint32_t bits=u32(); float v; std::memcpy(&v,&bits,4); if(!std::isfinite(v)) throw std::runtime_error("Non-finite number in save"); return v; }
    size_t count(size_t maximum,size_t item_size) {
        const auto n=u32();
        if(n>maximum || n>(bytes.size()-at)/item_size) throw std::runtime_error("Invalid collection size in save");
        return n;
    }
};
uint32_t crc32(const uint8_t* bytes,size_t size) {
    uint32_t crc=0xffffffffU;
    for(size_t i=0;i<size;++i) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));
    }
    return ~crc;
}
template<class Sink> void put_stats(Sink& w,const Stats& s) {
    w.u64(s.worker_actions); w.u64(s.moves); w.u64(s.pickups); w.u64(s.exchanges);
    w.u64(s.births); w.u64(s.deaths); w.u64(s.eggs_laid); w.u64(s.brood_deaths);
    w.u64(s.excavated); w.u64(s.deposited); w.u64(s.collapses); w.u64(s.sugar_input);
    w.u64(s.protein_input); w.u64(s.sugar_metabolized); w.u64(s.protein_used);
    w.u64(s.germinations); w.u64(s.plant_deaths); w.u64(s.corpse_decompositions);
    w.u64(s.mineral_baseline); w.u64(s.colony_number); w.u32(s.extinction_second);
}
void get_stats(Reader& r,Stats& s) {
    s.worker_actions=r.u64(); s.moves=r.u64(); s.pickups=r.u64(); s.exchanges=r.u64();
    s.births=r.u64(); s.deaths=r.u64(); s.eggs_laid=r.u64(); s.brood_deaths=r.u64();
    s.excavated=r.u64(); s.deposited=r.u64(); s.collapses=r.u64(); s.sugar_input=r.u64();
    s.protein_input=r.u64(); s.sugar_metabolized=r.u64(); s.protein_used=r.u64();
    s.germinations=r.u64(); s.plant_deaths=r.u64(); s.corpse_decompositions=r.u64();
    s.mineral_baseline=r.u64(); s.colony_number=r.u64(); s.extinction_second=r.u32();
}
template<class Sink> void encode(Sink& w,const World& s,uint64_t next_minute,uint64_t next_hour) {
    w.u64(s.config.seed); w.u16(s.config.founders); w.u16(s.config.max_workers);
    w.real(s.config.nectar_scale); w.real(s.config.insect_scale); w.real(s.config.rain_scale);
    w.boolean(s.config.enable_collapses);
    w.u64(s.rng.state); w.u64(s.seconds); w.u32(s.next_id); w.boolean(s.paused); w.boolean(s.extinct);
    w.u64(next_minute); w.u64(next_hour);
    w.real(s.conditions.temperature_c); w.real(s.conditions.rain_mm_hour); w.real(s.conditions.light); w.real(s.conditions.wind);
    put_stats(w,s.stats);
    for(const auto value:s.ground) w.i16(value);
    for(const auto value:s.seed_bank) w.u16(value);
    w.u32(uint32_t(s.cells.size()));
    for(const auto& c:s.cells) {
        w.u16(c.home); w.u16(c.trail); w.u16(c.brood);
        w.u8(c.soil); w.u8(c.moisture); w.u8(c.nutrients); w.u8(c.disturbance);
    }
    w.u32(uint32_t(s.ants.size()));
    for(const auto& a:s.ants) {
        w.u32(a.id); w.i16(a.x); w.i16(a.y); w.i16(a.previous_x); w.i16(a.previous_y);
        w.i16(a.nest_x); w.i16(a.nest_y);
        w.u16(a.crop); w.u16(a.protein); w.u16(a.payload); w.u32(a.born); w.u32(a.lifespan); w.u32(a.last_move);
        w.u64(a.next_action); w.u32(a.moves); w.u32(a.food_delivered); w.u32(a.soil_moved);
        w.u16(a.patience); w.u8(uint8_t(a.task)); w.u8(a.heading); w.u8(a.soil_cargo); w.boolean(a.queen); w.boolean(a.alive);
    }
    w.u32(uint32_t(s.brood.size()));
    for(const auto& b:s.brood) {
        w.u32(b.id); w.i16(b.x); w.i16(b.y); w.u32(b.born); w.real(b.development); w.u16(b.fed); w.u8(uint8_t(b.stage)); w.u8(b.stress);
    }
    w.u32(uint32_t(s.corpses.size()));
    for(const auto& c:s.corpses) { w.i16(c.x); w.i16(c.y); w.u32(c.died); w.u32(c.id); w.u16(c.organic); w.boolean(c.carried); }
    w.u32(uint32_t(s.plants.size()));
    for(const auto& p:s.plants) { w.i16(p.x); w.i16(p.ground_y); w.u32(p.born); w.u16(p.lifespan_days); w.real(p.biomass); w.real(p.water); w.u16(p.sugar); w.u8(p.kind); w.boolean(p.alive); w.u32(p.died); }
    w.u32(uint32_t(s.foods.size()));
    for(const auto& f:s.foods) { w.i16(f.x); w.i16(f.y); w.u16(f.sugar); w.u16(f.protein); w.u32(f.created); w.u8(f.kind); }
}
void decode(Reader& r,World& s,uint64_t& next_minute,uint64_t& next_hour) {
    s.config.seed=r.u64(); s.config.founders=r.u16(); s.config.max_workers=r.u16();
    s.config.nectar_scale=r.real(); s.config.insect_scale=r.real(); s.config.rain_scale=r.real();
    s.config.enable_collapses=r.boolean();
    s.rng.state=r.u64(); s.seconds=r.u64(); s.next_id=r.u32(); s.paused=r.boolean(); s.extinct=r.boolean();
    next_minute=r.u64(); next_hour=r.u64();
    s.conditions.temperature_c=r.real(); s.conditions.rain_mm_hour=r.real(); s.conditions.light=r.real(); s.conditions.wind=r.real();
    get_stats(r,s.stats);
    for(auto& value:s.ground) value=r.i16();
    for(auto& value:s.seed_bank) value=r.u16();
    if(r.count(CellCount,10)!=CellCount) throw std::runtime_error("Save grid has wrong cell count");
    s.cells.resize(CellCount);
    for(auto& c:s.cells) {
        c.home=r.u16(); c.trail=r.u16(); c.brood=r.u16();
        c.soil=r.u8(); c.moisture=r.u8(); c.nutrients=r.u8(); c.disturbance=r.u8();
    }
    s.ants.resize(r.count(65535,61));
    for(auto& a:s.ants) {
        a.id=r.u32(); a.x=r.i16(); a.y=r.i16(); a.previous_x=r.i16(); a.previous_y=r.i16();
        a.nest_x=r.i16(); a.nest_y=r.i16();
        a.crop=r.u16(); a.protein=r.u16(); a.payload=r.u16(); a.born=r.u32(); a.lifespan=r.u32(); a.last_move=r.u32();
        a.next_action=r.u64(); a.moves=r.u32(); a.food_delivered=r.u32(); a.soil_moved=r.u32();
        a.patience=r.u16(); const auto task=r.u8();
        if(task>uint8_t(Task::Queen)) throw std::runtime_error("Invalid ant task in save");
        a.task=Task(task); a.heading=r.u8(); a.soil_cargo=r.u8(); a.queen=r.boolean(); a.alive=r.boolean();
        if(a.alive && !s.in_bounds(a.x,a.y)) throw std::runtime_error("Live ant outside save grid");
        if(a.alive && a.next_action<=s.seconds) throw std::runtime_error("Overdue ant action in save");
    }
    s.brood.resize(r.count(1000000,20));
    for(auto& b:s.brood) {
        b.id=r.u32(); b.x=r.i16(); b.y=r.i16(); b.born=r.u32(); b.development=r.real(); b.fed=r.u16();
        const auto stage=r.u8(); if(stage>uint8_t(Stage::Pupa)) throw std::runtime_error("Invalid brood stage in save");
        b.stage=Stage(stage); b.stress=r.u8();
        if(!s.in_bounds(b.x,b.y)) throw std::runtime_error("Brood outside save grid");
    }
    s.corpses.resize(r.count(1000000,15));
    for(auto& c:s.corpses) {
        c.x=r.i16(); c.y=r.i16(); c.died=r.u32(); c.id=r.u32(); c.organic=r.u16(); c.carried=r.boolean();
        if(!s.in_bounds(c.x,c.y)) throw std::runtime_error("Corpse outside save grid");
    }
    s.plants.resize(r.count(1000000,26));
    for(auto& p:s.plants) {
        p.x=r.i16(); p.ground_y=r.i16(); p.born=r.u32(); p.lifespan_days=r.u16(); p.biomass=r.real(); p.water=r.real(); p.sugar=r.u16(); p.kind=r.u8(); p.alive=r.boolean(); p.died=r.u32();
        if(!s.in_bounds(p.x,p.ground_y)) throw std::runtime_error("Plant outside save grid");
    }
    s.foods.resize(r.count(1000000,13));
    for(auto& f:s.foods) {
        f.x=r.i16(); f.y=r.i16(); f.sugar=r.u16(); f.protein=r.u16(); f.created=r.u32(); f.kind=r.u8();
        if(!s.in_bounds(f.x,f.y)) throw std::runtime_error("Food outside save grid");
    }
    if(r.at!=r.bytes.size()) throw std::runtime_error("Unexpected trailing save data");
    if(!s.rng.state || s.seconds>std::numeric_limits<uint32_t>::max() ||
       s.config.founders<2 || !s.config.max_workers || s.config.max_workers>4095 ||
       s.config.founders>unsigned(s.config.max_workers)+1 || s.ants.size()>unsigned(s.config.max_workers)+1 ||
       s.config.nectar_scale<0 || s.config.insect_scale<0 || s.config.rain_scale<0)
        throw std::runtime_error("Invalid simulation configuration in save");
    if(next_minute<=s.seconds || next_minute-s.seconds>60 || next_hour<=s.seconds || next_hour-s.seconds>3600)
        throw std::runtime_error("Invalid ecology schedule in save");
    for(const auto y:s.ground) if(y<0 || y>=Height) throw std::runtime_error("Invalid ground level in save");
}
void error_message(std::string* error,const std::string& message) { if(error) *error=message; }
} // namespace

uint64_t World::state_hash() const {
    Hasher hash;
    hash.u64(weather.fingerprint());
    encode(hash,*this,next_minute_,next_hour_);
    return hash.hash;
}

bool World::save(const std::string& path,std::string* error) const {
    // Sibling temp file keeps rename on the same filesystem. Distinct suffixes
    // prevent concurrent saves in this process from sharing a staging file.
    static std::atomic<uint64_t> sequence{0};
    const auto suffix=sequence.fetch_add(1,std::memory_order_relaxed);
    const std::string temp=path+".tmp."+std::to_string(reinterpret_cast<uintptr_t>(this))+"."+std::to_string(suffix);
    try {
        std::string validation;
        if(!validate(&validation)) throw std::runtime_error("Cannot save invalid world: "+validation);
        Writer payload;
        payload.bytes.reserve(640*1024);
        encode(payload,*this,next_minute_,next_hour_);
        Writer header;
        for(const auto byte:Magic) header.u8(byte);
        header.u32(FormatVersion); header.u32(Width); header.u32(Height); header.u32(Surface);
        header.u64(weather.fingerprint()); header.u64(payload.bytes.size());
        header.u32(crc32(payload.bytes.data(),payload.bytes.size()));
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        if(!out) throw std::runtime_error("Cannot open temporary save file");
        out.write(reinterpret_cast<const char*>(header.bytes.data()),std::streamsize(header.bytes.size()));
        out.write(reinterpret_cast<const char*>(payload.bytes.data()),std::streamsize(payload.bytes.size()));
        out.flush();
        if(!out) throw std::runtime_error("Failed writing save file");
        out.close();
        if(!out) throw std::runtime_error("Failed closing save file");
        if(std::rename(temp.c_str(),path.c_str())!=0) throw std::runtime_error("Cannot atomically replace save file");
        error_message(error,"");
        return true;
    } catch(const std::exception& ex) {
        std::remove(temp.c_str());
        error_message(error,ex.what());
        return false;
    }
}

bool World::load(const std::string& path,std::string* error) {
    try {
        std::ifstream in(path,std::ios::binary|std::ios::ate);
        if(!in) throw std::runtime_error("Cannot open save file");
        const auto size=in.tellg();
        if(size<44 || uint64_t(size)>MaxSaveBytes) throw std::runtime_error("Invalid save file size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        in.seekg(0);
        if(!in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))) throw std::runtime_error("Failed reading save file");
        Reader header{bytes};
        for(const auto byte:Magic) if(header.u8()!=byte) throw std::runtime_error("Not an ant-farm save");
        if(header.u32()!=FormatVersion) throw std::runtime_error("Unsupported save version");
        if(header.u32()!=Width || header.u32()!=Height || header.u32()!=Surface) throw std::runtime_error("Incompatible save dimensions");
        if(header.u64()!=weather.fingerprint()) throw std::runtime_error("Save weather source does not match loaded weather");
        const auto payload_size=header.u64(); const auto checksum=header.u32();
        if(payload_size!=bytes.size()-header.at) throw std::runtime_error("Save length mismatch");
        if(crc32(bytes.data()+header.at,size_t(payload_size))!=checksum) throw std::runtime_error("Save checksum mismatch");
        // Decode into a separate world. All errors leave this object's state,
        // including RNG and scheduler, exactly as it was before the call.
        World candidate(config);
        candidate.weather=weather;
        Reader reader{bytes,header.at};
        decode(reader,candidate,candidate.next_minute_,candidate.next_hour_);
        std::string validation;
        if(!candidate.validate(&validation)) throw std::runtime_error("Invalid saved world: "+validation);
        const auto saved_minute=candidate.next_minute_,saved_hour=candidate.next_hour_;
        candidate.rebuild_transient();
        candidate.next_minute_=saved_minute; candidate.next_hour_=saved_hour;
        *this=std::move(candidate);
        error_message(error,"");
        return true;
    } catch(const std::exception& ex) {
        error_message(error,ex.what());
        return false;
    }
}
} // namespace antfarm
