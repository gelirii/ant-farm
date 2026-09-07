#include "antfarm/core.hpp"
#include <iostream>
#include <map>
using namespace antfarm;
int main(int argc,char**argv){try{World w;if(argc>2)w.weather.load_csv(argv[2]);std::string e;if(argc<2||!w.load(argv[1],&e)){std::cerr<<e;return 1;}
 std::map<int,int> tasks;std::cout<<"day "<<w.seconds/86400.<<"\n";
 for(const auto&a:w.ants)if(a.alive){++tasks[int(a.task)];std::cout<<(a.queen?"queen ":"ant ")<<a.id<<" @"<<a.x<<","<<a.y<<" crop="<<a.crop<<" protein="<<a.protein<<" task="<<task_name(a.task)<<" patience="<<a.patience<<" home="<<w.cells[w.index(a.x,a.y)].home<<" soil="<<int(w.cells[w.index(a.x,a.y)].soil)<<" moves="<<a.moves<<"\n";}
 for(const auto&b:w.brood)std::cout<<"brood "<<b.id<<" age="<<(w.seconds-b.born)/86400.<<" @"<<b.x<<","<<b.y<<" stage="<<int(b.stage)<<" dev="<<b.development<<" fed="<<b.fed<<" stress="<<int(b.stress)<<" soil="<<int(w.cells[w.index(b.x,b.y)].soil)<<" moisture="<<int(w.cells[w.index(b.x,b.y)].moisture)<<"\n";
 std::cout<<"eggs="<<w.stats.eggs_laid<<" brooddeaths="<<w.stats.brood_deaths<<" births="<<w.stats.births<<"\n";
}catch(const std::exception&e){std::cerr<<e.what();return 1;}}
