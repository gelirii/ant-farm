#include "antfarm/core.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace antfarm {
namespace {
constexpr int DX[8]={1,1,0,-1,-1,-1,0,1};
constexpr int DY[8]={0,1,1,1,0,-1,-1,-1};
constexpr uint16_t CropCapacity=480, ProteinCapacity=120;
uint16_t sat_add(uint16_t a,unsigned b){return uint16_t(std::min(65535u,unsigned(a)+b));}
}

World::World(Config cfg):config(cfg),cells(CellCount),occupancy_(CellCount,-1),
 scratch_home_(CellCount),scratch_trail_(CellCount),walkable_cache_(CellCount) {
    if(config.founders<2||config.founders>config.max_workers+1||config.max_workers>4095)
        throw std::invalid_argument("founders must be 2..max_workers+1; max_workers <=4095");
    rng.state=config.seed?config.seed:1;
    ants.reserve(config.max_workers+1); brood.reserve(config.max_workers*2);
    corpses.reserve(config.max_workers); plants.reserve(32); foods.reserve(128);
    next_occupant_.reserve(config.max_workers+1);
    for(auto& b:wheel_)b.reserve(4);
    init_soil();
    for(int x=0;x<Width;++x)seed_bank[x]=uint16_t(1+rng.bounded(4));
    // The colony arrives in a living patch of meadow, not an empty food dispenser.
    for(int i=0;i<12;++i){
        Plant p; p.x=int16_t(5+i*14+int(rng.bounded(7)));p.ground_y=ground[p.x];
        p.lifespan_days=uint16_t(56+rng.bounded(15));p.kind=uint8_t(i%4);
        p.biomass=.35f+.5f*rng.unit();p.water=.8f;p.sugar=120;
        // Initial plants have a remaining lifespan; born=0 avoids unsigned prehistory.
        p.lifespan_days=uint16_t(28+rng.bounded(35));
        plants.push_back(p);
    }
    conditions=weather.sample(0,config.seed);
    found_colony();stats.mineral_baseline=mineral_total();
}
void World::init_soil(){
    for(int x=0;x<Width;++x){
        ground[x]=int16_t(Surface+int(2*std::sin(x*.047f)));
        for(int y=0;y<Height;++y){auto& c=cells[index(x,y)];c=Cell{};
            c.soil=y>=ground[x]?SoilFull:0;c.moisture=uint8_t(std::min(165,65+std::max(0,y-Surface)/2));}
    }
    topology_dirty_=true;
}
void World::found_colony(){
    ants.clear();brood.clear();corpses.clear();
    for(auto& b:wheel_)b.clear();
    const int total=config.founders;
    for(int i=0;i<total;++i){
        Ant a;a.id=next_id++;a.x=0;a.y=ground[0]-1;a.previous_x=a.x;a.previous_y=a.y;
        a.queen=i==0;a.crop=a.queen?960:360;a.protein=a.queen?240:24;
        a.born=uint32_t(seconds);a.lifespan=a.queen?uint32_t((15+rng.bounded(11))*365*Day):uint32_t((160+rng.bounded(141))*Day);
        a.task=Task::Arrive;a.next_action=seconds+1+uint64_t(a.queen?total*4:i*4);
        ants.push_back(a);
    }
    extinct=false;stats.extinction_second=0;rebuild_transient();
}
unsigned World::worker_count()const{unsigned n=0;for(const auto&a:ants)if(a.alive&&!a.queen)++n;return n;}
const Ant* World::queen()const{for(const auto&a:ants)if(a.queen&&a.alive)return &a;return nullptr;}
Ant* World::queen(){for(auto&a:ants)if(a.queen&&a.alive)return &a;return nullptr;}
bool World::walkable(int x,int y)const{
    if(!open(x,y))return false;
    // The thin formicarium supports ants on exposed tunnel walls. Above ground
    // there must be material immediately beneath, so they cannot walk in the sky.
    if(y>=ground[x]-1)return true;
    // Ants can climb the exposed sides of a spoil pile, not only its top.
    // Contact with a solid neighbour still prevents walking through open sky.
    for(int k=0;k<8;++k)if(in_bounds(x+DX[k],y+DY[k])&&cells[index(x+DX[k],y+DY[k])].soil)return true;
    return false;
}
void World::set_soil(int cell,uint8_t amount){
    bool changed=bool(cells[cell].soil)!=bool(amount);cells[cell].soil=amount;
    if(!changed)return;
    int x=cell%Width,y=cell/Width;
    for(int yy=std::max(0,y-1);yy<=std::min(Height-1,y+1);++yy)
        for(int xx=std::max(0,x-1);xx<=std::min(Width-1,x+1);++xx)
            walkable_cache_[index(xx,yy)]=uint8_t(walkable(xx,yy));
    topology_dirty_=true;
}
void World::rebuild_occupancy(){
    std::fill(occupancy_.begin(),occupancy_.end(),-1);next_occupant_.assign(ants.size(),-1);
    for(int i=int(ants.size())-1;i>=0;--i){const auto&a=ants[i];if(!a.alive||!in_bounds(a.x,a.y))continue;
        int c=index(a.x,a.y);next_occupant_[i]=occupancy_[c];occupancy_[c]=int(i);}
}
void World::rebuild_transient(){
    for(auto& b:wheel_)b.clear();
    for(unsigned i=0;i<ants.size();++i)if(ants[i].alive){
        if(ants[i].next_action<=seconds)ants[i].next_action=seconds+1;
        wheel_[ants[i].next_action%WheelSize].push_back(uint16_t(i));}
    rebuild_occupancy();topology_dirty_=true;
    for(int y=0;y<Height;++y)for(int x=0;x<Width;++x)walkable_cache_[index(x,y)]=uint8_t(walkable(x,y));
    next_minute_=(seconds/60+1)*60;next_hour_=(seconds/3600+1)*3600;
}
void World::schedule(unsigned i,uint32_t delay){
    ants[i].next_action=seconds+std::max(1u,delay);
    wheel_[ants[i].next_action%WheelSize].push_back(uint16_t(i));
}
void World::move_ant(Ant&a,int x,int y){
    unsigned ai=unsigned(&a-ants.data());int old=index(a.x,a.y);
    int32_t* link=&occupancy_[old];
    while(*link>=0&&unsigned(*link)!=ai)link=&next_occupant_[*link];
    if(*link>=0)*link=next_occupant_[ai];
    a.previous_x=a.x;a.previous_y=a.y;
    a.last_move=uint32_t(seconds);
    int dx=x-a.x,dy=y-a.y;for(unsigned k=0;k<8;++k)if(DX[k]==dx&&DY[k]==dy)a.heading=uint8_t(k);
    a.x=int16_t(x);a.y=int16_t(y);int cell=index(x,y);
    int32_t* insertion=&occupancy_[cell];
    while(*insertion>=0&&unsigned(*insertion)<ai)insertion=&next_occupant_[*insertion];
    next_occupant_[ai]=*insertion;*insertion=int(ai);
    ++a.moves;++stats.moves;
}
bool World::step(Ant&a,bool homeward,bool exploring){
    int best=-1;float best_score=-1e20f;
    const Cell& here=cells[index(a.x,a.y)];
    for(int k=0;k<8;++k){
        int x=a.x+DX[k],y=a.y+DY[k];if(!can_walk(x,y))continue;
        // No diagonal passage through two solid corner cells.
        if(DX[k]&&DY[k]&&!open(a.x+DX[k],a.y)&&!open(a.x,a.y+DY[k]))continue;
        const auto& c=cells[index(x,y)];
        float score=rng.unit()*180.f;
        int turn=std::abs(k-int(a.heading));turn=std::min(turn,8-turn);
        score+=float(4-turn)*(exploring?55.f:22.f);
        if(homeward){
            score+=(int(c.home)-int(here.home))*.7f+c.home*.003f;
            // An innate home vector, updated only by physical nest contact.
            // This supplies orientation outside scent range, never a route/map.
            if(here.home<1500){int before=std::abs(a.x-a.nest_x)+std::abs(a.y-a.nest_y);
                int after=std::abs(x-a.nest_x)+std::abs(y-a.nest_y);score+=(before-after)*190.f;}
        }
        else if(a.task==Task::Forage&&a.patience<180)score+=(int(c.trail)-int(here.trail))*.3f;
        // A unsuccessful search eventually leaves an old scent peak. Immediate
        // reversal is possible at a dead end, but not the preferred open route.
        if(x==a.previous_x&&y==a.previous_y)score-=250.f;
        if(a.task==Task::ReturnSoil){
            if(a.y>=ground[a.x])score-=DY[k]*80.f;
            else score+=(std::abs(x-a.nest_x)-std::abs(a.x-a.nest_x))*120.f;
            score-=c.home*.006f;
        }
        if(a.task==Task::RemoveCorpse){score-=DY[k]*90.f;score-=c.home*.008f;}
        if(a.task==Task::Forage&&a.y>=ground[a.x])score-=DY[k]*65.f;
        if(c.moisture>220)score-=(c.moisture-220)*12.f;
        unsigned crowd=0;for(int o=occupancy_[index(x,y)];o>=0&&crowd<8;o=next_occupant_[o])if(ants[o].alive)++crowd;
        score-=crowd*15.f;
        if(score>best_score){best_score=score;best=k;}
    }
    if(best<0)return false;
    move_ant(a,a.x+DX[best],a.y+DY[best]);return true;
}
bool World::take_food(Ant&a){
    for(auto& f:foods){if(std::abs(f.x-a.x)>1||std::abs(f.y-a.y)>2)continue;
        unsigned sugar=std::min<unsigned>(f.sugar,CropCapacity-std::min(CropCapacity,a.crop));
        unsigned protein=std::min<unsigned>(f.protein,ProteinCapacity-std::min(ProteinCapacity,a.protein));
        if(!sugar&&!protein)continue;
        a.crop=uint16_t(a.crop+sugar);a.protein=uint16_t(a.protein+protein);
        f.sugar=uint16_t(f.sugar-sugar);f.protein=uint16_t(f.protein-protein);
        a.task=Task::ReturnFood;a.patience=0;++stats.pickups;
        return true;
    }
    return false;
}
void World::share_food(Ant&a){
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
        int x=a.x+dx,y=a.y+dy;if(!in_bounds(x,y))continue;
        for(int j=occupancy_[index(x,y)];j>=0;j=next_occupant_[j]){
            auto& b=ants[j];if(!b.alive||b.id==a.id)continue;
            if(b.queen){a.nest_x=b.x;a.nest_y=b.y;}
            // Share reserves in proportion to metabolic need. A queen cannot
            // demand a full crop from a worker whose own reserve is running low.
            int need=b.queen?std::min(720,int(a.crop)*2)-int(b.crop):int(a.crop)-int(b.crop);
            unsigned give=need>24&&a.crop>48?std::min<unsigned>(32,std::min<int>(a.crop-48,need/2)):0;
            if(give){a.crop=uint16_t(a.crop-give);b.crop=sat_add(b.crop,give);++stats.exchanges;a.food_delivered+=give;}
            if(a.protein>12&&(b.queen||b.task==Task::Nurse)&&b.protein+12<a.protein){
                unsigned p=std::min<unsigned>(12,a.protein/3);a.protein-=uint16_t(p);b.protein+=uint16_t(p);++stats.exchanges;}
        }
    }
    if(a.protein){
        // Local brood triage: feed the most developed nearby larva first.
        // Spreading each small prey delivery across every larva previously left
        // all of them below survival intake, wasting nearly a whole cohort.
        Brood* recipient=nullptr;
        for(auto& b:brood)if(b.stage==Stage::Larva&&std::abs(b.x-a.x)<=2&&std::abs(b.y-a.y)<=2&&b.fed<48)
            if(!recipient||b.development>recipient->development||(b.development==recipient->development&&b.id<recipient->id))recipient=&b;
        if(recipient){unsigned give=std::min<unsigned>(8,a.protein);a.protein-=uint16_t(give);recipient->fed+=uint16_t(give);++stats.exchanges;}
    }
}
bool World::dig(Ant&a){
    if(a.soil_cargo)return false;
    // Surface excavation begins where nest scent is concentrated. A worker
    // outside the nest must first return, rather than strip-mining the meadow.
    if(a.y<ground[a.x]+1&&std::abs(a.x-Width/2)>2)return false;
    unsigned space=0,crowd=0;
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){int x=a.x+dx,y=a.y+dy;
        if(!in_bounds(x,y))continue;
        space+=open(x,y);
        for(int o=occupancy_[index(x,y)];o>=0;o=next_occupant_[o])crowd+=ants[o].alive;
    }
    if(a.y>=Surface+6&&space>10&&crowd<3&&rng.bounded(200)!=0)return false;
    int target=-1;float best=-1e20f;
    for(int k=0;k<8;++k){int x=a.x+DX[k],y=a.y+DY[k];
        if(x<2||x>=Width-2||y<ground[x]||y>=Height-3)continue;
        auto&c=cells[index(x,y)];if(!c.soil||c.moisture>235)continue;
        // Local depth, brood scent and occupancy encourage expansion. No room plan.
        float score=rng.unit()*8+DY[k]*3;
        if(a.y<Surface+16)score+=DY[k]*18-std::abs(DX[k])*4;
        if(a.y>Surface+100)score-=DY[k]*8;
        score+=cells[index(a.x,a.y)].brood*.001f;
        if(c.moisture<45)score-=5;
        if(score>best){best=score;target=index(x,y);}
    }
    if(target<0)return false;
    unsigned amount=std::min<unsigned>(2,cells[target].soil);
    set_soil(target,uint8_t(cells[target].soil-amount));a.soil_cargo=uint8_t(amount);
    a.task=Task::ReturnSoil;a.patience=0;a.soil_moved+=amount;stats.excavated+=amount;
    if(!cells[target].soil)topology_dirty_=true;
    return true;
}
bool World::deposit(Ant&a){
    if(a.y>ground[a.x]+1)return false;
    // Carry spoil out, then put the very same parcels on a nearby exposed column.
    int choices[4]={a.x-2,a.x+2,a.x-3,a.x+3};
    int chosen=-1,level=-1;
    for(int x:choices)if(x>1&&x<Width-2){
        int y=ground[x];while(y>1&&cells[index(x,y-1)].soil)y--;
        if(std::abs(x-Width/2)<3)continue; // entrance traffic discourages dumping
        int ty=cells[index(x,y)].soil<SoilFull?y:y-1;
        // Loose spoil cannot form a vertical tower. Only raise the exposed
        // surface where both neighbouring columns support a one-cell slope.
        // Otherwise the carrier must walk farther to find a deposition site.
        if(!cells[index(x,ty)].soil&&(!cells[index(x-1,ty+1)].soil||!cells[index(x+1,ty+1)].soil))continue;
        bool occupied=false;
        for(int o=occupancy_[index(x,ty)];o>=0;o=next_occupant_[o])occupied|=ants[o].alive;
        for(const auto&b:brood)if(b.x==x&&b.y==ty){occupied=true;break;}
        if(!occupied&&y>level){level=y;chosen=x;}}
    if(chosen<0)return false;
    // Fill a partially occupied top cell before making the pile taller.
    int target=index(chosen,cells[index(chosen,level)].soil<SoilFull?level:level-1);auto& c=cells[target];
    unsigned n=std::min<unsigned>(a.soil_cargo,SoilFull-c.soil);set_soil(target,uint8_t(c.soil+n));a.soil_cargo-=uint8_t(n);stats.deposited+=n;
    if(!a.soil_cargo){a.task=Task::Nurse;a.patience=0;}
    topology_dirty_=true;return n>0;
}
void World::act(unsigned ai){
    Ant& a=ants[ai];if(!a.alive)return;++stats.worker_actions;
    if(cells[index(a.x,a.y)].soil){
        bool escaped=false;for(int k=0;k<8;++k)if(can_walk(a.x+DX[k],a.y+DY[k])){move_ant(a,a.x+DX[k],a.y+DY[k]);escaped=true;break;}
        if(!escaped){schedule(ai,120);return;}
    }
    if(a.task==Task::Arrive){
        int nx=std::min(Width/2,int(a.x)+1),ny=a.y;
        for(int dy=-1;dy<=1;++dy)if(can_walk(nx,a.y+dy)){ny=a.y+dy;break;}
        if(nx!=a.x&&can_walk(nx,ny))move_ant(a,nx,ny);
        const Ant* q=queen();
        if(a.x==Width/2&&(a.queen||!q||q->task!=Task::Arrive)){
            a.task=a.queen?Task::Queen:(ai%3==0?Task::Excavate:Task::Forage);
        }
        schedule(ai,2);return;
    }
    if(a.queen){
        // Prefer a sheltered, moderately humid adjacent site; no knowledge of a nest map.
        int best=-1;float score0=-999.f;
        for(int k=0;k<8;++k){int x=a.x+DX[k],y=a.y+DY[k];if(!can_walk(x,y))continue;
            float s=-std::abs(y-(Surface+25))*8-std::abs(int(cells[index(x,y)].moisture)-100)+rng.unit()*6;
            float current=-std::abs(int(a.y)-(Surface+25))*8-std::abs(int(cells[index(a.x,a.y)].moisture)-100);
            if(s>current+7&&s>score0){score0=s;best=k;}}
        if(best>=0)move_ant(a,a.x+DX[best],a.y+DY[best]);
        schedule(ai,120);return;
    }
    if(a.soil_cargo){a.task=Task::ReturnSoil;if(!deposit(a))step(a,false);schedule(ai,3);return;}
    if(a.task==Task::RemoveCorpse){
        if(a.payload<corpses.size()){
            auto&c=corpses[a.payload];c.x=a.x;c.y=a.y;
            if(a.y<ground[a.x]&&std::abs(a.x-Width/2)>18){c.carried=false;a.task=Task::Explore;}
            else step(a,false);
        }else a.task=Task::Explore;
        schedule(ai,4);return;
    }
    share_food(a);
    if(a.task==Task::ReturnFood){
        auto& c=cells[index(a.x,a.y)];c.trail=std::max<uint16_t>(c.trail,uint16_t(std::max(300,9000-int(a.patience)*24)));
        if(c.home>6500&&a.y>=ground[a.x]){a.task=Task::Nurse;a.patience=0;schedule(ai,15);return;}
        if(++a.patience>900){a.task=Task::Explore;a.patience=0;}
        step(a,true);schedule(ai,2);return;
    }
    if(a.crop<120&&a.task!=Task::Forage){a.task=Task::Forage;a.patience=0;}
    if(a.task==Task::Forage||a.task==Task::Explore){
        if(take_food(a)){schedule(ai,30);return;}
        if(a.y>=ground[a.x]&&a.crop>120&&rng.bounded(200)==0)a.task=Task::Excavate;
        else step(a,false,true);
        // A scout returns occasionally even without food; this prevents endless exile.
        if(++a.patience>500&&a.crop>120){a.task=Task::Nurse;a.patience=0;}
        schedule(ai,2);return;
    }
    if(a.task==Task::Excavate){
        if(dig(a)){schedule(ai,45);return;}
        step(a,true);
        if(++a.patience>120){a.task=Task::Nurse;a.patience=0;}
        schedule(ai,4);return;
    }
    if(a.task==Task::Nurse){
        // Relocate a nearby stressed brood item one step towards a better local site.
        for(auto& b:brood)if(std::abs(b.x-a.x)<=1&&std::abs(b.y-a.y)<=1){
            int bi=index(b.x,b.y);int best=bi;int quality=std::abs(int(cells[bi].moisture)-100)-(b.y>=Surface?20:0)+(cells[bi].soil?1000:0)-cells[bi].home/400;
            for(int k=0;k<8;++k){int x=b.x+DX[k],y=b.y+DY[k];if(!can_walk(x,y))continue;
                int q=std::abs(int(cells[index(x,y)].moisture)-100)-(y>=Surface?20:0)-cells[index(x,y)].home/400;
                if(q<quality){best=index(x,y);quality=q;}}
            b.x=int16_t(best%Width);b.y=int16_t(best/Width);break;
        }
        for(unsigned ci=0;ci<corpses.size();++ci){auto&c=corpses[ci];
            if(!c.carried&&c.organic&&(c.y>=ground[c.x]||std::abs(c.x-Width/2)<18)&&seconds-c.died>3600&&std::abs(c.x-a.x)<=1&&std::abs(c.y-a.y)<=1){
                c.carried=true;a.payload=uint16_t(ci);a.task=Task::RemoveCorpse;schedule(ai,4);return;}}
        if(cells[index(a.x,a.y)].home<18000)step(a,true);
        else if(rng.bounded(6)==0)step(a,false);
        if(++a.patience>40){a.patience=0;
            uint32_t choice=rng.bounded(100);
            a.task=choice<25?Task::Excavate:(choice<60?Task::Forage:Task::Rest);}
        schedule(ai,20);return;
    }
    if(a.task==Task::Rest){
        if(++a.patience>24){a.task=Task::Nurse;a.patience=0;}
        schedule(ai,60);return;
    }
    a.task=Task::Explore;schedule(ai,2);
}

void World::chemistry(){
    if(topology_dirty_){
        walk_cells_.clear();for(int y=0;y<Height;++y)for(int x=0;x<Width;++x)if(can_walk(x,y))walk_cells_.push_back(uint32_t(index(x,y)));
        topology_dirty_=false;
    }
    for(uint32_t i:walk_cells_){int x=int(i%Width),y=int(i/Width);unsigned home=0,trail=0,n=0;
        for(int k=0;k<8;++k){int nx=x+DX[k],ny=y+DY[k];if(!can_walk(nx,ny))continue;
            const auto&c=cells[index(nx,ny)];home+=c.home;trail+=c.trail;++n;}
        // Stable local Jacobi diffusion; diffusion and decay operate only on air.
        const auto& c=cells[i];scratch_home_[i]=uint16_t((unsigned(c.home)*4+home)/(4+n)*995/1000);
        scratch_trail_[i]=uint16_t((unsigned(c.trail)*8+trail)/(8+n)*986/1000);
    }
    for(uint32_t i:walk_cells_){cells[i].home=scratch_home_[i];cells[i].trail=scratch_trail_[i];cells[i].brood=uint16_t(cells[i].brood*98/100);}
    if(const auto*q=queen()){cells[index(q->x,q->y)].home=24000;}
    // Nest entrance odour persists as a local source connected to the colony.
    if(queen())for(int x=Width/2-2;x<=Width/2+2;++x)for(int y=ground[x]-1;y<ground[x]+2;++y)if(open(x,y))cells[index(x,y)].home=std::max<uint16_t>(cells[index(x,y)].home,1200);
    for(const auto&b:brood)cells[index(b.x,b.y)].brood=12000;
    for(const auto&f:foods)if(f.sugar||f.protein)cells[index(f.x,f.y)].trail=std::max<uint16_t>(cells[index(f.x,f.y)].trail,1800);
}
void World::minute(){chemistry();}
void World::die(Ant&a){
    if(!a.alive)return;
    unsigned ai=unsigned(&a-ants.data());int32_t* link=&occupancy_[index(a.x,a.y)];
    while(*link>=0&&unsigned(*link)!=ai)link=&next_occupant_[*link];
    if(*link>=0)*link=next_occupant_[ai];
    next_occupant_[ai]=-1;
    a.alive=false;++stats.deaths;
    Corpse c;c.id=a.id;c.x=a.x;c.y=a.y;c.died=uint32_t(seconds);c.organic=uint16_t(24+a.crop/8+a.protein/4);corpses.push_back(c);
    if(a.soil_cargo){auto&cell=cells[index(a.x,a.y)];set_soil(index(a.x,a.y),uint8_t(cell.soil+a.soil_cargo));stats.deposited+=a.soil_cargo;a.soil_cargo=0;topology_dirty_=true;}
    if(a.task==Task::RemoveCorpse&&a.payload<corpses.size())corpses[a.payload].carried=false;
    a.crop=0;a.protein=0;
}
void World::metabolism(){
    for(auto&a:ants)if(a.alive){
        unsigned cost=a.queen?2:1;
        if(cells[index(a.x,a.y)].moisture>245)cost+=4;
        if(a.crop>=cost){a.crop-=uint16_t(cost);stats.sugar_metabolized+=cost;}
        else{die(a);continue;}
        if(seconds-a.born>=a.lifespan)die(a);
    }
    unsigned workers=worker_count();
    Ant*q=queen();
    if(q&&workers&&q->crop>200&&brood.size()<workers*3&&brood.size()<config.max_workers*2){
        // At most a few eggs/day in a young colony; nutrition throttles laying.
        unsigned lay_interval=std::max(3u,24u/(1+workers/20));
        if((seconds/3600)%lay_interval==0&&q->protein>=8){
            Brood b;b.id=next_id++;b.x=q->x;b.y=q->y;b.born=uint32_t(seconds);brood.push_back(b);
            q->protein-=8;q->crop-=4;stats.protein_used+=8;stats.sugar_metabolized+=4;++stats.eggs_laid;
        }
    }
    float underground_temp=conditions.temperature_c*.3f+17.f*.7f;
    float rate=std::clamp((underground_temp-8.f)/12.f,.05f,1.4f);
    for(size_t i=0;i<brood.size();){auto&b=brood[i];bool dead=false;
        auto&c=cells[index(b.x,b.y)];
        int stress_change=(c.soil||c.moisture>240||workers==0)?2:-1;
        if(b.stage==Stage::Larva){
            if(b.fed){--b.fed;++stats.protein_used;b.development+=rate;}
            else stress_change=std::max(1,stress_change);
        }else b.development+=rate;
        b.stress=uint8_t(std::clamp(int(b.stress)+stress_change,0,255));
        if(b.stress>=72||seconds-b.born>150*Day)dead=true;
        if(b.stage==Stage::Egg&&b.development>=240){b.stage=Stage::Larva;b.development=0;b.fed=8;}
        else if(b.stage==Stage::Larva&&b.development>=336){b.stage=Stage::Pupa;b.development=0;}
        else if(b.stage==Stage::Pupa&&b.development>=336&&workers<config.max_workers){
            Ant a;a.id=b.id;a.x=b.x;a.y=b.y;a.previous_x=a.x;a.previous_y=a.y;a.born=uint32_t(seconds);
            a.nest_x=b.x;a.nest_y=b.y;
            a.crop=48;a.protein=0;a.lifespan=uint32_t((180+rng.bounded(121))*Day);a.task=Task::Nurse;
            unsigned slot=1;while(slot<ants.size()&&ants[slot].alive)++slot;
            if(slot==ants.size()){ants.push_back(a);next_occupant_.push_back(-1);}else ants[slot]=a;
            int idx=index(a.x,a.y);int32_t* insertion=&occupancy_[idx];
            while(*insertion>=0&&unsigned(*insertion)<slot)insertion=&next_occupant_[*insertion];
            next_occupant_[slot]=*insertion;*insertion=int(slot);
            schedule(slot,1);++stats.births;++workers;
            brood[i]=brood.back();brood.pop_back();continue;
        }
        if(dead){Corpse corpse;corpse.id=b.id;corpse.x=b.x;corpse.y=b.y;corpse.died=uint32_t(seconds);corpse.organic=8;corpses.push_back(corpse);++stats.brood_deaths;
            brood[i]=brood.back();brood.pop_back();continue;}
        ++i;
    }
    if(!queen()&&workers==0&&brood.empty()&&!extinct){extinct=true;stats.extinction_second=uint32_t(seconds);}
}
void World::ecology(){
    const float rain=conditions.rain_mm_hour*config.rain_scale;
    for(int x=0;x<Width;++x){
        int sy=ground[x];int surface_m=cells[index(x,sy)].moisture;
        surface_m+=int(rain*9);surface_m-=int(1+conditions.light*2+std::max(0.f,conditions.temperature_c-20.f)*.15f);
        cells[index(x,sy)].moisture=uint8_t(std::clamp(surface_m,20,250));
        for(int y=sy+1;y<Height;++y){auto&c=cells[index(x,y)];int above=cells[index(x,y-1)].moisture;
            int target=std::max(50,above);int delta=(target-int(c.moisture))/12;
            c.moisture=uint8_t(std::clamp(int(c.moisture)+delta,0,255));}
    }
    auto add_food=[&](int x,unsigned sugar,unsigned protein,uint8_t kind){
        if(!sugar&&!protein)return;
        int top=1;while(top<Height-1&&!cells[index(x,top)].soil)++top;
        for(auto&f:foods)if(f.x==x&&f.kind==kind&&seconds-f.created<7*Day){f.sugar=sat_add(f.sugar,sugar);f.protein=sat_add(f.protein,protein);f.y=int16_t(top-1);return;}
        if(foods.size()<128){Food f;f.x=int16_t(x);f.y=int16_t(top-1);f.sugar=uint16_t(sugar);f.protein=uint16_t(protein);f.kind=kind;f.created=uint32_t(seconds);foods.push_back(f);}
    };
    for(auto&p:plants){
        if(p.alive){
            const int gy=ground[p.x];
            float root_moisture=(cells[index(p.x,gy)].moisture+cells[index(p.x,gy+6)].moisture+cells[index(p.x,gy+12)].moisture)/3.f;
            p.water=std::clamp(root_moisture/130.f,0.f,1.f);
            float growth=conditions.light*p.water*.008f;
            p.biomass=std::min(1.f,p.biomass+growth);
            if(p.biomass>.35f&&p.water>.2f){unsigned production=unsigned(60.f*conditions.light*p.water*p.biomass*config.nectar_scale);
                add_food(p.x,production,0,0);stats.sugar_input+=production;}
            if(seconds/Day%7==0&&seconds%Day==0&&p.biomass>.6f){
                int x=std::clamp(int(p.x)+int(rng.bounded(25))-12,0,Width-1);seed_bank[x]=uint16_t(std::min(200,int(seed_bank[x])+2));}
            if(seconds-p.born>=uint64_t(p.lifespan_days)*Day){p.alive=false;p.died=uint32_t(seconds);++stats.plant_deaths;}
        }else{
            p.biomass=std::max(0.f,p.biomass-1.f/(21.f*24));
            if(seconds%Day==0){auto&c=cells[index(p.x,ground[p.x])];c.nutrients=uint8_t(std::min(255,int(c.nutrients)+1));}
        }
    }
    plants.erase(std::remove_if(plants.begin(),plants.end(),[](const Plant&p){return !p.alive&&p.biomass<=0;}),plants.end());
    if(seconds%Day==0){
        // Boundary seed rain is explicit and independent of colony health.
        seed_bank[rng.bounded(Width)]=uint16_t(3);
        for(int attempt=0;attempt<8&&plants.size()<22;++attempt){int x=int(rng.bounded(Width));
            // Buried seeds respond to their own shallow soil layer. Testing
            // only the exposed surface at midnight prevented regeneration even
            // while the substrate remained moist below its evaporating skin.
            if(!seed_bank[x]||cells[index(x,ground[x]+2)].moisture<45)continue;
            bool crowded=false;for(const auto&p:plants)if(std::abs(p.x-x)<7){crowded=true;break;}
            if(crowded)continue;
            --seed_bank[x];Plant p;p.x=int16_t(x);p.ground_y=ground[x];p.born=uint32_t(seconds);p.biomass=.03f;p.lifespan_days=uint16_t(56+rng.bounded(15));p.kind=uint8_t(rng.bounded(4));plants.push_back(p);++stats.germinations;
        }
    }
    // Small arthropod carcasses arrive from the surrounding meadow. This open
    // ecosystem input has a fixed rate, never a population-dependent rescue.
    if(rng.unit()<config.insect_scale/18.f){int x=3+int(rng.bounded(Width-6));unsigned protein=300+rng.bounded(600);add_food(x,24,protein,1);stats.sugar_input+=24;stats.protein_input+=protein;}
    for(auto&f:foods){
        if(seconds-f.created>3*Day){if(f.sugar)f.sugar-=uint16_t(std::max(1,int(f.sugar/200)));if(f.protein)f.protein-=uint16_t(std::max(1,int(f.protein/250)));}
    }
    foods.erase(std::remove_if(foods.begin(),foods.end(),[](const Food&f){return !f.sugar&&!f.protein;}),foods.end());
    // Corpse slots remain stable while carried; finished slots are retained to
    // avoid changing a carrier's index. Their count is bounded by daily cleanup.
    for(auto&c:corpses)if(c.organic&&seconds-c.died>14*Day&&seconds%Day==0){--c.organic;
        auto&soil=cells[index(c.x,std::max<int>(ground[c.x],c.y))];soil.nutrients=uint8_t(std::min(255,int(soil.nutrients)+1));if(!c.organic)++stats.corpse_decompositions;}
    if(seconds%Day==0){
        // Compact only with an index remap for every carrier.
        std::vector<uint16_t> map(corpses.size(),65535);size_t n=0;
        for(size_t i=0;i<corpses.size();++i)if(corpses[i].organic){map[i]=uint16_t(n);corpses[n++]=corpses[i];}
        for(auto&a:ants)if(a.alive&&a.task==Task::RemoveCorpse){if(a.payload<map.size()&&map[a.payload]!=65535)a.payload=map[a.payload];else a.task=Task::Nurse;}
        corpses.resize(n);
    }
    if(config.enable_collapses&&seconds%(6*3600)==0){
        // Cohesive soil supports narrow corridors; only wide unsupported spans
        // undergo local failure. Every moved parcel remains in the mineral ledger.
        for(int test=0;test<24;++test){int x=3+int(rng.bounded(Width-6)),y=Surface+3+int(rng.bounded(90));
            auto&c=cells[index(x,y)];if(!c.soil||!open(x,y+1))continue;
            int span=0;for(int dx=-4;dx<=4;++dx)if(open(x+dx,y+1))++span;
            if(span<9||rng.bounded(80)!=0)continue;
            int yy=y+1;while(yy+1<Height&&open(x,yy+1))++yy;
            set_soil(index(x,yy),c.soil);set_soil(index(x,y),0);++stats.collapses;topology_dirty_=true;
        }
    }
}
void World::hour(){conditions=weather.sample(seconds,config.seed);ecology();metabolism();rebuild_occupancy();}
void World::advance(uint64_t duration){
    if(paused)return;
    if(duration>std::numeric_limits<uint32_t>::max()-seconds)throw std::invalid_argument("simulation exceeds 136-year timestamp range");
    uint64_t end=seconds+duration;
    while(seconds<end){++seconds;auto& bucket=wheel_[seconds%WheelSize];
        std::sort(bucket.begin(),bucket.end());bucket.erase(std::unique(bucket.begin(),bucket.end()),bucket.end());size_t n=bucket.size();
        for(size_t j=0;j<n;++j){unsigned i=bucket[j];if(i<ants.size()&&ants[i].alive&&ants[i].next_action==seconds)act(i);}
        bucket.erase(bucket.begin(),bucket.begin()+static_cast<std::ptrdiff_t>(n));
        if(seconds==next_minute_){minute();next_minute_+=60;}
        if(seconds==next_hour_){hour();next_hour_+=3600;}
    }
}
void World::power_off(){paused=true;}
void World::restart_after_extinction(){
    // A user-requested replacement of the soil is an explicit world reset, not
    // a conservation violation during ordinary simulation. Surface ecology/time survive.
    init_soil();for(auto&p:plants)p.ground_y=ground[p.x];for(auto&f:foods)f.y=ground[f.x]-1;
    uint64_t number=stats.colony_number+1;stats=Stats{};stats.colony_number=number;
    found_colony();stats.mineral_baseline=mineral_total();
}
void World::power_on(){if(paused&&extinct)restart_after_extinction();paused=false;}
void World::catch_up(uint64_t elapsed){if(!paused)advance(elapsed);}
uint64_t World::mineral_total()const{uint64_t n=0;for(const auto&c:cells)n+=c.soil;for(const auto&a:ants)n+=a.soil_cargo;return n;}
Snapshot World::snapshot()const{
    Snapshot s;s.seconds=seconds;s.workers=worker_count();s.brood=unsigned(brood.size());s.corpses=unsigned(corpses.size());
    for(const auto&p:plants)s.plants+=p.alive;
    for(const auto&f:foods){s.sugar+=f.sugar;s.protein+=f.protein;}
    for(const auto&a:ants)if(a.alive){s.sugar+=a.crop;s.protein+=a.protein;}
    s.food_sites=unsigned(foods.size());s.queen_alive=queen()!=nullptr;s.extinct=extinct;s.mineral=mineral_total();s.births=stats.births;s.deaths=stats.deaths;s.excavated=stats.excavated;return s;
}
size_t World::memory_bytes()const{
    size_t n=sizeof(*this)+cells.capacity()*sizeof(Cell)+ants.capacity()*sizeof(Ant)+brood.capacity()*sizeof(Brood)+corpses.capacity()*sizeof(Corpse)+plants.capacity()*sizeof(Plant)+foods.capacity()*sizeof(Food);
    n+=walkable_cache_.capacity();
    n+=occupancy_.capacity()*sizeof(int32_t)+next_occupant_.capacity()*sizeof(int32_t)+walk_cells_.capacity()*sizeof(uint32_t)+(scratch_home_.capacity()+scratch_trail_.capacity())*sizeof(uint16_t);
    for(const auto&b:wheel_)n+=b.capacity()*sizeof(uint16_t);
    return n;
}
bool World::validate(std::string* error)const{
    auto bad=[&](const char*s){if(error)*error=s;return false;};
    if(cells.size()!=CellCount)return bad("terrain size");
    if(mineral_total()!=stats.mineral_baseline)return bad("mineral conservation");
    if(worker_count()>config.max_workers)return bad("worker capacity");
    for(const auto&a:ants)if(a.alive&&!in_bounds(a.x,a.y))return bad("ant out of world");
    for(const auto&p:plants)if(p.x<0||p.x>=Width||!std::isfinite(p.biomass)||!std::isfinite(p.water))return bad("invalid plant");
    for(const auto&b:brood)if(!in_bounds(b.x,b.y)||!std::isfinite(b.development))return bad("invalid brood");
    return true;
}
const char* task_name(Task t){constexpr const char* names[]={"arriving","exploring","foraging","carrying food","excavating","carrying soil","tending brood","resting","carrying corpse","queen"};return names[unsigned(t)<10?unsigned(t):0];}
} // namespace antfarm
