#include "antfarm/weather.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace antfarm {
namespace {
uint64_t mix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
float signed_unit(uint64_t x) {
    return float(mix(x) >> 40) * (2.0f / 16777215.0f) - 1.0f;
}
WeatherSample blend(const WeatherSample& a, const WeatherSample& b, float t) {
    return {a.temperature_c + (b.temperature_c-a.temperature_c)*t,
            a.rain_mm_hour + (b.rain_mm_hour-a.rain_mm_hour)*t,
            a.light + (b.light-a.light)*t, a.wind + (b.wind-a.wind)*t};
}
WeatherSample vary(WeatherSample a, uint64_t seed, uint64_t cycle) {
    const uint64_t key = mix(seed) ^ mix(cycle);
    a.temperature_c += 0.6f * signed_unit(key);
    a.rain_mm_hour *= 1.0f + 0.08f * signed_unit(key + 1);
    a.light = std::clamp(a.light*(1.0f+0.03f*signed_unit(key+2)),0.0f,1.0f);
    a.wind *= 1.0f + 0.05f * signed_unit(key + 3);
    return a;
}
// Gregorian civil date conversion; no locale or host timezone dependence.
int64_t civil_days(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y-399) / 400;
    const unsigned yoe = unsigned(y-era*400);
    const unsigned doy = (153*(m+(m>2 ? -3 : 9))+2)/5+d-1;
    return int64_t(era)*146097+yoe*365+yoe/4-yoe/100+doy-719468;
}
int64_t timestamp(const std::string& s) {
    if (s.size()!=16 || s[4]!='-' || s[7]!='-' || s[10]!='T' || s[13]!=':' || s.substr(14)!="00")
        throw std::runtime_error("Weather timestamps must be YYYY-MM-DDTHH:00 in UTC");
    for (std::size_t i=0;i<s.size();++i)
        if (i!=4 && i!=7 && i!=10 && i!=13 && (s[i]<'0'||s[i]>'9'))
            throw std::runtime_error("Invalid weather timestamp");
    const int y=std::stoi(s.substr(0,4)), m=std::stoi(s.substr(5,2));
    const int d=std::stoi(s.substr(8,2)), h=std::stoi(s.substr(11,2));
    const bool leap = y%4==0 && (y%100!=0 || y%400==0);
    constexpr int mdays[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if (y<1940 || m<1 || m>12 || d<1 || d>mdays[m-1]+(m==2&&leap) || h>23)
        throw std::runtime_error("Invalid weather calendar date");
    return civil_days(y,unsigned(m),unsigned(d))*24+h;
}
float number(const std::string& s, float lo, float hi) {
    std::size_t n=0;
    const float v=std::stof(s,&n);
    if (n!=s.size() || !std::isfinite(v) || v<lo || v>hi)
        throw std::runtime_error("Missing or out-of-range weather observation");
    return v;
}
}

WeatherTape::WeatherTape() {
    metadata_="SYNTHETIC DIAGNOSTIC ONLY; not historical weather";
    hours_.reserve(24*123);
    for (int i=0;i<24*123;++i) {
        const int h=i%24, day=i/24;
        const float solar=std::max(0.0f,std::sin(float(h-5)*3.14159265358979323846f/16.0f));
        const float t=17.0f+4.0f*std::sin(float(h-8)*3.14159265358979323846f/12.0f);
        const float rain=(day%7==2 && h>=9 && h<=13) ? 0.8f : 0.0f;
        hours_.push_back({int16_t(std::lround(t*10)),uint16_t(std::lround(rain*100)),
                          uint16_t(std::lround(solar*650)),220});
    }
    update_fingerprint();
}

void WeatherTape::load_csv(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open weather tape: "+path);
    std::vector<PackedHour> hours;
    std::string metadata, line;
    bool header=false, source=false;
    int64_t previous=0;
    while (std::getline(input,line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0]=='#') {
            metadata += line.substr(1)+"\n";
            if (line.find("kind=historical_reanalysis")!=std::string::npos) source=true;
            continue;
        }
        if (!header) {
            if (line!="time_utc,temperature_c,rain_mm_hour,solar_wm2,wind_ms")
                throw std::runtime_error("Unrecognized weather CSV columns");
            header=true; continue;
        }
        std::array<std::string,5> field;
        std::istringstream row(line);
        for (auto& f:field) if (!std::getline(row,f,',')) throw std::runtime_error("Missing weather field");
        std::string extra;
        if (std::getline(row,extra,',')) throw std::runtime_error("Extra weather field");
        const int64_t current=timestamp(field[0]);
        if (!hours.empty() && current!=previous+1)
            throw std::runtime_error("Weather observations are not complete consecutive UTC hours");
        previous=current;
        hours.push_back({int16_t(std::lround(number(field[1],-60,60)*10)),
                         uint16_t(std::lround(number(field[2],0,500)*100)),
                         uint16_t(std::lround(number(field[3],0,1500))),
                         uint16_t(std::lround(number(field[4],0,150)*100))});
    }
    if (!source || hours.size()<24*120 || hours.size()>24*184 || hours.size()%24!=0)
        throw std::runtime_error("Historical tape needs source metadata and 120-184 complete days");
    hours_=std::move(hours);
    metadata_=std::move(metadata);
    historical_=true;
    update_fingerprint();
}

uint64_t WeatherTape::period_seconds() const { return (hours_.size()-seam_hours_)*3600ULL; }

WeatherSample WeatherTape::raw_sample(uint64_t seconds) const {
    const auto i=std::size_t(seconds/3600)%hours_.size();
    const auto next=(i+1)%hours_.size();
    const float fraction=float(seconds%3600)/3600.0f;
    const auto unpack=[](const PackedHour& h) {
        return WeatherSample{h.temperature_decic*0.1f,h.rain_centimm*0.01f,
                             std::min(1.0f,h.solar_wm2/800.0f),h.wind_centims*0.01f};
    };
    const auto a=unpack(hours_[i]), b=unpack(hours_[next]);
    auto result=blend(a,b,fraction);
    // Rain represents this complete hour's accumulated amount, held as its rate.
    // Interpolating precipitation would leak rain into preceding dry hours.
    result.rain_mm_hour=a.rain_mm_hour;
    return result;
}

WeatherSample WeatherTape::sample(uint64_t sim_seconds, uint64_t seed) const {
    const uint64_t period=period_seconds(), seam=seam_hours_*3600ULL;
    const uint64_t cycle=sim_seconds/period, phase=sim_seconds%period;
    const auto current=vary(raw_sample(phase+seam),seed,cycle);
    if (phase<period-seam) return current;
    const uint64_t offset=phase-(period-seam);
    float weight=float(offset)/float(seam);
    weight=weight*weight*(3.0f-2.0f*weight);
    return blend(current,vary(raw_sample(offset),seed,cycle+1),weight);
}

void WeatherTape::update_fingerprint() {
    uint64_t h=14695981039346656037ULL;
    const auto byte=[&h](uint8_t b){ h=(h^b)*1099511628211ULL; };
    for (unsigned char c:metadata_) byte(c);
    for (unsigned shift=0;shift<32;shift+=8) byte(uint8_t(seam_hours_>>shift));
    for (const auto& p:hours_) {
        const uint16_t fields[]={uint16_t(p.temperature_decic),p.rain_centimm,p.solar_wm2,p.wind_centims};
        for (uint16_t v:fields) { byte(uint8_t(v)); byte(uint8_t(v>>8)); }
    }
    fingerprint_=h;
}

} // namespace antfarm
