#include "antfarm/weather.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace antfarm;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
    for(int year:{2019,2021,2023}){
        WeatherTape tape;tape.load_csv("data/weather/newcastle_"+std::to_string(year)+"_may_aug.csv");
        check(tape.historical()&&tape.size()==2952,"Incomplete historical tape");
        check(tape.period_seconds()==118ULL*86400,"Incorrect overlap period");
        for(uint64_t second=0;second<3*tape.period_seconds();second+=3600){
            auto a=tape.sample(second,42),b=tape.sample(second,42);
            check(a.temperature_c==b.temperature_c&&a.rain_mm_hour==b.rain_mm_hour,"Weather replay changed");
            check(std::isfinite(a.temperature_c)&&a.rain_mm_hour>=0&&a.light>=0&&a.light<=1&&a.wind>=0,"Invalid weather sample");
        }
        // Rain is held per hour; discontinuities at observed hourly rate changes
        // are intentional. Test continuous fields at both seam endpoints.
        for(uint64_t cycle=1;cycle<=3;++cycle)for(uint64_t edge:{cycle*tape.period_seconds(),cycle*tape.period_seconds()-5*86400}){
            auto a=tape.sample(edge-1,42),b=tape.sample(edge,42);
            check(std::abs(a.temperature_c-b.temperature_c)<.01f&&std::abs(a.light-b.light)<.01f&&std::abs(a.wind-b.wind)<.01f,"Crossfade discontinuity");
        }
        auto old=tape.fingerprint();bool rejected=false;
        try{tape.load_csv("data/weather/nonexistent.csv");}catch(const std::exception&){rejected=true;}
        check(rejected&&old==tape.fingerprint(),"Failed weather load was not transactional");
    }
    std::cout<<"PASS three historical tapes, deterministic playback, range checks and five-day seams\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
