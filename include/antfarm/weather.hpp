#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace antfarm {

struct WeatherSample {
    float temperature_c = 18.0f;
    float rain_mm_hour = 0.0f;
    float light = 0.0f; // Solar irradiance / 800 W/m2, clamped to [0,1].
    float wind = 0.0f;  // m/s at the archive's 10 m reference height.
};

class WeatherTape {
public:
    // Explicitly labelled synthetic diagnostic weather until load_csv succeeds.
    WeatherTape();
    // Strict import. Throws on malformed, missing, duplicate or non-hourly data.
    // A failed import leaves the existing tape unchanged.
    void load_csv(const std::string& path);
    WeatherSample sample(uint64_t sim_seconds, uint64_t seed) const;
    std::size_t size() const { return hours_.size(); }
    uint64_t period_seconds() const;
    const std::string& metadata() const { return metadata_; }
    uint64_t fingerprint() const { return fingerprint_; }
    bool historical() const { return historical_; }

private:
    struct PackedHour {
        int16_t temperature_decic;
        uint16_t rain_centimm;
        uint16_t solar_wm2;
        uint16_t wind_centims;
    };
    static_assert(sizeof(PackedHour) == 8, "Weather records must remain 8 bytes");
    std::vector<PackedHour> hours_;
    std::string metadata_;
    uint32_t seam_hours_ = 120;
    uint64_t fingerprint_ = 0;
    bool historical_ = false;
    WeatherSample raw_sample(uint64_t seconds) const;
    void update_fingerprint();
};

} // namespace antfarm
