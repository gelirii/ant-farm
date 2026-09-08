#pragma once
#include "core.hpp"
#include <cstdint>
#include <string>

namespace antfarm {
struct CheckpointRecovery {
    uint64_t sequence=0;
    uint64_t saved_wall_seconds=0;
    uint64_t simulated_outage_seconds=0;
    unsigned slot=0;
    bool was_paused=false;
};

// Serialize calls and world access externally. Wall time is supplied by the
// platform's battery-backed RTC; this portable controller does not read hardware.
class CheckpointStore {
public:
    explicit CheckpointStore(std::string base_path);
    std::string slot_path(unsigned slot) const;
    bool write(const World& world,uint64_t wall_seconds,std::string* error=nullptr) const;
    bool restore(World& world,uint64_t wall_seconds,bool resume_paused=false,
                 CheckpointRecovery* recovery=nullptr,std::string* error=nullptr) const;
private:
    std::string base_path_;
};
} // namespace antfarm
