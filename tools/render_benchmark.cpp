#include "antfarm/render.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
using namespace antfarm;
int main(){
    // Deliberately dense renderer fixture. This is NOT a biological trajectory.
    Config cfg;cfg.founders=513;World world(cfg);
    for(size_t i=0;i<world.ants.size();++i){auto& a=world.ants[i];
        a.x=int16_t(4+i*67%(Width-8));a.y=int16_t(Surface+4+i*41%(Height-Surface-8));
        a.previous_x=a.x-1;a.previous_y=a.y;a.last_move=0;
        world.cells[world.index(a.x,a.y)].soil=0;
    }
    for(int i=0;i<1024;++i){Brood b;b.x=int16_t(4+i*29%(Width-8));b.y=int16_t(Surface+4+i*37%(Height-Surface-8));world.brood.push_back(b);}
    for(int i=0;i<128;++i){Corpse c;c.x=int16_t(4+i%(Width-8));c.y=Surface-1;world.corpses.push_back(c);
        Food f;f.x=c.x;f.y=Surface-1;f.protein=300;world.foods.push_back(f);}
    using Clock=std::chrono::steady_clock;
    Renderer r;std::vector<uint16_t> pixels;auto begin=Clock::now();r.render(world,pixels);
    double cold=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
    std::vector<double> ms;size_t restored=0;
    for(int i=0;i<300;++i){begin=Clock::now();r.render(world,pixels,1080,1920,float(i%10)/10);
        ms.push_back(std::chrono::duration<double,std::milli>(Clock::now()-begin).count());restored+=r.last_restored_pixels();}
    double sum=0;for(double x:ms)sum+=x;std::sort(ms.begin(),ms.end());
    std::cout<<"{\"scope\":\"dense host renderer fixture, not a living colony or P4 benchmark\",\"workers\":512,\"brood\":1024,\"corpses\":128,\"food_sites\":128,\"frames\":300,\"cold_ms\":"<<cold
      <<",\"warm_mean_ms\":"<<sum/ms.size()<<",\"warm_p95_ms\":"<<ms[284]<<",\"mean_restored_pixels\":"<<restored/300
      <<",\"renderer_bytes\":"<<r.memory_bytes()<<",\"output_bytes\":"<<pixels.capacity()*sizeof(uint16_t)<<"}\n";
}
