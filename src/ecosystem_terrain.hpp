#pragma once
#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace neuroevo {
// Repaint only walkable outdoor floor after obstacles/shelters are finalized.
// A private stream keeps resources, shelters, and walls independent of texture.
inline void cluster_rough_ground(EcosystemWorld& world, double fraction)
{
    const auto width=world.config.width,height=world.config.height;
    std::vector<std::size_t> eligible;
    for(std::size_t i=0;i<world.terrain.size();++i)
        if(world.terrain[i]==Terrain::Ground || world.terrain[i]==Terrain::Rough) {
            world.terrain[i]=Terrain::Ground;eligible.push_back(i);
        }
    if(eligible.empty())return;
    Random rng(world.config.seed ^ 0x726f756768626c6fULL);
    const auto target=std::size_t(std::round(eligible.size()*fraction));
    std::size_t painted=0;
    for(std::size_t attempt=0;attempt<eligible.size() && painted<target;++attempt) {
        const auto center=eligible[rng.uniform_index(eligible.size())];
        const double cx=double(center%width)+.5,cy=double(center/width)+.5;
        const double rx=rng.uniform(3.0,6.0),ry=rng.uniform(2.0,4.0);
        const double angle=rng.uniform(0.0,6.283185307179586);
        const double c=std::cos(angle),s=std::sin(angle);
        std::vector<std::size_t> patch;
        for(int y=std::max(1,int(cy)-7);y<std::min(int(height)-1,int(cy)+8);++y)
            for(int x=std::max(1,int(cx)-7);x<std::min(int(width)-1,int(cx)+8);++x) {
                const double dx=x+.5-cx,dy=y+.5-cy;
                const double u=(dx*c+dy*s)/rx,v=(-dx*s+dy*c)/ry;
                const auto i=std::size_t(y)*width+std::size_t(x);
                if(u*u+v*v<=1 && world.terrain[i]==Terrain::Ground)patch.push_back(i);
            }
        // Keep whole ellipses instead of clipping the last patch into noise.
        if(patch.size()<8)continue;
        for(auto i:patch)world.terrain[i]=Terrain::Rough;
        painted+=patch.size();
    }
    // Obstacles and shelter footprints can clip a patch into tiny fragments.
    // Remove those fragments so isolated rough tiles do not survive the repaint.
    std::vector<unsigned char> seen(world.terrain.size());
    for(auto i:eligible)if(!seen[i] && world.terrain[i]==Terrain::Rough) {
        std::vector<std::size_t> component{i};seen[i]=1;
        for(std::size_t head=0;head<component.size();++head) {
            const auto cell=component[head];
            for(auto n:{cell-1,cell+1,cell-width,cell+width})
                if(n<world.terrain.size() && !seen[n] && world.terrain[n]==Terrain::Rough) {
                    seen[n]=1;component.push_back(n);
                }
        }
        if(component.size()<8)for(auto cell:component)world.terrain[cell]=Terrain::Ground;
    }
}
} // namespace neuroevo
