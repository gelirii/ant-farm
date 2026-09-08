#include "antfarm/checkpoint.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace antfarm {
namespace {
constexpr uint8_t Magic[8]={'A','N','T','C','H','K','P',0};
constexpr uint32_t Version=1;
constexpr size_t HeaderBytes=40;
constexpr uint64_t MaxSnapshotBytes=64ULL*1024*1024;
struct Temporary {
    std::string path;
    explicit Temporary(const std::string& base) {
        static std::atomic<uint64_t> sequence{0};
        path=base+".scratch."+std::to_string(reinterpret_cast<uintptr_t>(this))+"."+
             std::to_string(sequence.fetch_add(1,std::memory_order_relaxed));
    }
    ~Temporary() { std::remove(path.c_str()); }
};
struct Envelope {
    uint64_t sequence=0,wall_seconds=0;
    unsigned slot=0;
    std::vector<uint8_t> bytes;
};
void set_error(std::string* error,const std::string& message) { if(error)*error=message; }
void put(std::vector<uint8_t>& out,uint64_t value,unsigned size) {
    for(unsigned i=0;i<size;++i)out.push_back(uint8_t(value>>(8*i)));
}
uint64_t get(const std::vector<uint8_t>& bytes,size_t offset,unsigned size) {
    uint64_t value=0;
    for(unsigned i=0;i<size;++i)value|=uint64_t(bytes[offset+i])<<(8*i);
    return value;
}
uint32_t crc_part(uint32_t crc,const uint8_t* bytes,size_t count) {
    for(size_t i=0;i<count;++i){
        crc^=bytes[i];
        for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));
    }
    return crc;
}
std::vector<uint8_t> read_bytes(const std::string& path,uint64_t maximum) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in)throw std::runtime_error("Cannot open "+path);
    auto length=in.tellg();
    if(length<0||uint64_t(length)>maximum)throw std::runtime_error("Invalid checkpoint size");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    in.seekg(0);
    if(!bytes.empty()&&!in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))
        throw std::runtime_error("Cannot read complete checkpoint");
    return bytes;
}
void write_bytes(const std::string& path,const uint8_t* bytes,size_t size) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error("Cannot open checkpoint staging file");
    out.write(reinterpret_cast<const char*>(bytes),std::streamsize(size));
    out.flush();
    if(!out)throw std::runtime_error("Cannot write complete checkpoint");
    out.close();
    if(!out)throw std::runtime_error("Cannot close checkpoint staging file");
}
Envelope read_envelope(const std::string& path,unsigned slot) {
    Envelope envelope;
    envelope.bytes=read_bytes(path,MaxSnapshotBytes+HeaderBytes);
    const auto& bytes=envelope.bytes;
    if(bytes.size()<HeaderBytes)throw std::runtime_error("Truncated checkpoint header");
    if(!std::equal(std::begin(Magic),std::end(Magic),bytes.begin()))throw std::runtime_error("Invalid checkpoint magic");
    if(get(bytes,8,4)!=Version)throw std::runtime_error("Unsupported checkpoint envelope version");
    envelope.sequence=get(bytes,12,8); envelope.wall_seconds=get(bytes,20,8); envelope.slot=slot;
    if(!envelope.sequence||get(bytes,28,8)!=bytes.size()-HeaderBytes)throw std::runtime_error("Invalid checkpoint sequence or length");
    const auto crc=~crc_part(crc_part(0xffffffffU,bytes.data(),36),bytes.data()+HeaderBytes,bytes.size()-HeaderBytes);
    if(crc!=get(bytes,36,4))throw std::runtime_error("Checkpoint checksum mismatch");
    return envelope;
}
std::vector<Envelope> envelopes(const CheckpointStore& store) {
    std::vector<Envelope> found;
    for(unsigned slot=0;slot<2;++slot) {
        try { found.push_back(read_envelope(store.slot_path(slot),slot)); }
        catch(const std::exception&) { /* An interrupted/corrupt slot is recoverable. */ }
    }
    std::sort(found.begin(),found.end(),[](const Envelope& a,const Envelope& b){
        return a.sequence!=b.sequence?a.sequence>b.sequence:a.slot<b.slot;
    });
    return found;
}
bool load_snapshot(const Envelope& envelope,World& world,const std::string& base,std::string& error) {
    Temporary staging(base);
    write_bytes(staging.path,envelope.bytes.data()+HeaderBytes,envelope.bytes.size()-HeaderBytes);
    return world.load(staging.path,&error);
}
} // namespace

CheckpointStore::CheckpointStore(std::string base_path):base_path_(std::move(base_path)) {
    if(base_path_.empty())throw std::invalid_argument("Checkpoint base path must not be empty");
}
std::string CheckpointStore::slot_path(unsigned slot)const {
    if(slot>1)throw std::invalid_argument("Checkpoint slot must be 0 or 1");
    return base_path_+"."+std::to_string(slot)+".checkpoint";
}
bool CheckpointStore::write(const World& world,uint64_t wall_seconds,std::string* error)const {
    try {
        auto previous=envelopes(*this);
        uint64_t sequence=1;unsigned target=0;
        // Keep the newest slot whose nested world can actually be restored.
        // An intact envelope around a damaged/incompatible snapshot is not a
        // fallback checkpoint for this world's weather source.
        for(const auto& entry:previous) {
            World probe=world;std::string diagnostic;
            if(!load_snapshot(entry,probe,base_path_,diagnostic))continue;
            if(wall_seconds<entry.wall_seconds)throw std::runtime_error("RTC moved backwards; checkpoint not written");
            if(entry.sequence==std::numeric_limits<uint64_t>::max())throw std::runtime_error("Checkpoint sequence exhausted");
            sequence=entry.sequence+1;target=1-entry.slot;break;
        }
        Temporary snapshot(base_path_);
        std::string diagnostic;
        if(!world.save(snapshot.path,&diagnostic))throw std::runtime_error("World checkpoint failed: "+diagnostic);
        auto payload=read_bytes(snapshot.path,MaxSnapshotBytes);
        std::vector<uint8_t> bytes;bytes.reserve(HeaderBytes+payload.size());
        bytes.insert(bytes.end(),std::begin(Magic),std::end(Magic));
        put(bytes,Version,4);put(bytes,sequence,8);put(bytes,wall_seconds,8);put(bytes,payload.size(),8);
        const auto crc=~crc_part(crc_part(0xffffffffU,bytes.data(),bytes.size()),payload.data(),payload.size());
        put(bytes,crc,4);bytes.insert(bytes.end(),payload.begin(),payload.end());
        Temporary staging(slot_path(target));
        write_bytes(staging.path,bytes.data(),bytes.size());
        if(std::rename(staging.path.c_str(),slot_path(target).c_str())!=0)throw std::runtime_error("Cannot atomically replace checkpoint slot");
        set_error(error,"");return true;
    }catch(const std::exception& ex){set_error(error,ex.what());return false;}
}
bool CheckpointStore::restore(World& world,uint64_t wall_seconds,bool resume_paused,
                              CheckpointRecovery* recovery,std::string* error)const {
    try {
        auto available=envelopes(*this);
        std::string diagnostic="No valid checkpoint slot";
        for(const auto& entry:available) {
            World candidate=world;
            if(!load_snapshot(entry,candidate,base_path_,diagnostic))continue;
            // Clock errors must not silently select an older slot: that would
            // hide an invalid RTC and could replay the wrong amount of time.
            if(wall_seconds<entry.wall_seconds)throw std::runtime_error("RTC moved backwards; current world left unchanged");
            CheckpointRecovery result;
            result.sequence=entry.sequence;result.saved_wall_seconds=entry.wall_seconds;
            result.slot=entry.slot;result.was_paused=candidate.paused;
            if(!candidate.paused){
                result.simulated_outage_seconds=wall_seconds-entry.wall_seconds;
                candidate.catch_up(result.simulated_outage_seconds);
            }else if(resume_paused)candidate.power_on();
            if(!candidate.validate(&diagnostic))throw std::runtime_error("Restored checkpoint failed validation: "+diagnostic);
            world=std::move(candidate);
            if(recovery)*recovery=result;
            set_error(error,"");return true;
        }
        throw std::runtime_error("No restorable checkpoint slot: "+diagnostic);
    }catch(const std::exception& ex){set_error(error,ex.what());return false;}
}
} // namespace antfarm
