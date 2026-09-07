#include "antfarm/render.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace antfarm {
namespace {
constexpr uint16_t rgb(int r,int g,int b) {
    return uint16_t(((r&248)<<8)|((g&252)<<3)|(b>>3));
}
uint32_t noise(uint32_t x,uint32_t y,uint64_t seed) {
    uint32_t n=x*0x9e3779b9u+y*0x85ebca6bu+uint32_t(seed)+uint32_t(seed>>32);
    n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;return n^(n>>16);
}
constexpr uint16_t Ink=rgb(40,35,29), Leg=rgb(55,47,36), Leaf=rgb(100,113,75),
    LeafLight=rgb(122,130,88), Stem=rgb(112,114,75), Dead=rgb(145,128,92),
    Egg=rgb(225,213,173), EggShade=rgb(188,170,126), Petal=rgb(223,213,177),
    Pollen=rgb(174,151,84), Root=rgb(171,149,107);
int sgn(int n) { return (n>0)-(n<0); }
uint16_t key(const Cell& c) {return uint16_t(c.soil|((c.moisture>>5)<<4)|((c.nutrients>>5)<<7));}
}

int Renderer::px(float x) const {return int(x*float(width_)/Width+0.5f);}
int Renderer::py(float y) const {return int(y*float(height_)/Height+0.5f);}
void Renderer::invalidate() {cell_keys_.clear();terrain_second_=~uint64_t(0);}
size_t Renderer::memory_bytes() const {
    return sizeof(*this)+terrain_.capacity()*sizeof(uint16_t)+cell_keys_.capacity()*sizeof(uint16_t)+dirty_.capacity()+changed_.capacity();
}
void Renderer::mark(int x0,int y0,int x1,int y1) {
    if(x1<0||y1<0||x0>=width_||y0>=height_)return;
    x0=std::max(0,x0)/Tile;y0=std::max(0,y0)/Tile;
    x1=std::min(width_-1,x1)/Tile;y1=std::min(height_-1,y1)/Tile;
    for(int y=y0;y<=y1;++y)std::fill(dirty_.begin()+y*columns_+x0,dirty_.begin()+y*columns_+x1+1,1);
}
void Renderer::box(int x0,int y0,int x1,int y1,uint16_t color) {
    mark(x0,y0,x1,y1);
    x0=std::max(0,x0);y0=std::max(0,y0);x1=std::min(width_-1,x1);y1=std::min(height_-1,y1);
    if(x0>x1||y0>y1)return;
    for(int y=y0;y<=y1;++y)std::fill(target_+size_t(y)*width_+x0,target_+size_t(y)*width_+x1+1,color);
}
void Renderer::disk(int x,int y,int rx,int ry,uint16_t color) {
    rx=std::max(1,rx);ry=std::max(1,ry);mark(x-rx,y-ry,x+rx,y+ry);
    const int64_t rr=int64_t(rx)*rx*ry*ry;
    for(int dy=-ry;dy<=ry;++dy){
        const int yy=y+dy;if(yy<0||yy>=height_)continue;
        int extent=rx;
        while(extent>0&&int64_t(extent)*extent*ry*ry+int64_t(dy)*dy*rx*rx>rr)--extent;
        const int left=std::max(0,x-extent),right=std::min(width_-1,x+extent);
        if(left<=right)std::fill(target_+size_t(yy)*width_+left,target_+size_t(yy)*width_+right+1,color);
    }
}
void Renderer::line(int x0,int y0,int x1,int y1,uint16_t color,int thickness) {
    const int r=std::max(0,thickness/2);mark(std::min(x0,x1)-r,std::min(y0,y1)-r,std::max(x0,x1)+r,std::max(y0,y1)+r);
    const int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1;int e=dx+dy;
    for(;;){
        if(r)box(x0-r,y0-r,x0+r,y0+r,color);
        else if(x0>=0&&y0>=0&&x0<width_&&y0<height_)target_[size_t(y0)*width_+x0]=color;
        if(x0==x1&&y0==y1)break;
        const int e2=2*e;if(e2>=dy){e+=dy;x0+=sx;}if(e2<=dx){e+=dx;y0+=sy;}
    }
}

void Renderer::background_cell(const World& w,int cx,int cy) {
    const int x0=cx*width_/Width,x1=(cx+1)*width_/Width,y0=cy*height_/Height,y1=(cy+1)*height_/Height;
    const Cell& c=w.cells[w.index(cx,cy)];
    const bool below=cy>=w.ground[cx], hollow=c.soil==0;
    const int moist=int(c.moisture>>5),grain_size=std::max(1,width_/540);
    // Very shallow geological boundaries; all texture is coordinate-stable.
    const int wave=int(noise(uint32_t(cx/9),0,seed_)%3)-1;
    const int depth=cy<178+wave?0:(cy<253+wave?1:2);
    const int base_r=213-depth*8-moist*2,base_g=191-depth*9-moist*2,base_b=145-depth*6-moist;
    const bool left=cx>0&&w.cells[w.index(cx-1,cy)].soil!=0;
    const bool right=cx+1<Width&&w.cells[w.index(cx+1,cy)].soil!=0;
    const bool top=cy>0&&w.cells[w.index(cx,cy-1)].soil!=0;
    const bool bottom=cy+1<Height&&w.cells[w.index(cx,cy+1)].soil!=0;
    for(int gy=y0/grain_size;gy<=(y1-1)/grain_size;++gy)
    for(int gx=x0/grain_size;gx<=(x1-1)/grain_size;++gx){
        const uint32_t n=noise(uint32_t(gx),uint32_t(gy),seed_);
        const int grain=(n%97==0)?-14:((n%19==0)?-5:((n%23==0)?4:0));
        const int start_y=std::max(y0,gy*grain_size),end_y=std::min(y1,(gy+1)*grain_size);
        const int start_x=std::max(x0,gx*grain_size),end_x=std::min(x1,(gx+1)*grain_size);
        for(int y=start_y;y<end_y;++y)for(int x=start_x;x<end_x;++x){
        uint16_t color=sky_;
        if(!hollow){
            int fleck=grain;
            if(c.soil<SoilFull&&((x-x0+2*(y-y0))%8)>=c.soil)fleck-=4;
            color=rgb(base_r+fleck,base_g+fleck,base_b+fleck);
            if(cy<w.ground[cx]+1&&y==y0)color=rgb(base_r-8,base_g-8,base_b-7);
        } else if(below){
            int shade=(n%11==0)?-3:0;
            const int lx=x-x0,rx=x1-1-x,ly=y-y0,by=y1-1-y;
            const int border=std::max(1,width_/1080);
            // Rounded corners are a surface finish of excavated cells, never a generated tunnel.
            const bool corner=(left&&top&&lx+ly<2*border)||(right&&top&&rx+ly<2*border)||
                              (left&&bottom&&lx+by<2*border)||(right&&bottom&&rx+by<2*border);
            const bool edge=(left&&lx<border)||(right&&rx<border)||(top&&ly<border)||(bottom&&by<border);
            color=corner?rgb(base_r-10,base_g-11,base_b-10):
                  (edge?rgb(164-depth*5,141-depth*5,101-depth*3):rgb(139-depth*5+shade,119-depth*5+shade,86-depth*3+shade));
        }
        terrain_[size_t(y)*width_+x]=color;
        }
    }
    mark(x0,y0,x1-1,y1-1);++terrain_cells_;
}

void Renderer::plant(const World& w,const Plant& p) {
    const float maturity=std::max(0.0f,std::min(1.0f,p.biomass));
    if(maturity<0.02f)return;
    const uint32_t n=noise(p.x,p.born,w.config.seed);
    const float scale=std::max(0.16f,maturity),height=(10+int(n%17))*scale;
    const float lean=(int((n>>8)%9)-4)*0.22f;
    const int bx=px(p.x+0.5f),by=py(p.ground_y),tipx=px(p.x+0.5f+lean),tipy=py(p.ground_y-height);
    const int twig=std::max(1,width_/720),leafsize=std::max(1,px(0.6f+scale*0.45f));
    const uint16_t foliage=p.alive?(p.water<0.28f?rgb(131,128,83):Leaf):Dead;
    const uint16_t light=p.alive?LeafLight:rgb(157,139,100);
    // Short rootlets give plants an attachment to soil, clipped to occupied cells.
    for(int branch=-1;branch<=1;++branch){
        int x=bx,y=by;
        for(int i=1;i<=int(5*scale);++i){
            const int nx=bx+px(branch*i*0.33f),ny=by+py(float(i));
            const int cx=std::clamp(nx*Width/width_,0,Width-1),cy=std::clamp(ny*Height/height_,0,Height-1);
            if(w.cells[w.index(cx,cy)].soil==0)break;
            line(x,y,nx,ny,Root);x=nx;y=ny;
        }
    }
    if(p.kind%3==2){
        // Low branching herb, individually grown from the plant's biomass.
        for(int b=-2;b<=2;++b){
            const float h=height*(1.0f-std::abs(b)*0.13f);
            int tx=px(p.x+0.5f+b*1.25f*scale),ty=py(p.ground_y-h*0.62f);
            line(bx,by,tx,ty,Stem,twig);
            for(int leaf=1;leaf<=3;++leaf){
                int xx=bx+(tx-bx)*leaf/3,yy=by+(ty-by)*leaf/3;
                disk(xx,yy,std::max(1,px(scale*(1.2f+0.15f*leaf))),std::max(1,py(scale*0.65f)),leaf%2?foliage:light);
            }
        }
    }else{
        line(bx,by,tipx,tipy,Stem,twig);
        const int leaves=3+int(scale*4);
        for(int j=1;j<=leaves;++j){
            const float f=float(j)/(leaves+2);int x=bx+int((tipx-bx)*f),y=by+int((tipy-by)*f);
            const int side=j%2?1:-1,len=px((2.0f+1.1f*float((n>>j)&1))*scale);
            const int ex=x+side*len,ey=y-py(1.5f*scale);
            line(x,y,ex,ey,foliage,twig);
            disk(ex-side*len/4,ey+py(0.22f),std::max(1,len/2),std::max(1,leafsize/3),j%2?foliage:light);
        }
        if(p.alive&&maturity>0.64f){
            const int r=std::max(1,px(0.6f));
            if(p.kind%3==0){
                for(int j=-1;j<=1;++j){
                    const int xx=tipx+j*r*2,yy=tipy-std::abs(j)*r;
                    line(tipx,tipy+r*3,xx,yy,Stem,twig);
                    disk(xx-r,yy,r,r,Petal);disk(xx+r,yy,r,r,Petal);
                    disk(xx,yy-r,r,r,Petal);disk(xx,yy+r,r,r,Petal);disk(xx,yy,1,1,Pollen);
                }
            }else{
                for(int j=0;j<4;++j)disk(tipx+(j%2?1:-1)*r,tipy+j*r*2,r,r,Pollen);
            }
        }
    }
}

void Renderer::ant(const Ant& a,float alpha,uint64_t seconds) {
    if(!a.alive)return;
    const float progress=std::clamp((float(seconds>=a.last_move?seconds-a.last_move:0)+alpha)*0.5f,0.0f,1.0f);
    const float x=a.previous_x+(a.x-a.previous_x)*progress+0.5f,y=a.previous_y+(a.y-a.previous_y)*progress+0.5f;
    const int cx=px(x),cy=py(y),unit=std::max(1,width_/1080);
    constexpr int hx[8]={256,181,0,-181,-256,-181,0,181},hy[8]={0,181,256,181,0,-181,-256,-181};
    int dir=a.heading%8,dx=sgn(a.x-a.previous_x),dy=sgn(a.y-a.previous_y);
    if(dx||dy)for(int i=0;i<8;++i)if(sgn(hx[i])==dx&&sgn(hy[i])==dy){dir=i;break;}
    const int ux=hx[dir],uy=hy[dir],body=a.queen?8*unit:5*unit;
    auto point=[&](int along,int across){return std::pair<int,int>{cx+(ux*along-uy*across)/256,cy+(uy*along+ux*across)/256};};
    // Gait changes only as the simulation advances; a resting ant never jitters.
    const int phase=(int(a.moves)+int(progress*4))&1;
    const int moving=progress<1&&(a.x!=a.previous_x||a.y!=a.previous_y)?1:0;
    for(int side=-1;side<=1;side+=2)for(int leg=-1;leg<=1;++leg){
        const int stride=moving*((leg+phase+3)%2?unit:-unit);
        auto a0=point(leg*unit,0),a1=point(leg*2*unit+stride,side*2*unit),a2=point(leg*3*unit-stride,side*3*unit);
        line(a0.first,a0.second,a1.first,a1.second,Leg);line(a1.first,a1.second,a2.first,a2.second,Leg);
    }
    const auto tail=point(-body/2,0),middle=point(0,0),head=point(body/2,0);
    const bool horizontal=std::abs(ux)>std::abs(uy);
    disk(tail.first,tail.second,(horizontal?3:2)*unit+(a.queen?unit:0),(horizontal?2:3)*unit+(a.queen?unit:0),Ink);
    disk(middle.first,middle.second,unit,unit,Ink);
    disk(head.first,head.second,2*unit,2*unit,Ink);
    for(int side=-1;side<=1;side+=2){auto elbow=point(body/2+3*unit,side*2*unit),tip=point(body/2+4*unit,side*3*unit);
        line(head.first,head.second,elbow.first,elbow.second,Leg);line(elbow.first,elbow.second,tip.first,tip.second,Leg);}
    if(a.soil_cargo||a.task==Task::ReturnSoil){auto q=point(body/2+5*unit,0);disk(q.first,q.second,2*unit,2*unit,rgb(177,152,105));}
    else if(a.task==Task::ReturnFood&&a.payload){auto q=point(body/2+4*unit,0);disk(q.first,q.second,unit,2*unit,rgb(148,129,79));}
}

void Renderer::render(const World& w,std::vector<uint16_t>& output,int width,int height,float alpha) {
    if(width<180||height<320||width>4096||height>4096)throw std::invalid_argument("render dimensions must be between 180x320 and 4096x4096");
    if(w.cells.size()!=CellCount)throw std::invalid_argument("world has no complete soil grid");
    alpha=std::clamp(alpha,0.0f,1.0f);restored_pixels_=terrain_cells_=0;
    const bool resize=width!=width_||height!=height_;
    if(resize){width_=width;height_=height;columns_=(width+Tile-1)/Tile;rows_=(height+Tile-1)/Tile;
        terrain_.resize(size_t(width)*height);dirty_.assign(size_t(columns_)*rows_,1);invalidate();}
    output.resize(size_t(width)*height);
    if(target_!=output.data()){target_=output.data();std::fill(dirty_.begin(),dirty_.end(),1);}
    const bool all=cell_keys_.size()!=CellCount||seed_!=w.config.seed;
    if(all){cell_keys_.assign(CellCount,0xffff);seed_=w.config.seed;}
    // The whole display stays legible overnight. Colour follows the world clock slowly.
    const double hour=double(w.seconds%Day)/3600.0;
    const double light=std::clamp((std::cos((hour-13.0)*3.141592653589793/12.0)+0.45)/1.45,0.0,1.0);
    const uint16_t next_sky=rgb(int(194+39*light),int(191+38*light),int(164+40*light));
    const bool sky_changed=next_sky!=sky_;sky_=next_sky;
    if(all||terrain_second_!=w.seconds||sky_changed){
        // Reused flags avoid a per-frame temporary allocation; the key is committed
        // after its neighbours have been invalidated, so an edited cell updates edges too.
        if(!all)changed_.assign(CellCount,0);
        for(int y=0;y<Height;++y)for(int x=0;x<Width;++x){const int i=y*Width+x;const uint16_t k=key(w.cells[i]);
            if(all){cell_keys_[i]=k;background_cell(w,x,y);continue;}
            if(k!=cell_keys_[i]){
                cell_keys_[i]=k;
                for(int yy=std::max(0,y-1);yy<=std::min(Height-1,y+1);++yy)
                    for(int xx=std::max(0,x-1);xx<=std::min(Width-1,x+1);++xx)changed_[yy*Width+xx]=1;
            }
            if(sky_changed&&y<w.ground[x]&&w.cells[i].soil==0)changed_[i]=1;
        }
        if(!all)for(int i=0;i<CellCount;++i)if(changed_[i])background_cell(w,i%Width,i/Width);
        terrain_second_=w.seconds;
    }
    for(int ty=0;ty<rows_;++ty)for(int tx=0;tx<columns_;++tx){
        uint8_t& d=dirty_[ty*columns_+tx];if(!d)continue;d=0;
        const int x=tx*Tile,endx=std::min(width_,x+Tile),endy=std::min(height_,(ty+1)*Tile);
        for(int y=ty*Tile;y<endy;++y)std::memcpy(target_+size_t(y)*width_+x,terrain_.data()+size_t(y)*width_+x,size_t(endx-x)*2);
        restored_pixels_+=size_t(endx-x)*(endy-ty*Tile);
    }
    for(const Plant& p:w.plants)plant(w,p);
    const int u=std::max(1,width_/1080);
    for(const Food& f:w.foods){const int x=px(f.x+0.5f),y=py(f.y+0.5f);
        if(f.protein){disk(x,y,3*u,u,rgb(111,101,72));line(x-2*u,y-u,x-u,y-2*u,Leg);line(x+u,y+u,x+2*u,y+2*u,Leg);}
        else if(f.sugar)disk(x,y,u,u,rgb(165,150,89));}
    for(const Corpse& c:w.corpses)if(!c.carried){const int x=px(c.x+0.5f),y=py(c.y+0.5f);
        const uint16_t color=c.organic<10?rgb(125,110,81):rgb(80,69,47);
        line(x-3*u,y+u,x+3*u,y-u,color);disk(x-2*u,y+u,2*u,u,color);
        line(x-u,y-u,x+u,y+2*u,color);line(x+u,y-2*u,x+2*u,y+u,color);}
    for(const Brood& b:w.brood){const int x=px(b.x+0.5f),y=py(b.y+0.5f),r=b.stage==Stage::Egg?u:2*u;
        disk(x,y,r,r+u,EggShade);disk(x,y-u,std::max(1,r-u),r,Egg);}
    for(const Ant& a:w.ants)if(!a.queen)ant(a,alpha,w.seconds);
    for(const Ant& a:w.ants)if(a.queen)ant(a,alpha,w.seconds);
}

void render(const World& world,std::vector<uint16_t>& output,int width,int height,float alpha) {
    Renderer renderer;renderer.render(world,output,width,height,alpha);
}
bool write_ppm(const std::string& path,const std::vector<uint16_t>& pixels,int width,int height) {
    if(width<=0||height<=0||pixels.size()!=size_t(width)*height)return false;
    FILE* file=std::fopen(path.c_str(),"wb");if(!file)return false;
    bool ok=std::fprintf(file,"P6\n%d %d\n255\n",width,height)>0;
    std::vector<uint8_t> row(size_t(width)*3);
    for(int y=0;ok&&y<height;++y){
        for(int x=0;x<width;++x){const uint16_t c=pixels[size_t(y)*width+x];
            const int r=(c>>11)&31,g=(c>>5)&63,b=c&31;
            row[x*3]=uint8_t((r<<3)|(r>>2));row[x*3+1]=uint8_t((g<<2)|(g>>4));row[x*3+2]=uint8_t((b<<3)|(b>>2));}
        ok=std::fwrite(row.data(),1,row.size(),file)==row.size();
    }
    return std::fclose(file)==0&&ok;
}
} // namespace antfarm
