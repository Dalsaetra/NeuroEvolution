#include "neuroevo/ecosystem.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace neuroevo {
const char* obstacle_preset_name(ObstaclePreset preset)
{
    switch(preset) {
        case ObstaclePreset::Sparse: return "sparse";
        case ObstaclePreset::Valleys: return "valleys";
        case ObstaclePreset::Rooms: return "rooms";
        case ObstaclePreset::Labyrinth: return "labyrinth";
        case ObstaclePreset::Mixed: return "mixed";
    }
    throw std::invalid_argument("Invalid obstacle preset");
}

void EcosystemWorld::generate_structured_obstacles(const std::vector<std::size_t>& outside)
{
    if(outside.empty() || config.obstacle_density==0)return;
    const int width=int(config.width),height=int(config.height);
    const int nx=(width-int(config.nursery_size))/2,ny=(height-int(config.nursery_size))/2;
    Random rng(config.seed ^ 0x7374727563747572ULL);
    const auto budget=std::size_t(outside.size()*config.obstacle_density);
    std::vector<unsigned char> allowed(terrain.size()),occupied(terrain.size());
    for(auto cell:outside) {
        const int x=int(cell%config.width),y=int(cell/config.width);
        if(x<3 || y<3 || x>=width-3 || y>=height-3 || reserved_for_food({x+.5,y+.5},1.5))continue;
        bool clear=true;
        // Preserve shelter floors and their immediate entrances.
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            if(terrain[std::size_t(y+dy)*config.width+std::size_t(x+dx)]==Terrain::Shelter)clear=false;
        allowed[cell]=clear;
    }
    const auto connected = [&]() {
        std::vector<unsigned char> seen(terrain.size());
        std::vector<std::size_t> queue;
        std::size_t floor=0;
        for(std::size_t i=0;i<terrain.size();++i) {
            const int x=int(i%config.width),y=int(i/config.width);
            if(terrain[i]==Terrain::Wall || (x>=nx && x<nx+int(config.nursery_size)
                && y>=ny && y<ny+int(config.nursery_size)))continue;
            ++floor;if(queue.empty()){queue.push_back(i);seen[i]=1;}
        }
        for(std::size_t head=0;head<queue.size();++head) {
            const auto i=queue[head];
            for(auto n:{i-1,i+1,i-config.width,i+config.width}) {
                if(n>=terrain.size() || seen[n] || terrain[n]==Terrain::Wall)continue;
                const int x=int(n%config.width),y=int(n/config.width);
                if(x>=nx && x<nx+int(config.nursery_size) && y>=ny && y<ny+int(config.nursery_size))continue;
                seen[n]=1;queue.push_back(n);
            }
        }
        return queue.size()==floor;
    };
    std::size_t painted=0,footprint=0;
    for(std::size_t attempt=0;attempt<600 && painted<budget;++attempt) {
        const auto anchor=outside[rng.uniform_index(outside.size())];
        const int span=std::max(12,int(config.obstacle_scale*rng.uniform(.75,1.25)));
        const int x0=int(anchor%config.width)-span/2,y0=int(anchor/config.width)-span/2;
        if(x0<3 || y0<3 || x0+span>=width-3 || y0+span>=height-3)continue;
        // Structures occupy at most half the frontier, leaving broad open regions.
        if(footprint+std::size_t(span*span)>outside.size()/2)continue;
        bool overlaps=false;
        for(int y=y0-2;y<=y0+span+2 && !overlaps;++y)for(int x=x0-2;x<=x0+span+2;++x)
            if(occupied[std::size_t(y)*config.width+std::size_t(x)]){overlaps=true;break;}
        if(overlaps)continue;
        auto preset=config.obstacle_preset;
        if(preset==ObstaclePreset::Mixed)preset=static_cast<ObstaclePreset>(1+rng.uniform_index(3));
        std::vector<unsigned char> shape(std::size_t(span*span));
        const bool rotate=rng.chance(.5),mirror=rng.chance(.5);
        const auto put = [&](int x,int y) {
            if(rotate)std::swap(x,y);
            if(mirror)x=span-1-x;
            if(x>=0 && y>=0 && x<span && y<span)shape[std::size_t(y*span+x)]=1;
        };
        const auto horizontal = [&](int x0_,int x1_,int y,int door=-100) {
            for(int x=x0_;x<=x1_;++x)if(x<door || x>=door+3)put(x,y);
        };
        const auto vertical = [&](int y0_,int y1_,int x,int door=-100) {
            for(int y=y0_;y<=y1_;++y)if(y<door || y>=door+3)put(x,y);
        };
        if(preset==ObstaclePreset::Valleys) {
            int ridge=span/4;
            const int gap=std::max(5,span/3);
            for(int x=0;x<span;++x) {
                const int previous=ridge;
                if(x && x%4==0)ridge=std::clamp(ridge+(rng.chance(.5)?1:-1),2,span-gap-3);
                for(int y=std::min(previous,ridge);y<=std::max(previous,ridge);++y) {
                    put(x,y);put(x,y+gap);
                }
            }
        } else if(preset==ObstaclePreset::Rooms) {
            const int last=span-1,mid=span/2;
            horizontal(0,last,0,span/3);horizontal(0,last,last,2*span/3-1);
            vertical(0,last,0);vertical(0,last,last);
            // A T-shaped partition gives connected room walls with usable doors.
            vertical(1,last-1,mid,span/3);
            horizontal(mid+1,last-1,mid,mid+2);
        } else {
            // Recursive division: every partition has a three-cell passage.
            // Stop with rooms at least five cells wide; four outer gates connect
            // the maze to surrounding open ground.
            const int last=span-1,door=span/2-1;
            horizontal(0,last,0,door);horizontal(0,last,last,door);
            vertical(0,last,0,door);vertical(0,last,last,door);
            std::function<void(int,int,int,int)> divide;
            divide = [&](int left,int top,int right,int bottom) {
                const int w=right-left+1,h=bottom-top+1;
                if(w<13 && h<13)return;
                const bool split_x=w>=13 && (h<13 || w>h || (w==h && rng.chance(.5)));
                if(split_x) {
                    const int x=left+5+int(rng.uniform_index(std::size_t(w-10)));
                    const int door=top+1+int(rng.uniform_index(std::size_t(h-4)));
                    vertical(top,bottom,x,door);
                    divide(left,top,x-1,bottom);divide(x+1,top,right,bottom);
                } else {
                    const int y=top+5+int(rng.uniform_index(std::size_t(h-10)));
                    const int door=left+1+int(rng.uniform_index(std::size_t(w-4)));
                    horizontal(left,right,y,door);
                    divide(left,top,right,y-1);divide(left,y+1,right,bottom);
                }
            };
            divide(1,1,span-2,span-2);
        }
        std::vector<std::pair<std::size_t,Terrain>> previous;
        for(int y=0;y<span;++y)for(int x=0;x<span;++x) {
            const auto cell=std::size_t(y0+y)*config.width+std::size_t(x0+x);
            if(shape[std::size_t(y*span+x)] && allowed[cell])previous.emplace_back(cell,terrain[cell]);
        }
        // Clipping around food/shelters may leave tiny scraps: retain only long connected walls.
        std::vector<unsigned char> member(terrain.size());
        for(const auto& p:previous)member[p.first]=1;
        for(const auto& p:previous)if(member[p.first]==1) {
            std::vector<std::size_t> component{p.first};member[p.first]=2;
            for(std::size_t head=0;head<component.size();++head) {
                const auto cell=component[head];
                for(auto n:{cell-1,cell+1,cell-config.width,cell+config.width})
                    if(n<member.size() && member[n]==1){member[n]=2;component.push_back(n);}
            }
            if(component.size()<5)for(auto cell:component)member[cell]=0;
        }
        previous.erase(std::remove_if(previous.begin(),previous.end(),[&](const auto& p){return !member[p.first];}),previous.end());
        if(previous.size()<std::size_t(span) || painted+previous.size()>budget)continue;
        for(const auto& p:previous)terrain[p.first]=Terrain::Wall;
        if(!connected()) {for(const auto& p:previous)terrain[p.first]=p.second;continue;}
        painted+=previous.size();footprint+=std::size_t(span*span);
        for(int y=y0;y<y0+span;++y)for(int x=x0;x<x0+span;++x)occupied[std::size_t(y)*config.width+std::size_t(x)]=1;
    }
}
} // namespace neuroevo
