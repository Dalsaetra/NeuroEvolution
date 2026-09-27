#include "fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace neuroevo;
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
std::string saved(const EcosystemWorld& w){std::ostringstream s;w.save_checkpoint(s);return s.str();}
Vec2 position(const EcosystemWorld& w,std::size_t i){return {double(i%w.config.width)+.5,double(i/w.config.width)+.5};}
std::size_t check_layout(const EcosystemWorld& w)
{
    const auto width=w.config.width,height=w.config.height;
    std::vector<unsigned char> seen(w.terrain.size());
    std::vector<std::size_t> queue;
    std::size_t floor=0,walls=0,largest=0,clearings=0;
    const auto outdoor_wall=[&](std::size_t i) {
        return i<seen.size() && i%width>0 && i%width+1<width && i/width>0 && i/width+1<height
            && w.terrain[i]==Terrain::Wall && !w.in_nursery(position(w,i));
    };
    for(std::size_t i=0;i<w.terrain.size();++i) {
        if(w.terrain[i]!=Terrain::Wall && !w.in_nursery(position(w,i))) {
            ++floor;if(queue.empty()){queue.push_back(i);seen[i]=1;}
        }
        if(outdoor_wall(i)) {
            ++walls;
            check(!w.reserved_for_food(position(w,i),1.5),"Obstacle intrudes on source-food clearing");
        }
        // Count centers of genuinely open 9x9 outdoor areas.
        const auto x=i%width,y=i/width;
        if(x<5 || y<5 || x+5>=width || y+5>=height || w.in_nursery(position(w,i)))continue;
        bool open=true;
        for(auto yy=y-4;yy<=y+4 && open;++yy)for(auto xx=x-4;xx<=x+4;++xx)
            if(w.terrain[yy*width+xx]==Terrain::Wall){open=false;break;}
        clearings+=open;
    }
    for(std::size_t head=0;head<queue.size();++head)for(auto n:{queue[head]-1,queue[head]+1,queue[head]-width,queue[head]+width})
        if(n<seen.size() && !seen[n] && w.terrain[n]!=Terrain::Wall && !w.in_nursery(position(w,n))) {
            seen[n]=1;queue.push_back(n);
        }
    check(queue.size()==floor,"Structured walls disconnect outdoor floor");
    check(clearings>100,"Structured walls leave too little open ground");
    seen.assign(seen.size(),0);
    for(std::size_t i=0;i<seen.size();++i)if(outdoor_wall(i) && !seen[i]) {
        std::vector<std::size_t> component{i};seen[i]=1;
        for(std::size_t h=0;h<component.size();++h)for(auto n:{component[h]-1,component[h]+1,component[h]-width,component[h]+width})
            if(outdoor_wall(n) && !seen[n]){seen[n]=1;component.push_back(n);}
        check(component.size()>=5,"Clipping left isolated wall scraps");
        largest=std::max(largest,component.size());
    }
    check(largest>=12,"Preset lacks long connected wall structures");
    check(walls<=std::size_t(width*height*w.config.obstacle_density),"Wall density exceeds budget");
    for(const auto& r:w.resources)check(w.traversable(r.position),"Wall obstructs food");
    return walls;
}
}
int main() try {
    for(auto preset:{ObstaclePreset::Valleys,ObstaclePreset::Rooms,ObstaclePreset::Labyrinth,ObstaclePreset::Mixed})
        for(auto seed:{1u,7u,19u})for(bool sources:{false,true}) {
        EcosystemConfig cfg;cfg.initial_creatures=0;cfg.seed=seed;cfg.obstacle_preset=preset;
        if(!sources)cfg.food_distribution=FoodDistribution::Scattered;
        if(seed==7)cfg.nursery_exit_width=0;
        EcosystemWorld w(cfg),repeat(cfg);
        check(w.terrain==repeat.terrain,"Obstacle layout is not deterministic");
        check_layout(w);
        auto open_cfg=cfg;open_cfg.obstacle_density=0;EcosystemWorld open(open_cfg);
        const auto x0=(cfg.width-cfg.nursery_size)/2,y0=(cfg.height-cfg.nursery_size)/2;
        for(std::size_t y=0;y<cfg.height;++y)for(std::size_t x=0;x<cfg.width;++x) {
            const auto i=y*cfg.width+x;
            if((x+3>=x0 && x<=x0+cfg.nursery_size+2 && y+3>=y0 && y<=y0+cfg.nursery_size+2)
                || open.terrain[i]==Terrain::Shelter)
                check((w.terrain[i]==Terrain::Wall)==(open.terrain[i]==Terrain::Wall),"Obstacles changed nursery approaches or shelter floors");
        }
        std::istringstream input(saved(w));auto restored=EcosystemWorld::load_checkpoint(input);
        check(saved(w)==saved(restored),"Obstacle checkpoint did not roundtrip");
        w.step();restored.step();check(saved(w)==saved(restored),"Obstacle checkpoint continuation differs");
    }
    auto cfg=scattered_config();cfg.initial_creatures=0;EcosystemWorld sparse(cfg);
    auto old=saved(sparse);old.replace(0,21,"NEUROEVO_ECOSYSTEM_46");old.erase(old.find("OBSTACLES_1"));old+="END_ECOSYSTEM\n";
    std::istringstream input(old);auto legacy=EcosystemWorld::load_checkpoint(input);
    check(legacy.config.obstacle_preset==ObstaclePreset::Sparse && legacy.terrain==sparse.terrain,"Old world was regenerated");
    check(saved(legacy)==saved(sparse),"Old obstacle configuration changed");
    for(int invalid=0;invalid<4;++invalid) {
        auto c=cfg;
        if(invalid==0)c.obstacle_density=-.1;
        if(invalid==1)c.obstacle_density=.16;
        if(invalid==2)c.obstacle_scale=11;
        if(invalid==3)c.obstacle_preset=static_cast<ObstaclePreset>(99);
        bool rejected=false;try{c.validate();}catch(const std::invalid_argument&){rejected=true;}
        check(rejected,"Invalid obstacle setting accepted");
    }
    std::cout<<"Obstacle presets preserve connected floor, clearings, and checkpoints\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
