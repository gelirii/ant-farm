#include "antfarm/render.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

// Compositor test, not an ecology/colony-longevity test. Artificial state edits
// below deliberately exercise invalidation and object removal in isolation.
int main(int argc,char** argv) {try {
    using namespace antfarm;
    using Clock=std::chrono::steady_clock;
    World world;world.advance(180);
    Renderer cached;std::vector<uint16_t> pixels,reference;
    auto begin=Clock::now();cached.render(world,pixels);
    const double cold_ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
    begin=Clock::now();size_t restored=0;
    for(int i=0;i<500;++i){cached.render(world,pixels,1080,1920,float(i%10)/10);restored+=cached.last_restored_pixels();}
    const double warm_ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count()/500;
    const size_t render_bytes=cached.memory_bytes(),average_restored=restored/500;
    unsigned comparisons=0;
    auto compare=[&](int width,int height,float alpha){
        cached.render(world,pixels,width,height,alpha);Renderer fresh;fresh.render(world,reference,width,height,alpha);
        if(pixels!=reference)throw std::runtime_error("cached frame differs from full render at comparison "+std::to_string(comparisons));
        ++comparisons;
    };
    for(int i=0;i<12;++i){world.advance(1);compare(1080,1920,float(i%10)/10);}
    // Cells affect their own fill, four neighbours' exposed edges, and roots.
    ++world.seconds;world.cells[world.index(60,Surface+5)].soil=0;compare(1080,1920,1);
    ++world.seconds;world.cells[world.index(61,Surface+5)].soil=0;compare(1080,1920,1);
    ++world.seconds;world.cells[world.index(60,Surface+5)].soil=SoilFull;compare(1080,1920,1);
    ++world.seconds;world.cells[world.index(61,Surface+5)].moisture=250;compare(1080,1920,1);
    if(!world.plants.empty()){world.plants.front().biomass=1;compare(1080,1920,1);world.plants.erase(world.plants.begin());compare(1080,1920,1);}
    world.corpses.push_back({60,Surface-1,0,99,24,false});compare(1080,1920,1);
    world.corpses.clear();compare(1080,1920,1);
    if(!world.ants.empty()){world.ants.front().alive=false;compare(1080,1920,1);}
    world.seconds=Day/2;compare(1080,1920,1);world.seconds=Day-1;compare(1080,1920,1);
    compare(540,960,1);compare(361,641,1);compare(1080,1920,1);
    std::vector<uint16_t> replacement;cached.render(world,replacement);Renderer fresh;fresh.render(world,reference);
    if(replacement!=reference)throw std::runtime_error("replacement output buffer was not fully restored");
    ++comparisons;
    if(argc>1&&!write_ppm(argv[1],reference))throw std::runtime_error("could not write requested test frame");
    std::cout<<"{\"cache_comparisons\":"<<comparisons<<",\"all_equal\":true,\"cold_ms\":"<<cold_ms
             <<",\"warm_mean_ms\":"<<warm_ms<<",\"average_restored_pixels\":"<<average_restored
             <<",\"framebuffer_pixels\":2073600,\"renderer_memory_bytes\":"<<render_bytes
             <<",\"timing_scope\":\"host software only; fixed 180-second world, 500 warm frames\"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"render_check: "<<e.what()<<'\n';return 1;}}
