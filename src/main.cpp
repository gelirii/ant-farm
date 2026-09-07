#include "antfarm/core.hpp"
#include "antfarm/render.hpp"
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

using namespace antfarm;
namespace {
volatile std::sig_atomic_t running=1;
void stop(int){running=0;}
struct Options {
 std::string command="run",weather="data/weather/newcastle_2023_may_aug.csv",output="out/frame.ppm",save,load,csv;
 double days=30;uint64_t seed=42;unsigned founders=30,max_workers=512,interval_hours=24;
 float nectar=1,insects=0.25f,rain=1;int width=1080,height=1920;
 bool stop_on_queen_loss=false,days_given=false;
};
Options parse(int argc,char**argv){
 Options o;if(argc>1)o.command=argv[1];
 for(int i=2;i<argc;++i){std::string a=argv[i];if(i+1>=argc)throw std::runtime_error("missing value for "+a);std::string v=argv[++i];
 if(a=="--weather")o.weather=v;else if(a=="--output")o.output=v;else if(a=="--save")o.save=v;else if(a=="--load")o.load=v;else if(a=="--csv")o.csv=v;
 else if(a=="--days"){o.days=std::stod(v);o.days_given=true;}else if(a=="--seed")o.seed=std::stoull(v);else if(a=="--founders")o.founders=unsigned(std::stoul(v));
 else if(a=="--max-workers")o.max_workers=unsigned(std::stoul(v));else if(a=="--interval-hours")o.interval_hours=unsigned(std::stoul(v));
 else if(a=="--nectar")o.nectar=std::stof(v);else if(a=="--insects")o.insects=std::stof(v);else if(a=="--rain")o.rain=std::stof(v);
 else if(a=="--width")o.width=std::stoi(v);else if(a=="--height")o.height=std::stoi(v);
 else if(a=="--stop-on-queen-loss")o.stop_on_queen_loss=std::stoi(v)!=0;
 else throw std::runtime_error("unknown option "+a);}
 if(!o.days_given&&(o.command=="render"||o.command=="power-cycle"))o.days=0;
 if(!std::isfinite(o.days)||o.days<0||o.days>40000||!o.interval_hours||o.width<180||o.height<320||o.width>2160||o.height>3840)throw std::runtime_error("invalid duration, interval, or display size");
 if(!std::isfinite(o.nectar)||!std::isfinite(o.insects)||!std::isfinite(o.rain)||o.nectar<0||o.insects<0||o.rain<0)throw std::runtime_error("invalid resource/weather multiplier");
 if(o.founders>4096||o.max_workers>4095)throw std::runtime_error("too many ants");
 return o;
}
void header(std::ostream&out){out<<"day,workers,brood,queen,extinct,plants,corpses,food_sites,sugar,protein,births,deaths,soil_moved,minerals,temperature_c,rain_mm_h,worker_actions\n";}
void row(std::ostream&out,const World&w){auto s=w.snapshot();out<<std::fixed<<std::setprecision(4)<<double(w.seconds)/Day<<','<<s.workers<<','<<s.brood<<','<<s.queen_alive<<','<<s.extinct<<','<<s.plants<<','<<s.corpses<<','<<s.food_sites<<','<<s.sugar<<','<<s.protein<<','<<s.births<<','<<s.deaths<<','<<s.excavated<<','<<s.mineral<<','<<w.conditions.temperature_c<<','<<w.conditions.rain_mm_hour<<','<<w.stats.worker_actions<<'\n';}
void summary(const World&w,double elapsed){auto s=w.snapshot();std::cout<<"{\"day\":"<<double(w.seconds)/Day<<",\"seed\":"<<w.config.seed<<",\"founders\":"<<w.config.founders<<",\"workers\":"<<s.workers<<",\"brood\":"<<s.brood<<",\"queen\":"<<(s.queen_alive?"true":"false")<<",\"extinct\":"<<(s.extinct?"true":"false")<<",\"births\":"<<s.births<<",\"deaths\":"<<s.deaths<<",\"plants\":"<<s.plants<<",\"soil_moved\":"<<s.excavated<<",\"pickups\":"<<w.stats.pickups<<",\"exchanges\":"<<w.stats.exchanges<<",\"actions\":"<<w.stats.worker_actions<<",\"memory_bytes\":"<<w.memory_bytes()<<",\"wall_seconds\":"<<elapsed<<",\"historical_weather\":"<<(w.weather.historical()?"true":"false")<<",\"hash\":"<<w.state_hash()<<"}\n";}
}
int main(int argc,char**argv){try{
 if(argc<2||std::string(argv[1])=="--help"){std::cout<<"Ant Farm — portable core and quiet RGB565 renderer\nCommands: run, render, live, power-cycle, benchmark\nOptions: --days N --seed N --founders N --weather CSV --csv CSV --save FILE --load FILE --output PPM --width N --height N --nectar N --insects N --rain N\n";return 0;}
 Options o=parse(argc,argv);Config c;c.seed=o.seed;c.founders=uint16_t(o.founders);c.max_workers=uint16_t(o.max_workers);c.nectar_scale=o.nectar;c.insect_scale=o.insects;c.rain_scale=o.rain;
 World w(c);if(o.weather!="synthetic")w.weather.load_csv(o.weather);w.conditions=w.weather.sample(w.seconds,c.seed);
 std::string err;if(!o.load.empty()&&!w.load(o.load,&err))throw std::runtime_error("load: "+err);
 std::ofstream csv;if(!o.csv.empty()){csv.open(o.csv);if(!csv)throw std::runtime_error("cannot open CSV");header(csv);row(csv,w);}
 auto start=std::chrono::steady_clock::now();
 if(o.command=="live"){
   std::signal(SIGINT,stop);std::signal(SIGTERM,stop);Renderer renderer;std::vector<uint16_t> pixels;
   auto epoch=std::chrono::steady_clock::now();uint64_t base=w.seconds,checkpoint=w.seconds;
   while(running){auto now=std::chrono::steady_clock::now();double elapsed=std::chrono::duration<double>(now-epoch).count();uint64_t target=base+uint64_t(elapsed);
     if(target>w.seconds)w.advance(target-w.seconds);
     renderer.render(w,pixels,o.width,o.height,float(elapsed-std::floor(elapsed)));
     if(!write_ppm(o.output+".tmp",pixels,o.width,o.height)||std::rename((o.output+".tmp").c_str(),o.output.c_str()))throw std::runtime_error("frame write failed");
     if(!o.save.empty()&&w.seconds-checkpoint>=60){if(!w.save(o.save,&err))throw std::runtime_error(err);checkpoint=w.seconds;}
     std::this_thread::sleep_until(now+std::chrono::milliseconds(100));
   }
   if(!o.save.empty()&&!w.save(o.save,&err))throw std::runtime_error(err);
 }else{
   if(o.command=="power-cycle"){w.power_off();w.power_on();}
   else if(o.command!="run"&&o.command!="render"&&o.command!="benchmark")throw std::runtime_error("unknown command");
   uint64_t remain=uint64_t(o.days*Day);while(remain){uint64_t n=std::min<uint64_t>(remain,uint64_t(o.interval_hours)*3600);w.advance(n);remain-=n;
     if(!w.validate(&err))throw std::runtime_error("invariant at day "+std::to_string(double(w.seconds)/Day)+": "+err);
     if(csv){row(csv,w);csv.flush();}
     if(o.stop_on_queen_loss&&!w.queen())break;
   }
   if(o.command=="render"||o.command=="power-cycle"){std::vector<uint16_t>pixels;render(w,pixels,o.width,o.height);if(!write_ppm(o.output,pixels,o.width,o.height))throw std::runtime_error("frame write failed");}
   if(!o.save.empty()&&!w.save(o.save,&err))throw std::runtime_error("save: "+err);
   summary(w,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
 }
 return 0;
 }catch(const std::exception&e){std::cerr<<"antfarm: "<<e.what()<<'\n';return 1;}}
