#pragma once
#include "core.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace antfarm {

// RGB565, native-endian. The display driver owns scanout and any byte swap.
// Keep one Renderer and one output vector alive between frames to reuse memory.
class Renderer {
public:
    // alpha is a fractional second past world.seconds; movement uses last_move.
    void render(const World& world, std::vector<uint16_t>& output,
                int width=1080, int height=1920, float alpha=1.0f);
    size_t memory_bytes() const;
    size_t last_restored_pixels() const { return restored_pixels_; }
    size_t last_terrain_cells() const { return terrain_cells_; }
    void invalidate();
private:
    static constexpr int Tile=32;
    int width_=0,height_=0,columns_=0,rows_=0;
    uint16_t sky_=0;
    uint64_t seed_=0, terrain_second_=~uint64_t(0);
    std::vector<uint16_t> terrain_,cell_keys_;
    std::vector<uint8_t> dirty_,changed_;
    uint16_t* target_=nullptr;
    size_t restored_pixels_=0,terrain_cells_=0;
    int px(float x) const;
    int py(float y) const;
    void mark(int x0,int y0,int x1,int y1);
    void box(int x0,int y0,int x1,int y1,uint16_t color);
    void disk(int x,int y,int rx,int ry,uint16_t color);
    void line(int x0,int y0,int x1,int y1,uint16_t color,int thickness=1);
    void background_cell(const World& world,int cx,int cy);
    void plant(const World& world,const Plant& p);
    void ant(const Ant& ant,float alpha,uint64_t seconds);
};

// Convenience API; use an owned Renderer for several independent displays.
void render(const World& world,std::vector<uint16_t>& output,
            int width=1080,int height=1920,float alpha=1.0f);
bool write_ppm(const std::string& path,const std::vector<uint16_t>& pixels,
               int width=1080,int height=1920);
} // namespace antfarm
